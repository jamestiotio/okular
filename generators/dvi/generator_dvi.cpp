/***************************************************************************
 *   Copyright (C) 2006 by Luigi Toscano <luigi.toscano@tiscali.it>        *
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 2 of the License, or     *
 *   (at your option) any later version.                                   *
 ***************************************************************************/

#include "generator_dvi.h"
#include "core/page.h"
#include "core/link.h"
#include "core/utils.h"
#include "dviFile.h"
#include "dviPageInfo.h"
#include "dviRenderer.h"
#include "pageSize.h"

#include <qapplication.h>
#include <QX11Info>
#include <qstring.h>
#include <qurl.h>
#include <qvector.h>
#include <qstack.h>

#include <kdebug.h>
#include <kimageeffect.h>
#include <klocale.h>

OKULAR_EXPORT_PLUGIN(DviGenerator)

DviGenerator::DviGenerator( KPDFDocument * doc ) : Generator ( doc ),
  m_docInfo( 0 ), m_docSynopsis( 0 ), ready( false ), m_dviRenderer( 0 )
{
}

bool DviGenerator::loadDocument( const QString & fileName, QVector< KPDFPage * > &pagesVector )
{
    //kDebug() << "file: " << qPrintable( fileName ) << endl;
    KUrl base( fileName );

    delete m_docInfo;
    m_docInfo = 0;
    delete m_docSynopsis;
    m_docSynopsis = 0;

    delete m_dviRenderer;

    m_dviRenderer = new dviRenderer();
    if ( ! m_dviRenderer->setFile( fileName, base ) )
        return false;

    kDebug() << "# of pages: " << m_dviRenderer->dviFile->total_pages << endl;

    m_resolution = QX11Info::appDpiY();
    loadPages( pagesVector, 0 );

    ready = true;
    return true;
}

bool DviGenerator::canGeneratePixmap ( bool /*async*/ )
{
    return ready;
}

static void rotateCoordinates( const int iWidth, const int iHeight, 
                               int &fWidth, int &fHeight, const int orientation)
{
    if ( orientation % 2 == 0 ) 
    {
        /* portrait */
        fWidth = iWidth;
        fHeight = iHeight;
    }
    else
    {
        /* landscape */
        fWidth = iHeight;
        fHeight = iWidth;
    }

}

static void rotateCoordinates( const double iWidth, const double iHeight, 
                               double &fWidth, double &fHeight, const int orientation)
{
    if ( orientation % 2 == 0 ) 
    {
        /* portrait */
        fWidth = iWidth;
        fHeight = iHeight;
    }
    else
    {
        /* landscape */
        fWidth = iHeight;
        fHeight = iWidth;
    }

}

void DviGenerator::fillViewportFromAnchor( DocumentViewport &vp,
                                           const Anchor &anch, int pW, int pH, 
                                           int orientation ) 
{
    vp.pageNumber = anch.page - 1;

    double vp_x = 0.0, vp_y = 0.0;

    SimplePageSize ps = m_dviRenderer->sizeOfPage( vp.pageNumber );
    double resolution = 0;

    if ( orientation % 2 == 0 )
        resolution = (double)(pW)/ps.width().getLength_in_inch();
    else
        resolution = (double)(pH)/ps.height().getLength_in_inch();

    double py = (double)anch.distance_from_top.getLength_in_inch()*resolution + 0.5; 
 
    rotateCoordinates( 0.5, py / (double)pH,
                       vp_x, vp_y, orientation );
    vp.rePos.normalizedX = vp_x;
    vp.rePos.normalizedY = vp_y;
    vp.rePos.enabled = true;
    vp.rePos.pos = DocumentViewport::Center;
}

