/*************************************************************************
 *
 * Copyright (C) 2014-2015 Barbara Geller & Ansel Sermersheim 
 * Copyright (C) 1997-2014 by Dimitri van Heesch.
 * All rights reserved.    
 *
 * Permission to use, copy, modify, and distribute this software and its
 * documentation under the terms of the GNU General Public License version 2
 * is hereby granted. No representations are made about the suitability of
 * this software for any purpose. It is provided "as is" without express or
 * implied warranty. See the GNU General Public License for more details.
 *
 * Documents produced by DoxyPress are derivative works derived from the
 * input used in their production; they are not affected by this license.
 *
*************************************************************************/

#ifndef SECTION_H
#define SECTION_H

class Definition;

/** Class representing a section in a page */
struct SectionInfo {

   enum SectionType { Page          = 0,
                      Section       = 1,
                      Subsection    = 2,
                      Subsubsection = 3,
                      Paragraph     = 4,
                      Anchor        = 5,
                      Table         = 6
                    };

   SectionInfo(const QString &f, const int line, const QString &anchor, const QString &t, SectionType secT, 
      int lev, const QString &r = QString()) 
      : label(anchor), title(t), type(secT), ref(r), fileName(f), lineNr(line), generated(false), level(lev), dupAnchor_cnt(0) 
   { }  

   QString label;
   QString title;   
   QString ref;
   QString fileName;
      
   int lineNr;   
   int level;

   bool generated;

   SectionType type;
   QSharedPointer<Definition> definition;

   int dupAnchor_cnt;
   QString dupAnchor_fName;
};

#endif
