/***************************************************************************
 *   Copyright (C) 2002 by Wilco Greven <greven@kde.org>                   *
 *   Copyright (C) 2002 by Chris Cheney <ccheney@cheney.cx>                *
 *   Copyright (C) 2002 by Malcolm Hunter <malcolm.hunter@gmx.co.uk>       *
 *   Copyright (C) 2003-2004 by Christophe Devriese                        *
 *                         <Christophe.Devriese@student.kuleuven.ac.be>    *
 *   Copyright (C) 2003 by Daniel Molkentin <molkentin@kde.org>            *
 *   Copyright (C) 2003 by Andy Goossens <andygoossens@telenet.be>         *
 *   Copyright (C) 2003 by Dirk Mueller <mueller@kde.org>                  *
 *   Copyright (C) 2003 by Laurent Montel <montel@kde.org>                 *
 *   Copyright (C) 2004 by Dominique Devriese <devriese@kde.org>           *
 *   Copyright (C) 2004 by Christoph Cullmann <crossfire@babylon2k.de>     *
 *   Copyright (C) 2004 by Henrique Pinto <stampede@coltec.ufmg.br>        *
 *   Copyright (C) 2004 by Waldo Bastian <bastian@kde.org>                 *
 *   Copyright (C) 2004 by Albert Astals Cid <tsdgeos@terra.es>            *
 *   Copyright (C) 2004 by Antti Markus <antti.markus@starman.ee>          *
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 2 of the License, or     *
 *   (at your option) any later version.                                   *
 ***************************************************************************/

#include <qsplitter.h>
#include <qpainter.h>
#include <qlayout.h>
#include <qlabel.h>
#include <qvbox.h>
#include <qtoolbox.h>
#include <qpushbutton.h>

#include <dcopobject.h>
#include <kaction.h>
#include <kinstance.h>
#include <kprinter.h>
#include <kstdaction.h>
#include <kdeversion.h>
#include <kparts/genericfactory.h>
#include <kurldrag.h>
#include <kfiledialog.h>
#include <kmessagebox.h>
#include <kfinddialog.h>
#include <knuminput.h>
#include <kiconloader.h>
#include <kio/netaccess.h>

#include "kpdf_error.h"

#include "GlobalParams.h"

#include "kpdf_part.h"
#include "pageview.h"
#include "thumbnaillist.h"
#include "searchwidget.h"
#include "document.h"
#include "page.h"
#include "toc.h"
#include "preferencesdialog.h"
#include "settings.h"

typedef KParts::GenericFactory<KPDF::Part> KPDFPartFactory;
K_EXPORT_COMPONENT_FACTORY(libkpdfpart, KPDFPartFactory)

using namespace KPDF;

unsigned int Part::m_count = 0;