QLinkedList<ObjectRect*> DviGenerator::generateDviLinks( const dviPageInfo *pageInfo,   
                                                         int orientation )
{
    QLinkedList<ObjectRect*> dviLinks;

    int pageWidth = 0, pageHeight = 0;

    rotateCoordinates( pageInfo->width, pageInfo->height, 
                       pageWidth, pageHeight, orientation );

    foreach( const Hyperlink dviLink, pageInfo->hyperLinkList )
    {
        QRect boxArea = okularUtils::rotateRect( dviLink.box, pageWidth, pageHeight, 
                                                 orientation );
        double nl = (double)boxArea.left() / pageWidth,
               nt = (double)boxArea.top() / pageHeight,
               nr = (double)boxArea.right() / pageWidth,
               nb = (double)boxArea.bottom() / pageHeight;

        Anchor anch = m_dviRenderer->findAnchor(dviLink.linkText);

        KPDFLink *okuLink = 0;

        /* distinguish between local (-> anchor) and remote links */
        if (anch.isValid())
        {
            /* internal link */
            DocumentViewport vp;
            fillViewportFromAnchor( vp, anch, pageWidth, pageHeight,
                                    orientation );
            okuLink = new KPDFLinkGoto( "", vp );
        }
        else
        {
            okuLink = new KPDFLinkBrowse( dviLink.linkText );
        }
        if ( okuLink ) 
        {
            ObjectRect *orlink = new ObjectRect( nl, nt, nr, nb, false, ObjectRect::Link, 
                                                 okuLink );
            dviLinks.push_front( orlink );
        }
	
    }
    return dviLinks; 
}

void DviGenerator::generatePixmap( PixmapRequest *request )
{

    dviPageInfo *pageInfo = new dviPageInfo();
    pageSize ps;

    rotateCoordinates( request->width, request->height, 
                       pageInfo->width, pageInfo->height, request->documentRotation );

    pageInfo->pageNumber = request->pageNumber + 1;

//  pageInfo->resolution = m_resolution;

    SimplePageSize s = m_dviRenderer->sizeOfPage( pageInfo->pageNumber );

/*   if ( s.width() != pageInfo->width) */
    //   if (!useDocumentSpecifiedSize)
    //    s = userPreferredSize;

    if (s.isValid())
    {
        ps = s; /* it should be the user specified size, if any, instead */
    }

    pageInfo->resolution = (double)(pageInfo->width)/ps.width().getLength_in_inch();

#if 0
    kDebug() << request
    << ", res:" << pageInfo->resolution << " - (" << pageInfo->width << ","
    << ps.width().getLength_in_inch() << ")," << ps.width().getLength_in_mm()
    << endl;
#endif

    if ( m_dviRenderer )
    {
        m_dviRenderer->drawPage( pageInfo );

        if ( ! pageInfo->img.isNull() )
        {
            kDebug() << "Image OK" << endl;

            if ( request->documentRotation > 0 )
                pageInfo->img = KImageEffect::rotate( pageInfo->img,
                   (KImageEffect::RotateDirection)( request->documentRotation - 1 ) );

            QPixmap *tmpPx = new QPixmap();
            *tmpPx = QPixmap::fromImage( pageInfo->img );
            request->page->setPixmap( request->id, tmpPx );

            request->page->setObjectRects(
                     generateDviLinks( pageInfo, request->documentRotation ) );
        }
    }

    ready = true;

    delete pageInfo;

    signalRequestDone( request );
}

bool DviGenerator::canGenerateTextPage()
{
    return true;
}

void DviGenerator::generateSyncTextPage( KPDFPage* page )
{
    kDebug() << "DviGenerator::generateSyncTextPage( KPDFPage * page )" << endl;
    dviPageInfo *pageInfo = new dviPageInfo();
    pageSize ps;

    rotateCoordinates( (int)(page->width()), (int)(page->height()),
                       pageInfo->width, pageInfo->height, page->orientation() );

    pageInfo->pageNumber = page->number() + 1;

    pageInfo->resolution = m_resolution;
    SimplePageSize s = m_dviRenderer->sizeOfPage( pageInfo->pageNumber );
    pageInfo->resolution = (double)(pageInfo->width)/ps.width().getLength_in_inch();

    // get page text from m_dviRenderer
    if ( m_dviRenderer )
    {
        m_dviRenderer->getText( pageInfo );

        KPDFTextPage *ktp = extractTextFromPage( pageInfo, page->orientation() );
        page->setSearchPage( ktp );
    }
    delete pageInfo;
}

KPDFTextPage *DviGenerator::extractTextFromPage( dviPageInfo *pageInfo, int orientation )
{
    QList<KPDFTextEntity*> textOfThePage;

    QVector<TextBox>::ConstIterator it = pageInfo->textBoxList.constBegin();
    QVector<TextBox>::ConstIterator itEnd = pageInfo->textBoxList.constEnd();
    QRect tmpRect;

    int pageWidth = 0, pageHeight = 0;

    rotateCoordinates( pageInfo->width, pageInfo->height, 
                       pageWidth, pageHeight, orientation );

    for ( ; it != itEnd ; ++it )
    {
        TextBox curTB = *it;
 
        tmpRect = okularUtils::rotateRect( curTB.box, pageWidth, pageHeight, orientation );

#if 0
        kDebug() << "orientation: " << orientation
                 << ", curTB.box: " << curTB.box
                 << ", tmpRect: " << tmpRect 
                 << ", ( " << pageWidth << "," << pageHeight << " )" 
               <<endl;
#endif
        textOfThePage.push_back( new KPDFTextEntity( curTB.text,
              new NormalizedRect( tmpRect, pageWidth, pageHeight ) ) );
    }

    KPDFTextPage* ktp = new KPDFTextPage( textOfThePage );

    return ktp;
}

