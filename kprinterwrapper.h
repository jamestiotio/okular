// -*- Mode: C++; c-basic-offset: 2; indent-tabs-mode: nil; -*-
// kprinterwrapper.h
//
// Part of KDVI - A DVI previewer for the KDE desktop environment
//
// (C) 2003 Stefan Kebekus
// Distributed under the GPL


#ifndef _PRINTERWRAPPER_H
#define _PRINTERWRAPPER_H

#include "kprinter.h"


class KDVIPrinterWrapper : public KPrinter
{
public:
  KDVIPrinterWrapper() : KPrinter(true, QPrinter::ScreenResolution) {; };

  void doPreparePrinting() { preparePrinting(); };
};

#endif
