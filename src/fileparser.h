/*************************************************************************
 *
 * Copyright (C) 1997-2014 by Dimitri van Heesch.
 * Copyright (C) 2014-2015 Barbara Geller & Ansel Sermersheim 
 * All rights reserved.    
 *
 * Permission to use, copy, modify, and distribute this software and its
 * documentation under the terms of the GNU General Public License version 2
 * is hereby granted. No representations are made about the suitability of
 * this software for any purpose. It is provided "as is" without express or
 * implied warranty. See the GNU General Public License for more details.
 *
 * Documents produced by Doxygen are derivative works derived from the
 * input used in their production; they are not affected by this license.
 *
*************************************************************************/

#ifndef FILEPARSER_H
#define FILEPARSER_H

#include <parserintf.h>

/** @brief General file parser */
class FileParser : public ParserInterface
{
 public:
   virtual ~FileParser() {}
   void startTranslationUnit(const char *) {}
   void finishTranslationUnit() {}
   void parseInput(const char *, const char *, Entry *, bool, QStringList &) {}

   bool needsPreprocessing(const QByteArray &) {
      return false;
   }

   void parseCode(CodeOutputInterface &codeOutIntf,
                  const char *scopeName,
                  const QByteArray &input,
                  SrcLangExt lang,
                  bool isExampleBlock,
                  const char *exampleName = 0,
                  FileDef *fileDef = 0,
                  int startLine = -1,
                  int endLine = -1,
                  bool inlineFragment = false,
                  MemberDef *memberDef = 0,
                  bool showLineNumbers = true,
                  Definition *searchCtx = 0,
                  bool collectXRefs = true
                 );
   void resetCodeParserState() {}
   void parsePrototype(const char *) {}
};


#endif