const DocumentInfo *DviGenerator::generateDocumentInfo()
{
    if ( m_docInfo )
        return m_docInfo;

    m_docInfo = new DocumentInfo();

    m_docInfo->set( "mimeType", "application/x-dvi" );

    if ( m_dviRenderer && m_dviRenderer->dviFile )
    {
        dvifile *dvif = m_dviRenderer->dviFile;

        // read properties from dvif
        //m_docInfo->set( "filename", dvif->filename, i18n("Filename") );
        m_docInfo->set( "generatorDate", dvif->generatorString,
                       i18n("Generator/Date") );
        m_docInfo->set( "pages", QString::number( dvif->total_pages ),
                       i18n("Pages") );
    }
    return m_docInfo;
}

const DocumentSynopsis *DviGenerator::generateDocumentSynopsis()
{
    if ( m_docSynopsis )
        return m_docSynopsis;

    m_docSynopsis = new DocumentSynopsis();
 
    QStack<QDomElement*> stack;

    QVector<PreBookmark> prebookmarks = m_dviRenderer->getPrebookmarks();

    if ( prebookmarks.isEmpty() ) 
        return m_docSynopsis;

    QVector<PreBookmark>::ConstIterator it = prebookmarks.begin();
    QVector<PreBookmark>::ConstIterator itEnd = prebookmarks.end();
    for( ; it != itEnd; ++it ) 
    {
        QDomElement *domel = new QDomElement; 
        *domel= m_docSynopsis->createElement( (*it).title );

        Anchor a = m_dviRenderer->findAnchor((*it).anchorName);
        if ( a.isValid() )
        {
            DocumentViewport vp;
 
            const KPDFPage *p = m_document->page( a.page - 1 );
            /* Don't care about rotations... */
            fillViewportFromAnchor( vp, a, (int)p->width(), (int)p->height(), 0 );
            domel->setAttribute( "Viewport", vp.toString() );
        }
        if ( stack.isEmpty() )
            m_docSynopsis->appendChild( *domel );
        else 
        {
            stack.top()->appendChild( *domel );
            stack.pop();
        }
        for ( int i = 0; i < (*it).noOfChildren; ++i )
            stack.push( domel );
    }

    return m_docSynopsis;
}

void DviGenerator::setOrientation(QVector<KPDFPage*> & pagesVector, int orientation)
{
    loadPages( pagesVector, orientation );
}

void DviGenerator::loadPages( QVector< KPDFPage * > &pagesVector, int orientation )
{
    QSize pageRequiredSize;

    int numofpages = m_dviRenderer->dviFile->total_pages;
    pagesVector.resize( numofpages );

    //kDebug() << "resolution: " << m_resolution << ", dviFile->preferred? " << endl;

    /* get the suggested size */
    if ( m_dviRenderer->dviFile->suggestedPageSize )
    {
        pageRequiredSize = m_dviRenderer->dviFile->suggestedPageSize->sizeInPixel(
               m_resolution );
    }
    else
    {
        pageSize ps;
        pageRequiredSize = ps.sizeInPixel( m_resolution );
    }


    if ( orientation % 2 != 0 )
    {
        /* landscape */
        pageRequiredSize = QSize ( pageRequiredSize.height(),
                                   pageRequiredSize.width() );
    }

    for ( int i = 0; i < numofpages; ++i )
    {
        //kDebug() << "getting status of page " << i << ":" << endl;

        if ( pagesVector[i] )
        {
            delete pagesVector[i];
        }

        KPDFPage * page = new KPDFPage( i,
                                        pageRequiredSize.width(),
                                        pageRequiredSize.height(),
                                        orientation );
        pagesVector[i] = page;
    }
    kDebug() << "pagesVector successfully inizialized ! " << endl;
}

#include "generator_dvi.moc"
