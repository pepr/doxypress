/*************************************************************************
 *
 * Copyright (C) 2014-2016 Barbara Geller & Ansel Sermersheim 
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

#include <QByteArray>
#include <QHash>
#include <QSet>

#include <stdio.h>
#include <stdlib.h>

#include <parser_clang.h>

#include <config.h>
#include <doxy_globals.h>
#include <entry.h>
#include <message.h>
#include <outputgen.h>
#include <qfileinfo.h>
#include <stringmap.h>
#include <tooltip.h>
#include <util.h>

static QSet<QString>              s_includedFiles;
static QSharedPointer<Definition> s_currentDefinition;
static QSharedPointer<MemberDef>  s_currentMemberDef;

static uint g_currentLine    = 0;
static uint g_bracketCount   = 0;
static bool g_searchForBody  = false;
static bool g_insideBody     = false;

static void writeLineNumber(CodeOutputInterface &ol, QSharedPointer<FileDef> fd, uint line);

class ClangParser::Private
{
 public:
   enum DetectedLang { Detected_Cpp, Detected_ObjC, Detected_ObjCpp };

   Private() 
      : tu(0), tokens(0), numTokens(0), cursors(0), ufs(0), sources(0), numFiles(0), 
        fileMapping(), detectedLang(Detected_Cpp) 
   {      
   }

   int getCurrentTokenLine();

   QString fileName;
   QByteArray *sources;

   uint numFiles;
   uint numTokens;
   uint curLine;
   uint curToken;    

   CXIndex            index;
   CXTranslationUnit  tu; 
   CXToken           *tokens;   
   CXCursor          *cursors;  
   CXUnsavedFile     *ufs;      

   QHash<QString, uint> fileMapping;
   DetectedLang detectedLang;
};

int ClangParser::Private::getCurrentTokenLine()
{
   if (numTokens == 0) {
      return 1;
   }

   // guard against filters that reduce the number of lines
   uint retval;
   uint c;

   if (curToken >= numTokens) {
      curToken = numTokens - 1;
   }

   CXSourceLocation start = clang_getTokenLocation(tu, tokens[curToken]);
   clang_getSpellingLocation(start, 0, &retval, &c, 0);

   return retval;
}

ClangParser *ClangParser::instance()
{
   static ClangParser m_instance; 
   return &m_instance;
}

ClangParser::ClangParser()
{
   p = new Private;
}

ClangParser::~ClangParser()
{
   delete p;
}

static void codifyLines(CodeOutputInterface &ol, QSharedPointer<FileDef> fd, const QString &text,
                        uint &line, uint &column, const QString &fontClass)
{
   if (! fontClass.isEmpty()) {
      ol.startFontClass(fontClass);
   }

   const QChar *p   = text.constData();
   const QChar *sp  = p;
   const QChar *ptr = p;

   QChar c;

   bool done = false;

   while (! done) {
      sp = p;

      while ((c = *p++) != 0 && c != '\n') {
         column++;
      }

      if (c == '\n') {
         line++;
         int l = (int)(p - sp - 1);
         column = l + 1;

         char *tmp = (char *)malloc(l + 1);
         memcpy(tmp, sp, l);

         tmp[l] = '\0';
         ol.codify(tmp);
         free(tmp);

         if (! fontClass.isEmpty()) {
            ol.endFontClass();
         }

         ol.endCodeLine();
         ol.startCodeLine(true);
         writeLineNumber(ol, fd, line);

         if (! fontClass.isEmpty()) {
            ol.startFontClass(fontClass.toUtf8());
         }

      } else {
         ol.codify(text.mid(sp - ptr) );
         done = true;
      }
   }

  if (! fontClass.isEmpty()) {
      ol.endFontClass();
   }
}

static QString detab(const QString &str)
{
   static int tabSize = Config::getInt("tab-size");

   QString out;      
   int col = 0;

   const int maxIndent = 1000000;    // value representing infinity
   int minIndent = maxIndent;

   for (auto c : str) {

      switch (c.unicode()) {

         case '\t': {
            // expand tab
            int stop = tabSize - (col % tabSize);           
            col += stop;

            while (stop--) {
               out += ' ';
            }
         }
         break;

         case '\n': 
            // reset colomn counter
            out += c;
            col = 0;
            break;

         case ' ': 
            // increment column counter
            out += c;
            col++;
            break;

         default: 
            // non-whitespace => update minIndent
            out += c;
            
            if (col < minIndent) {
               minIndent = col;
            }
            col++;
      }
   }
  
   return out;
}

static void detectFunctionBody(const char *s)
{
  if (g_searchForBody && (s == ":" || s == "{")) { 
      // start of 'body' (: is for constructor)
      g_searchForBody = false;
      g_insideBody    = true;

   } else if (g_searchForBody && s == ";") { 
      // declaration only
      g_searchForBody = false;
      g_insideBody    = false;
   }

   if (g_insideBody && s == "{") { 
      // increase scoping level
      g_bracketCount++;
   }

   if (g_insideBody && s == "}") { 
      // decrease scoping level
      g_bracketCount--;

      if (g_bracketCount <= 0) { 
         // got outside of function body
         g_insideBody   = false;
         g_bracketCount = 0;
      }
   }
}

static QString getCursorKindName(CXCursor cursor)
{ 
   CXCursorKind cursorKind = clang_getCursorKind(cursor);
   CXString text  = clang_getCursorKindSpelling(cursorKind);

   QString retval = clang_getCString(text);

   clang_disposeString(text); 

   return retval;
}

static QString getCursorSpelling(CXCursor cursor)
{   
   CXString text  = clang_getCursorSpelling(cursor);
   QString retval = clang_getCString(text);

   clang_disposeString(text); 

   return retval;
}

static QString getFileName(CXFile file)
{ 
   CXString text  = clang_getFileName(file); 
   QString retval = clang_getCString(text);

   clang_disposeString(text);

   return retval;
}

static QString keywordToType(const QString &key)
{   
   static bool init = true;
   static QMap<QString, QString> keyWords;
   
   if (init) {
      keyWords.insert("break",    "keywordflow");
      keyWords.insert("case",     "keywordflow");
      keyWords.insert("catch",    "keywordflow");
      keyWords.insert("continue", "keywordflow");
      keyWords.insert("default",  "keywordflow");
      keyWords.insert("do",       "keywordflow");
      keyWords.insert("else",     "keywordflow");
      keyWords.insert("finally",  "keywordflow");
      keyWords.insert("for",      "keywordflow");
      keyWords.insert("foreach",  "keywordflow");
      keyWords.insert("for each", "keywordflow");
      keyWords.insert("goto",     "keywordflow");
      keyWords.insert("if",       "keywordflow");
      keyWords.insert("return",   "keywordflow");
      keyWords.insert("switch",   "keywordflow");
      keyWords.insert("throw",    "keywordflow");
      keyWords.insert("throws",   "keywordflow");
      keyWords.insert("try",      "keywordflow");
      keyWords.insert("while",    "keywordflow");
      keyWords.insert("@try",     "keywordflow");
      keyWords.insert("@catch",   "keywordflow");
      keyWords.insert("@finally", "keywordflow");

      keyWords.insert("bool",     "keywordtype");
      keyWords.insert("char",     "keywordtype");
      keyWords.insert("double",   "keywordtype");
      keyWords.insert("float",    "keywordtype");
      keyWords.insert("int",      "keywordtype");
      keyWords.insert("long",     "keywordtype");
      keyWords.insert("object",   "keywordtype");
      keyWords.insert("short",    "keywordtype");
      keyWords.insert("signed",   "keywordtype");
      keyWords.insert("unsigned", "keywordtype");
      keyWords.insert("void",     "keywordtype");
      keyWords.insert("wchar_t",  "keywordtype");
      keyWords.insert("size_t",   "keywordtype");
      keyWords.insert("boolean",  "keywordtype");
      keyWords.insert("id",       "keywordtype");
      keyWords.insert("SEL",      "keywordtype");
      keyWords.insert("string",   "keywordtype");
      keyWords.insert("nullptr",  "keywordtype");

      init = false;
   }

   auto iter = keyWords.find(key);

   if (iter != keyWords.end()) {
      return iter.value();
   }
 
   return "keyword";
}

static void writeLineNumber(CodeOutputInterface &ol, QSharedPointer<FileDef> fd, uint line)
{
   QSharedPointer<Definition> d;  

   if (fd) {
      d = fd->getSourceDefinition(line);
   }
  
   if (d && d->isLinkable()) {
      s_currentDefinition = d;
      g_currentLine = line;

      QSharedPointer<MemberDef> md = fd->getSourceMember(line);

      if (md && md->isLinkable()) { 
         // link to member

         if (s_currentMemberDef != md) { 
            // new member, start search for body

            g_searchForBody = true;
            g_insideBody    = false;
            g_bracketCount  = 0;
         }

         s_currentMemberDef = md;
         ol.writeLineNumber(md->getReference(), md->getOutputFileBase(), md->anchor(), line);

      } else { 
         // link to compound
         s_currentMemberDef = QSharedPointer<MemberDef>();
         ol.writeLineNumber(d->getReference(), d->getOutputFileBase(), d->anchor(), line);
      }

   } else { 
      // no link
      ol.writeLineNumber(0, 0, 0, line);
   }

   // set search page target
   if (Doxy_Globals::searchIndex) {          
      QString lineAnchor = QString("l%1").arg(line, 5, 10, QChar('0'));

      ol.setCurrentDoc(fd, lineAnchor, true);
   }
}

static void writeMultiLineCodeLink(CodeOutputInterface &ol, QSharedPointer<FileDef> fd, uint &line, uint &column, 
                  QSharedPointer<Definition> d, const QString &text)
{
   static bool sourceTooltips = Config::getBool("source-tooltips");
   TooltipManager::instance()->addTooltip(d);

   QString ref    = d->getReference();
   QString file   = d->getOutputFileBase();
   QString anchor = d->anchor();

   QString tooltip;

   if (! sourceTooltips) { 
      // fall back to simple "title" tooltips
      tooltip = d->briefDescriptionAsTooltip();
   }

   QString tmp;

   for (auto c : text) { 

      if (c == '\n') {
         line++;         

         ol.writeCodeLink(ref, file, anchor, tmp, tooltip);
         ol.endCodeLine();

         ol.startCodeLine(true);
         writeLineNumber(ol, fd, line);

         tmp = "";

      } else {
         column++;
         tmp += c;
                
      }
   }

   if (! tmp.isEmpty() )  {
      ol.writeCodeLink(ref, file, anchor, tmp, tooltip); 
   }
}

// call back, called for each include in a translation unit
static void inclusionVisitor(CXFile file, CXSourceLocation *, uint, CXClientData clientData)
{  
   s_includedFiles.insert(getFileName(file));   
}

// call back, called for each cursor node
static CXChildVisitResult visitor(CXCursor cursor, CXCursor parentCursor, CXClientData clientData)
{
   CXSourceLocation location = clang_getCursorLocation(cursor); 
   if (clang_Location_isFromMainFile(location) == 0) {
      return CXChildVisit_Continue;
   }   

   CXCursorKind kind = clang_getCursorKind(cursor);

  // if (kind == CXCursorKind::CXCursor_FunctionDecl || kind == CXCursorKind::CXCursor_CXXMethod || 
  //                kind == CXCursorKind::CXCursor_FunctionTemplate) {

         uint startLine   = 0;
         uint endLine     = 0;
         uint startColumn = 0;
         uint endColumn   = 0;

         CXSourceRange range            = clang_getCursorExtent(cursor);
         CXSourceLocation startLocation = clang_getRangeStart(range);
         CXSourceLocation endLocation   = clang_getRangeEnd(range);

         clang_getSpellingLocation(startLocation, nullptr, &startLine, &startColumn, nullptr); 
         clang_getSpellingLocation(endLocation,   nullptr, &endLine,   &endColumn,   nullptr); 

        
         CXString text1 = clang_getCursorDisplayName(cursor);
         QString  x1    = clang_getCString(text1);
         clang_disposeString(text1); 


         CXType type    = clang_getCursorResultType(cursor);
         CXString text2 = clang_getTypeSpelling(type);
         QString  x2    = clang_getCString(text2);         
         clang_disposeString(text2); 

  
         // parse A
         msg("--(A) kind = %-25s  n = %-15s  d = %-22s  type = %-10s  %d:%d \n", 
                  csPrintable(getCursorKindName(cursor)), csPrintable(getCursorSpelling(cursor)), csPrintable(x1), 
                  csPrintable(x2), startLine, startColumn);


         // comment B
         
         CXComment comment = clang_Cursor_getParsedComment(cursor);
         CXString  text3   = clang_FullComment_getAsHTML(comment);
         QString   x3      = clang_getCString(text3);      
         clang_disposeString(text3); 

         if (! x3.isEmpty()) {
            msg("\n--(B) comment = %s\n\n", csPrintable(x3) ) ; 
         }
//   }
 
   return CXChildVisit_Recurse;
}


// ** entry point
void ClangParser::start(const QString &fileName, QStringList &includeFiles, QSharedPointer<Entry> root)
{
   static QStringList includePath = Config::getList("include-path");    
   static QStringList clangFlags  = Config::getList("clang-flags");
   
   // exclude PCH files, disable diagnostics
   p->index    = clang_createIndex(false, false);

   p->fileName = fileName;
   p->curLine  = 1;
   p->curToken = 0;

   // reserve space for the command line options
   char **argv = (char **)malloc(sizeof(char *) * (4 + Doxy_Globals::inputPaths.count() + includePath.count() + clangFlags.count()));
   int argc    = 0;

   // add include paths for input files  
   for (auto item : Doxy_Globals::inputPaths) { 
      QString inc = "-I" + item;
      argv[argc++] = strdup(inc.toUtf8());  
   }

   // add external include paths
   for (uint i = 0; i < includePath.count(); i++) {  
      QString inc = "-I" + includePath.at(i);
      argv[argc++] = strdup(inc.toUtf8());  
   }

   // user specified options
   for (uint i = 0; i < clangFlags.count(); i++) {
      argv[argc++] = strdup(clangFlags.at(i).toUtf8());
   }

   // extra options
   argv[argc++] = strdup("-ferror-limit=0");
   argv[argc++] = strdup("-x");

   // if the file is a .h file it could contain C, C++, or Objective C
   // configure the parser before knowing this (?)

   // use the source file to detect the language. 
   // detection will fail if we pass .h files containing ObjC code and no source 

   SrcLangExt lang = getLanguageFromFileName(fileName);

   if (lang == SrcLangExt_ObjC || p->detectedLang != ClangParser::Private::Detected_Cpp) {
      QFileInfo fi(fileName);

      QString ext = fi.suffix().toLower();   

      if (p->detectedLang == ClangParser::Private::Detected_Cpp &&
            (ext == "cpp" || ext == "cxx" || ext == "cc" || ext == "c")) {

         // fall back to C/C++ once we see an extension that indicates C++
         p->detectedLang = ClangParser::Private::Detected_Cpp;

      } else if (ext == "mm") { 
         // switch to Objective C++
         p->detectedLang = ClangParser::Private::Detected_ObjCpp;

      } else if (ext == "m") { 
         // switch to Objective C
         p->detectedLang = ClangParser::Private::Detected_ObjC;
      }
   }

   switch (p->detectedLang) {
      case ClangParser::Private::Detected_Cpp:
         argv[argc++] = strdup("c++");
         break;

      case ClangParser::Private::Detected_ObjC:
         argv[argc++] = strdup("objective-c");
         break;

      case ClangParser::Private::Detected_ObjCpp:
         argv[argc++] = strdup("objective-c++");
         break;
   }

   // provide the input and their dependencies as unsaved files in memory
   static bool filterSourceFiles = Config::getBool("filter-source-files");

   argv[argc++] = strdup(fileName.toUtf8());
 
   uint numUnsavedFiles = includeFiles.count() + 1;

   p->numFiles = numUnsavedFiles;
   p->sources  = new QByteArray[numUnsavedFiles];
   p->ufs      = new CXUnsavedFile[numUnsavedFiles];

   p->sources[0]      = detab(fileToString(fileName, filterSourceFiles, true)).toUtf8();
   p->ufs[0].Filename = strdup(fileName.toUtf8());
   p->ufs[0].Contents = p->sources[0].constData();
   p->ufs[0].Length   = p->sources[0].length();

   //  
   uint i = 1;
   for (auto item : includeFiles) {       

      p->fileMapping.insert(item, i);

      p->sources[i]      = detab(fileToString(item, filterSourceFiles, true)).toUtf8();
      p->ufs[i].Filename = strdup(item.toUtf8());
      p->ufs[i].Contents = p->sources[i].constData();
      p->ufs[i].Length   = p->sources[i].length();

      i++;
   }

   // data structure, source filename (not needed, in argv), cmd line args, num of cmd line args
   // pass unsaved files(?), num of unsaved files, options flag, where to put the trans unit   

   // CXTranslationUnit_DetailedPreprocessingRecord | CXTranslationUnit_SkipFunctionBodies

   CXErrorCode errorCode = clang_parseTranslationUnit2(p->index, 0, argv, argc, p->ufs, numUnsavedFiles, 
                  CXTranslationUnit_DetailedPreprocessingRecord, &(p->tu) );

   // free memory
   for (int i = 0; i < argc; ++i) {
      free(argv[i]);
   }
   free(argv);

   if (p->tu && errorCode == CXError_Success) {
      // filter out any includes not found by the clang parser
      determineInputFiles(includeFiles);

      // show any warnings the compiler produced
      uint diagCnt = clang_getNumDiagnostics(p->tu);

      for (uint i = 0; i != diagCnt; i++) {
         CXDiagnostic diag = clang_getDiagnostic(p->tu, i);
         CXString diagMsg  = clang_formatDiagnostic(diag, clang_defaultDiagnosticDisplayOptions());

         err("%s\n", clang_getCString(diagMsg));

         clang_disposeDiagnostic(diag);
         clang_disposeString(diagMsg);
      }

      if (diagCnt > 0) {
         msg("\n");
      }

      // BROOM Test - walk the tree
      CXCursor rootCursor = clang_getTranslationUnitCursor(p->tu);       
      clang_visitChildren(rootCursor, visitor, nullptr);


      // create a source range for the given file
      QFileInfo fi(fileName);
      CXFile f = clang_getFile(p->tu, fileName.toUtf8());

      CXSourceLocation fileBegin = clang_getLocationForOffset(p->tu, f, 0);
      CXSourceLocation fileEnd   = clang_getLocationForOffset(p->tu, f, p->ufs[0].Length);
      CXSourceRange    fileRange = clang_getRange(fileBegin, fileEnd);

      // produce tokens for this file
      clang_tokenize(p->tu, fileRange, &p->tokens, &p->numTokens);

      // produce cursors for each token
      p->cursors = new CXCursor[p->numTokens];
      clang_annotateTokens(p->tu, p->tokens, p->numTokens, p->cursors);

      // BROOM - look at the list of tokens
      for (int index = 0; index < p->numTokens; index++)  {
         
         CXString text = clang_getTokenSpelling(p->tu, p->tokens[index]);
         QString  x    = clang_getCString(text);         
         clang_disposeString(text); 

         printf("\n (Tokens) #%d  Token Text = %s", index, csPrintable(x) );
      }

      printf("\n");

   } else {
      p->tokens    = 0;
      p->numTokens = 0;
      p->cursors   = 0;

      err("Clang failed to parse file %s\n", csPrintable(fileName));
   }
}

void ClangParser::finish()
{
   if (p->tu) {

      delete[] p->cursors;

      clang_disposeTokens(p->tu, p->tokens, p->numTokens);
      clang_disposeTranslationUnit(p->tu);
      clang_disposeIndex(p->index);

      p->fileMapping.clear();
      p->tokens    = 0;
      p->numTokens = 0;
      p->cursors   = 0;
   }

   for (uint i = 0; i < p->numFiles; i++) {
      free((void *)p->ufs[i].Filename);
   }

   delete[] p->ufs;
   delete[] p->sources;

   p->ufs       = 0;
   p->sources   = 0;
   p->numFiles  = 0;
   p->tu        = 0;
}

// filter the files keeping those which where found as include files within the TU
// files - list of files to filter
void ClangParser::determineInputFiles(QStringList &files)
{
   // save included files used by the translation unit to a container  
   clang_getInclusions(p->tu, inclusionVisitor, nullptr);

   // create a new filtered file list
   QStringList resultIncludes;
 
   for (auto item : files) {
      if (s_includedFiles.contains(item)) {
         resultIncludes.append(item);
      }
   }

   // replace the original list
   files = resultIncludes;
}

QString ClangParser::lookup(uint line, const QString &symbol)
{
   QString retval;

   if (symbol.isEmpty()) {
      return retval;
   }

   int symLen      = symbol.length();
   uint tokenLine  = p->getCurrentTokenLine();

   while (tokenLine >= line && p->curToken > 0) {

      if (tokenLine == line) { 
         // already at the right line

         p->curToken--;                   // linear search to start of the line
         tokenLine = p->getCurrentTokenLine();

      } else {
         p->curToken /= 2;                // binary search backward
         tokenLine = p->getCurrentTokenLine();
      }
   }

   bool found = false;

   while (tokenLine <= line && p->curToken < p->numTokens && !found) {
      CXString tokenString = clang_getTokenSpelling(p->tu, p->tokens[p->curToken]);
     
      const char *ts = clang_getCString(tokenString);
      int tl         = strlen(ts);
      int startIndex = p->curToken;

      if (tokenLine == line && strncmp(ts, symbol.toUtf8(), tl) == 0) { 
         // found partial match at the correct line
         int offset = tl;

         while (offset < symLen) { 
            // symbol spans multiple tokens

            p->curToken++;

            if (p->curToken >= p->numTokens) {
               break; // end of token stream
            }

            tokenLine = p->getCurrentTokenLine();
            clang_disposeString(tokenString);

            tokenString = clang_getTokenSpelling(p->tu, p->tokens[p->curToken]);
            ts = clang_getCString(tokenString);
            tl = ts ? strlen(ts) : 0;

            // skip over any spaces in the symbol
            QChar c;

            while (offset < symLen && ((c = symbol[offset]) == ' ' || c == '\t' || c == '\r' || c == '\n')) {
               offset++;
            }

            if (strncmp(ts, symbol.mid(offset).toUtf8(), tl) != 0) { 
               // next token matches?

               break; // no match
            }

            offset += tl;
         }

         if (offset == symLen) { 
            // symbol matches the token(s)
            CXCursor c   = p->cursors[p->curToken];
            CXString usr = clang_getCursorUSR(c);
           
            retval = clang_getCString(usr);
            clang_disposeString(usr);
            found = true;

         } else { 
            // reset token cursor to start of the search
            p->curToken = startIndex;
         }
      }

      clang_disposeString(tokenString);
      p->curToken++;

      if (p->curToken < p->numTokens) {
         tokenLine = p->getCurrentTokenLine();
      }
   }
  
   return retval;
}

void ClangParser::linkInclude(CodeOutputInterface &ol, QSharedPointer<FileDef> fd, uint &line, uint &column, const QString &text)
{
   QString incName = text;
   incName = incName.mid(1, incName.length() - 2); // strip ".." or  <..>

   QSharedPointer<FileDef> ifd;

   if (! incName.isEmpty()) {     
      QSharedPointer<FileNameList> fn = Doxy_Globals::inputNameDict->find(incName);

      if (fn) {
         bool found = false;

         // for each include name, see if this source file actually includes the file
         for (auto fd : *fn) {   
                      
            if (found) {
               break;
            }                     

            ifd = fd;
            found = fd->isIncluded(ifd->getFilePath());
         }
      }

   }

   if (ifd) {
      ol.writeCodeLink(ifd->getReference(), ifd->getOutputFileBase(), 0, text, ifd->briefDescriptionAsTooltip());

   } else {
      codifyLines(ol, ifd, text, line, column, "preprocessor");
   }
}

void ClangParser::linkMacro(CodeOutputInterface &ol, QSharedPointer<FileDef> fd, uint &line, uint &column, const QString &text)
{
   QSharedPointer<MemberName> mn = Doxy_Globals::functionNameSDict->find(text);
  
   if (mn) {
      for (auto md : *mn) {
         if (md->isDefine()) {
            writeMultiLineCodeLink(ol, fd, line, column, md, text);
            return;
         }
          
      }
   }
   codifyLines(ol, fd, text, line, column, "");
}


void ClangParser::linkIdentifier(CodeOutputInterface &ol, QSharedPointer<FileDef> fd,
                                 uint &line, uint &column, const QString &text, int tokenIndex)
{
   CXCursor c = p->cursors[tokenIndex];
   CXCursor r = clang_getCursorReferenced(c);

   if (! clang_equalCursors(r, c)) {
      // link to referenced location
      c = r; 
   }

   CXCursor t = clang_getSpecializedCursorTemplate(c);
  
   if (! clang_Cursor_isNull(t) && !clang_equalCursors(t, c)) {
      // link to template
      c = t; 
   }

   CXString usr = clang_getCursorUSR(c);
   const char *usrStr = clang_getCString(usr);

   QSharedPointer<Definition> d;

   if (usrStr) {
      d = Doxy_Globals::clangUsrMap.value(usrStr);
   }
   
   if (d && d->isLinkable()) {
      if (g_insideBody && s_currentMemberDef && d->definitionType() == Definition::TypeMember &&
            (s_currentMemberDef != d || g_currentLine < line)) { 

         // avoid self-reference
         addDocCrossReference(s_currentMemberDef, d.dynamicCast<MemberDef>());
      }

      writeMultiLineCodeLink(ol, fd, line, column, d, text);

   } else {
      codifyLines(ol, fd, text, line, column, "");
   }

   clang_disposeString(usr);
}

void ClangParser::switchToFile(const QString &fileName)
{
   if (p->tu) {
      delete[] p->cursors;

      clang_disposeTokens(p->tu, p->tokens, p->numTokens);

      p->tokens    = 0;
      p->numTokens = 0;
      p->cursors   = 0;

      QFileInfo fi(fileName);
      CXFile f = clang_getFile(p->tu, fileName.toUtf8());

      uint pIndex = p->fileMapping.value(fileName);

      if (pIndex < p->numFiles) {
         uint i = pIndex;

         CXSourceLocation fileBegin = clang_getLocationForOffset(p->tu, f, 0);
         CXSourceLocation fileEnd   = clang_getLocationForOffset(p->tu, f, p->ufs[i].Length);
         CXSourceRange    fileRange = clang_getRange(fileBegin, fileEnd);

         clang_tokenize(p->tu, fileRange, &p->tokens, &p->numTokens);

         p->cursors = new CXCursor[p->numTokens];
         clang_annotateTokens(p->tu, p->tokens, p->numTokens, p->cursors);

         p->curLine  = 1;
         p->curToken = 0;

      } else {
         err("Clang failed to find input file %s\n", csPrintable(fileName));

      }
   }
}

void ClangParser::writeSources(CodeOutputInterface &ol, QSharedPointer<FileDef> fd)
{
   TooltipManager::instance()->clearTooltips();

   // set global parser state
   s_currentDefinition = QSharedPointer<Definition>();
   s_currentMemberDef  = QSharedPointer<MemberDef>();
   g_currentLine       = 0;
   g_searchForBody     = false;
   g_insideBody        = false;
   g_bracketCount      = 0;

   uint line   = 1;
   uint column = 1;

   QString lineNumber;
   QString lineAnchor;
   
   ol.startCodeLine(true);
   writeLineNumber(ol, fd, line);

   for (uint i = 0; i < p->numTokens; i++) {
      CXSourceLocation start = clang_getTokenLocation(p->tu, p->tokens[i]);

      uint t_line;
      uint t_col;

      clang_getSpellingLocation(start, 0, &t_line, &t_col, 0);

      if (t_line > line) {
         column = 1;
      }

      while (line < t_line) {
         line++;
         ol.endCodeLine();
         ol.startCodeLine(true);
         writeLineNumber(ol, fd, line);
      }

      while (column < t_col) {
         ol.codify(" ");
         column++;
      }

      CXString tokenString = clang_getTokenSpelling(p->tu, p->tokens[i]);
      char const *s = clang_getCString(tokenString);

      CXCursorKind cursorKind  = clang_getCursorKind(p->cursors[i]);
      CXTokenKind tokenKind    = clang_getTokenKind(p->tokens[i]);

      switch (tokenKind) {
         case CXToken_Keyword:
            if (strcmp(s, "operator") == 0) {
               linkIdentifier(ol, fd, line, column, s, i);

            } else {
               QString temp;

               if (cursorKind == CXCursor_PreprocessingDirective) {
                  temp = "preprocessor";
 
               } else {
                  temp = keywordToType(s);

               }

               codifyLines(ol, fd, s, line, column, temp);
            }
            break;

         case CXToken_Literal:
            if (cursorKind == CXCursor_InclusionDirective) {
               linkInclude(ol, fd, line, column, s);

            } else if (s[0] == '"' || s[0] == '\'') {
               codifyLines(ol, fd, s, line, column, "stringliteral");

            } else {
               codifyLines(ol, fd, s, line, column, "");

            }
            break;

         case CXToken_Comment:
            codifyLines(ol, fd, s, line, column, "comment");
            break;

         default:  
            // CXToken_Punctuation or CXToken_Identifier

            if (tokenKind == CXToken_Punctuation) {
               detectFunctionBody(s);
            }

            switch (cursorKind) {
               case CXCursor_PreprocessingDirective:
                  codifyLines(ol, fd, s, line, column, "preprocessor");
                  break;

               case CXCursor_MacroDefinition:
                  codifyLines(ol, fd, s, line, column, "preprocessor");
                  break;

               case CXCursor_InclusionDirective:
                  linkInclude(ol, fd, line, column, s);
                  break;

               case CXCursor_MacroExpansion:
                  linkMacro(ol, fd, line, column, s);
                  break;

               default:
                  if (tokenKind == CXToken_Identifier || (tokenKind == CXToken_Punctuation && 
                         (cursorKind == CXCursor_DeclRefExpr || cursorKind == CXCursor_MemberRefExpr ||
                          cursorKind == CXCursor_CallExpr || cursorKind == CXCursor_ObjCMessageExpr)) ) {

                     linkIdentifier(ol, fd, line, column, s, i);

                     if (Doxy_Globals::searchIndex) {
                        ol.addWord(s, false);
                     }

                  } else {
                     codifyLines(ol, fd, s, line, column, "");
                  }

                  break;
            }
      }

      clang_disposeString(tokenString);
   }

   ol.endCodeLine();
   TooltipManager::instance()->writeTooltips(ol);
}