Part::Part(QWidget *parentWidget, const char *widgetName,
           QObject *parent, const char *name,
           const QStringList & /*args*/ )
	: DCOPObject("kpdf"), KParts::ReadOnlyPart(parent, name)
{
	// create browser extension (for printing when embedded into browser)
	new BrowserExtension(this);

	// xpdf 'extern' global class (m_count is a static instance counter)
	//if ( m_count ) TODO check if we need to insert these lines..
	//	delete globalParams;
	globalParams = new GlobalParams("");
	globalParams->setupBaseFonts(NULL);
	m_count++;

	// we need an instance
	setInstance(KPDFPartFactory::instance());

	// build the document
	document = new KPDFDocument();
	connect( document, SIGNAL( pageChanged() ), this, SLOT( updateActions() ) );

	// widgets: ^searchbar (toolbar containing label and SearchWidget)
//	m_searchToolBar = new KToolBar( parentWidget, "searchBar" );
//	m_searchToolBar->boxLayout()->setSpacing( KDialog::spacingHint() );
//	QLabel * sLabel = new QLabel( i18n( "&Search:" ), m_searchToolBar, "kde toolbar widget" );
//	m_searchWidget = new SearchWidget( m_searchToolBar, document );
//	sLabel->setBuddy( m_searchWidget );
//	m_searchToolBar->setStretchableWidget( m_searchWidget );

	// widgets: [] splitter []
	m_splitter = new QSplitter( parentWidget, widgetName );
	m_splitter->setOpaqueResize( true );
	setWidget( m_splitter );

	// widgets: [left toolbox] | []
	m_toolBox = new QToolBox( m_splitter );
	m_toolBox->setMinimumWidth( 60 );
	m_toolBox->setMaximumWidth( 200 );

	TOC * tocFrame = new TOC( m_toolBox, document );
	m_toolBox->addItem( tocFrame, QIconSet(SmallIcon("text_left")), i18n("Contents") );
	connect(tocFrame, SIGNAL(hasTOC(bool)), this, SLOT(enableTOC(bool)));
	enableTOC( false );

	QVBox * thumbsBox = new ThumbnailsBox( m_toolBox );
    m_searchWidget = new SearchWidget( thumbsBox, document );
    m_thumbnailList = new ThumbnailList( thumbsBox, document );
	connect( m_thumbnailList, SIGNAL( urlDropped( const KURL& ) ), SLOT( openURL( const KURL & )));
	m_toolBox->addItem( thumbsBox, QIconSet(SmallIcon("thumbnail")), i18n("Thumbnails") );
	m_toolBox->setCurrentItem( thumbsBox );

	QFrame * bookmarksFrame = new QFrame( m_toolBox );
	int iIdx = m_toolBox->addItem( bookmarksFrame, QIconSet(SmallIcon("bookmark")), i18n("Bookmarks") );
	m_toolBox->setItemEnabled( iIdx, false );

	QFrame * editFrame = new QFrame( m_toolBox );
	iIdx = m_toolBox->addItem( editFrame, QIconSet(SmallIcon("pencil")), i18n("Annotations") );
	m_toolBox->setItemEnabled( iIdx, false );

	// widgets: [] | [right 'pageView']
	m_pageView = new PageView( m_splitter, document );
	connect( m_pageView, SIGNAL( urlDropped( const KURL& ) ), SLOT( openURL( const KURL & )));
	//connect(m_pageView, SIGNAL( rightClick() ), this, SIGNAL( rightClick() ));

	// add document observers
	document->addObserver( m_thumbnailList );
	document->addObserver( m_pageView );
	document->addObserver( tocFrame );

	// ACTIONS
	KActionCollection * ac = actionCollection();

	// Page Traversal actions
	m_gotoPage = KStdAction::gotoPage( this, SLOT( slotGoToPage() ), ac, "goto_page" );
	m_gotoPage->setShortcut( "CTRL+G" );

	m_prevPage = KStdAction::prior(this, SLOT(slotPreviousPage()), ac, "previous_page");
	m_prevPage->setWhatsThis( i18n( "Moves to the previous page of the document" ) );
	m_prevPage->setShortcut( "Backspace" );

	m_nextPage = KStdAction::next(this, SLOT(slotNextPage()), ac, "next_page" );
	m_nextPage->setWhatsThis( i18n( "Moves to the next page of the document" ) );
	m_nextPage->setShortcut( "Space" );

	m_firstPage = KStdAction::firstPage( this, SLOT( slotGotoFirst() ), ac, "first_page" );
	m_firstPage->setWhatsThis( i18n( "Moves to the first page of the document" ) );

	m_lastPage = KStdAction::lastPage( this, SLOT( slotGotoLast() ), ac, "last_page" );
	m_lastPage->setWhatsThis( i18n( "Moves to the last page of the document" ) );

	// Find and other actions
	m_find = KStdAction::find( this, SLOT( slotFind() ), ac, "find" );
	m_find->setEnabled(false);

	m_findNext = KStdAction::findNext( this, SLOT( slotFindNext() ), ac, "find_next" );
	m_findNext->setEnabled(false);

	KStdAction::saveAs( this, SLOT( slotSaveFileAs() ), ac, "save" );
    KStdAction::preferences( this, SLOT( slotPreferences() ), ac, "preferences" );
	KStdAction::printPreview( this, SLOT( slotPrintPreview() ), ac );

    // attach the actions of the 2 children widgets too
	m_pageView->setupActions( ac );

	// apply configuration (both internal settings and GUI configured items)
    m_splitter->setSizes( Settings::splitterSizes() );
    slotNewConfig();

	// set our XML-UI resource file
	setXMLFile("kpdf_part.rc");
	updateActions();
}

Part::~Part()
{
    // save internal settings
    Settings::setSplitterSizes( m_splitter->sizes() );
    // write to disk config file
    Settings::writeConfig();

    delete document;
    if ( --m_count == 0 )
        delete globalParams;
}

void Part::goToPage(uint i)
{
	if (i <= document->pages())
		document->slotSetCurrentPage( i - 1 );
}

void Part::openDocument(KURL doc)
{
	openURL(doc);
}

uint Part::pages()
{
	return document->pages();
}

//this don't go anywhere but is required by genericfactory.h
KAboutData* Part::createAboutData()
{
	// the non-i18n name here must be the same as the directory in
	// which the part's rc file is installed ('partrcdir' in the
	// Makefile)
	KAboutData* aboutData = new KAboutData("kpdfpart", I18N_NOOP("KPDF::Part"), "0.1");
	aboutData->addAuthor("Wilco Greven", 0, "greven@kde.org");
	return aboutData;
}

bool Part::openFile()
{
	bool ok = document->openDocument( m_file );
	m_find->setEnabled( ok );
	return ok;
}

bool Part::openURL(const KURL &url)
{
	bool b = KParts::ReadOnlyPart::openURL(url);
	if (!b) KMessageBox::error(widget(), i18n("Could not open %1").arg(url.prettyURL()));
	return b;
}

bool Part::closeURL()
{
	document->closeDocument();
	return KParts::ReadOnlyPart::closeURL();
}

void Part::updateActions()
{
	if ( document->pages() > 0 )
	{
		m_gotoPage->setEnabled(document->pages()>1);
		m_firstPage->setEnabled(!document->atBegin());
		m_prevPage->setEnabled(!document->atBegin());
		m_lastPage->setEnabled(!document->atEnd());
		m_nextPage->setEnabled(!document->atEnd());
	}
	else
	{
		m_gotoPage->setEnabled(false);
		m_firstPage->setEnabled(false);
		m_lastPage->setEnabled(false);
		m_prevPage->setEnabled(false);
		m_nextPage->setEnabled(false);
	}
}

void Part::enableTOC(bool enable)
{
	m_toolBox->setItemEnabled(0, enable);
}

//BEGIN go to page dialog
class KPDFGotoPageDialog : public KDialogBase
{
public:
	KPDFGotoPageDialog(QWidget *p, int current, int max) : KDialogBase(p, 0L, true, i18n("Go to Page"), Ok | Cancel, Ok) {
		QWidget *w = new QWidget(this);
		setMainWidget(w);

		QVBoxLayout *topLayout = new QVBoxLayout( w, 0, spacingHint() );
		e1 = new KIntNumInput(current, w);
		e1->setRange(1, max);
		e1->setEditFocus(true);

		QLabel *label = new QLabel( e1,i18n("&Page:"), w );
		topLayout->addWidget(label);
		topLayout->addWidget(e1);
		topLayout->addSpacing(spacingHint()); // A little bit extra space
		topLayout->addStretch(10);
		e1->setFocus();
	}

	int getPage() {
		return e1->value();
	}

  protected:
    KIntNumInput *e1;
};
//END go to page dialog

void Part::slotGoToPage()
{
	KPDFGotoPageDialog pageDialog( m_pageView, document->currentPage() + 1, document->pages() );
	if ( pageDialog.exec() == QDialog::Accepted )
		document->slotSetCurrentPage( pageDialog.getPage() - 1 );
}

void Part::slotPreviousPage()
{
	if ( !document->atBegin() )
		document->slotSetCurrentPage( document->currentPage() - 1 );
}

void Part::slotNextPage()
{
	if ( !document->atEnd() )
		document->slotSetCurrentPage( document->currentPage() + 1 );
}

void Part::slotGotoFirst()
{
	document->slotSetCurrentPage( 0 );
}

void Part::slotGotoLast()
{
	document->slotSetCurrentPage( document->pages() - 1 );
}

void Part::slotFind()
{
	KFindDialog dlg( widget() );
	dlg.setHasCursor(false);
#if KDE_IS_VERSION(3,3,90)
	dlg.setSupportsBackwardsFind(false);
	dlg.setSupportsWholeWordsFind(false);
	dlg.setSupportsRegularExpressionFind(false);
#endif
	if (dlg.exec() == QDialog::Accepted)
	{
		m_findNext->setEnabled( true );
		document->slotFind( dlg.pattern(), dlg.options() & KFindDialog::CaseSensitive );
	}
}

void Part::slotFindNext()
{
	document->slotFind();
}

void Part::slotSaveFileAs()
{
    KURL saveURL = KFileDialog::getSaveURL( url().isLocalFile() ? url().url() : url().fileName(), QString::null, widget() );
    if ( saveURL.isValid() && !saveURL.isEmpty() )
    {
        if ( !KIO::NetAccess::file_copy( url(), saveURL, -1, true ) )
            KMessageBox::information( 0, i18n("File could not be saved in '%1'. Try to save it to another location.").arg( saveURL.prettyURL() ) );
    }
}

void Part::slotPreferences()
{
    // an instance the dialog could be already created and could be cached,
    // in which case you want to display the cached dialog
    if ( PreferencesDialog::showDialog( "preferences" ) )
        return;

    // we didn't find an instance of this dialog, so lets create it
    PreferencesDialog * dialog = new PreferencesDialog( m_pageView, Settings::self() );
    // keep us informed when the user changes settings
    connect( dialog, SIGNAL( settingsChanged() ), this, SLOT( slotNewConfig() ) );

    dialog->show();
}

void Part::slotNewConfig()
{
    // Apply settings here. A good policy is to check wether the setting has
    // changed before applying changes.

    // Left Panel and search Widget
    bool showLeft = Settings::showLeftPanel();
    if ( m_toolBox->isShown() != showLeft )
    {
        // show/hide left qtoolbox
        m_toolBox->setShown( showLeft );
        // this needs to be hidden explicitly to disable thumbnails gen
        m_thumbnailList->setShown( showLeft );
    }

    bool showSearch = Settings::showSearchBar();
    if ( m_searchWidget->isShown() != showSearch )
        m_searchWidget->setShown( showSearch );

    // Main View
    QScrollView::ScrollBarMode scrollBarMode = Settings::showScrollBars() ?
        QScrollView::AlwaysOn : QScrollView::AlwaysOff;
    if ( m_pageView->hScrollBarMode() != scrollBarMode )
    {
        m_pageView->setHScrollBarMode( scrollBarMode );
        m_pageView->setVScrollBarMode( scrollBarMode );
    }
}

void Part::slotPrintPreview()
{
    if (document->pages() == 0) return;

    double width, height;
    int landscape, portrait;
    KPrinter printer;
    const KPDFPage *page;

    printer.setMinMax(1, document->pages());
    printer.setPreviewOnly( true );
    printer.setMargins(0, 0, 0, 0);

    // if some pages are landscape and others are not the most common win as kprinter does
    // not accept a per page setting
    landscape = 0;
    portrait = 0;
    for (uint i = 0; i < document->pages(); i++)
    {
        page = document->page(i);
        width = page->width();
        height = page->height();
        if (page->rotation() == 90 || page->rotation() == 270) qSwap(width, height);
        if (width > height) landscape++;
        else portrait++;
    }
    if (landscape > portrait) printer.setOption("orientation-requested", "4");

    doPrint(printer);
}

void Part::slotPrint()
{
    if (document->pages() == 0) return;

    double width, height;
    int landscape, portrait;
    KPrinter printer;
    const KPDFPage *page;

    printer.setPageSelection(KPrinter::ApplicationSide);
    printer.setMinMax(1, document->pages());
    printer.setCurrentPage(document->currentPage()+1);
    printer.setMargins(0, 0, 0, 0);

    // if some pages are landscape and others are not the most common win as kprinter does
    // not accept a per page setting
    landscape = 0;
    portrait = 0;
    for (uint i = 0; i < document->pages(); i++)
    {
        page = document->page(i);
        width = page->width();
        height = page->height();
        if (page->rotation() == 90 || page->rotation() == 270) qSwap(width, height);
        if (width > height) landscape++;
        else portrait++;
    }
    if (landscape > portrait) printer.setOrientation(KPrinter::Landscape);

    if (printer.setup(widget())) doPrint( printer );
}

void Part::doPrint(KPrinter &printer)
{
    if (!document->okToPrint())
    {
        KMessageBox::error(widget(), i18n("Printing this document is not allowed."));
        return;
    }

    if (!document->print(printer))
    {
        KMessageBox::error(widget(), i18n("Could not print the document. Please report to bugs.kde.org"));	
    }
}

void Part::restoreDocument(const KURL &url, int page)
{
  if (openURL(url)) goToPage(page);
}

void Part::saveDocumentRestoreInfo(KConfig* config)
{
  config->writePathEntry( "URL", url().url() );
  if (document->pages() > 0) config->writeEntry( "Page", document->currentPage() + 1);
}

/*
* BrowserExtension class
*/
BrowserExtension::BrowserExtension(Part* parent)
  : KParts::BrowserExtension( parent, "KPDF::BrowserExtension" )
{
	emit enableAction("print", true);
	setURLDropHandlingEnabled(true);
}

void BrowserExtension::print()
{
	static_cast<Part*>(parent())->slotPrint();
}

#include "kpdf_part.moc"
