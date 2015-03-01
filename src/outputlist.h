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
 * Documents produced by DoxyPress are derivative works derived from the
 * input used in their production; they are not affected by this license.
 *
*************************************************************************/

#ifndef OUTPUTLIST_H
#define OUTPUTLIST_H

#include <QList>

#include <index.h>
#include <outputgen.h>
#include <section.h>

class ClassDiagram;
class DotClassGraph;
class DotDirDeps;
class DotInclDepGraph;
class DotGfxHierarchyTable;
class DotGroupCollaboration;
class DocRoot;

/** Class representing a list of output generators that are written to
 *  in parallel.
 */
class OutputList : public OutputDocInterface
{
 public:
   OutputList(bool);
   virtual ~OutputList();

   void add(OutputGenerator *);

   uint count() const {
      return m_outputs.count();
   }

   void disableAllBut(OutputGenerator::OutputType o);
   void enableAll();
   void disableAll();
   void disable(OutputGenerator::OutputType o);
   void enable(OutputGenerator::OutputType o);
   bool isEnabled(OutputGenerator::OutputType o);
   void pushGeneratorState();
   void popGeneratorState();


   //  OutputDocInterface implementation
   bool generateDoc(const char *fileName, int startLine, QSharedPointer<Definition> ctx, QSharedPointer<MemberDef> md, 
                    const QByteArray &docStr,bool indexWords, bool isExample, const char *exampleName = 0,
                    bool singleLine = false, bool linkFromIndex = false);

   void writeDoc(DocRoot *root, QSharedPointer<Definition> ctx, QSharedPointer<MemberDef> md);

   bool parseText(const QByteArray &textStr);

   bool parseText(const QString &textStr)  {
      return parseText(textStr.toUtf8());
   }

   bool parseText(const char *textStr)  {
      return parseText(QByteArray(textStr));
   }

   void startIndexSection(IndexSections is) {
      forall(&OutputGenerator::startIndexSection, is);
   }

   void endIndexSection(IndexSections is) {
      forall(&OutputGenerator::endIndexSection, is);
   }

   void writePageLink(const char *name, bool first) {
      forall(&OutputGenerator::writePageLink, name, first);
   }

   void startProjectNumber() {
      forall(&OutputGenerator::startProjectNumber);
   }

   void endProjectNumber() {
      forall(&OutputGenerator::endProjectNumber);
   }

   void writeStyleInfo(int part) {
      forall(&OutputGenerator::writeStyleInfo, part);
   }

   void startFile(const QString &name, const QString &manName, const QString &title) {
      forall(&OutputGenerator::startFile, name.toUtf8(), manName.toUtf8(), title.toUtf8());
   }

   void writeSearchInfo() {
      forall(&OutputGenerator::writeSearchInfo);
   }

   void writeFooter(const char *navPath) {
      forall(&OutputGenerator::writeFooter, navPath);
   }

   void endFile() {
      forall(&OutputGenerator::endFile);
   }

   void startTitleHead(const char *fileName) {
      forall(&OutputGenerator::startTitleHead, fileName);
   }

   void endTitleHead(const char *fileName, const char *name) {
      forall(&OutputGenerator::endTitleHead, fileName, name);
   }

   void startTitle() {
      forall(&OutputGenerator::startTitle);
   }

   void endTitle() {
      forall(&OutputGenerator::endTitle);
   }

   //void newParagraph()
   //{ forall(&OutputGenerator::newParagraph); }

   void startParagraph() {
      forall(&OutputGenerator::startParagraph);
   }

   void endParagraph() {
      forall(&OutputGenerator::endParagraph);
   }

   void writeString(const char *text) {
      forall(&OutputGenerator::writeString, text);
   }

   void startIndexListItem() {
      forall(&OutputGenerator::startIndexListItem);
   }

   void endIndexListItem() {
      forall(&OutputGenerator::endIndexListItem);
   }

   void startIndexList() {
      forall(&OutputGenerator::startIndexList);
   }

   void endIndexList() {
      forall(&OutputGenerator::endIndexList);
   }

   void startIndexKey() {
      forall(&OutputGenerator::startIndexKey);
   }

   void endIndexKey() {
      forall(&OutputGenerator::endIndexKey);
   }

   void startIndexValue(bool b) {
      forall(&OutputGenerator::startIndexValue, b);
   }

   void endIndexValue(const char *name, bool b) {
      forall(&OutputGenerator::endIndexValue, name, b);
   }

   void startItemList() {
      forall(&OutputGenerator::startItemList);
   }

   void endItemList() {
      forall(&OutputGenerator::endItemList);
   }

   void startIndexItem(const char *ref, const char *file) {
      forall(&OutputGenerator::startIndexItem, ref, file);
   }

   void endIndexItem(const char *ref, const char *file) {
      forall(&OutputGenerator::endIndexItem, ref, file);
   }


   void docify(const QByteArray &s)  override {
      forall(&OutputGenerator::docify, s);
   }

   void docify(const char *s)   {
      docify(QByteArray(s));
   } 

   void docify(const QString &s)   {
      docify(s.toUtf8());
   }
 
   void codify(const QByteArray &s)  override {
      forall(&OutputGenerator::codify, s);
   } 

   void writeObjectLink(const QByteArray &ref, const QByteArray &file, const QByteArray &anchor, const QByteArray &name) override {
      forall(&OutputGenerator::writeObjectLink, ref, file, anchor, name.constData());
   }

   void writeCodeLink(const QByteArray &ref, const QByteArray &file, const QByteArray &anchor, const QByteArray &name,
                      const QByteArray &tooltip)  override
   {
      forall(&OutputGenerator::writeCodeLink, ref, file, anchor, name, tooltip);
   }

   void writeTooltip(const char *id, const DocLinkInfo &docInfo, const QByteArray &decl, const QByteArray &desc, 
                     const SourceLinkInfo &defInfo, const SourceLinkInfo &declInfo)  override {
      forall(&OutputGenerator::writeTooltip, id, docInfo, decl, desc, defInfo, declInfo);
   }

   void startTextLink(const QByteArray &file, const QByteArray &anchor)  override{
      forall(&OutputGenerator::startTextLink, file, anchor);
   }

   void endTextLink() {
      forall(&OutputGenerator::endTextLink);
   }

   void startHtmlLink(const QByteArray &url)  override{
      forall(&OutputGenerator::startHtmlLink, url);
   }

   void endHtmlLink() {
      forall(&OutputGenerator::endHtmlLink);
   }

   void writeStartAnnoItem(const char *type, const char *file, const char *path, const char *name) {
      forall(&OutputGenerator::writeStartAnnoItem, type, file, path, name);
   }

   void writeEndAnnoItem(const char *name) {
      forall(&OutputGenerator::writeEndAnnoItem, name);
   }

   void startTypewriter() {
      forall(&OutputGenerator::startTypewriter);
   }

   void endTypewriter() {
      forall(&OutputGenerator::endTypewriter);
   }

   void startGroupHeader(int extraLevels = 0) {
      forall(&OutputGenerator::startGroupHeader, extraLevels);
   }

   void endGroupHeader(int extraLevels = 0) {
      forall(&OutputGenerator::endGroupHeader, extraLevels);
   }

   //void writeListItem()
   //{ forall(&OutputGenerator::writeListItem); }
   void startItemListItem() {
      forall(&OutputGenerator::startItemListItem);
   }
   void endItemListItem() {
      forall(&OutputGenerator::endItemListItem);
   }

   void startMemberSections() {
      forall(&OutputGenerator::startMemberSections);
   }

   void endMemberSections() {
      forall(&OutputGenerator::endMemberSections);
   }
   void startHeaderSection() {
      forall(&OutputGenerator::startHeaderSection);
   }
   void endHeaderSection() {
      forall(&OutputGenerator::endHeaderSection);
   }
   void startMemberHeader(const char *anchor) {
      forall(&OutputGenerator::startMemberHeader, anchor);
   }
   void endMemberHeader() {
      forall(&OutputGenerator::endMemberHeader);
   }
   void startMemberSubtitle() {
      forall(&OutputGenerator::startMemberSubtitle);
   }
   void endMemberSubtitle() {
      forall(&OutputGenerator::endMemberSubtitle);
   }
   void startMemberDocList() {
      forall(&OutputGenerator::startMemberDocList);
   }
   void endMemberDocList() {
      forall(&OutputGenerator::endMemberDocList);
   }
   void startMemberList() {
      forall(&OutputGenerator::startMemberList);
   }
   void endMemberList() {
      forall(&OutputGenerator::endMemberList);
   }
   void startInlineHeader() {
      forall(&OutputGenerator::startInlineHeader);
   }
   void endInlineHeader() {
      forall(&OutputGenerator::endInlineHeader);
   }
   void startAnonTypeScope(int i1) {
      forall(&OutputGenerator::startAnonTypeScope, i1);
   }
   void endAnonTypeScope(int i1) {
      forall(&OutputGenerator::endAnonTypeScope, i1);
   }
   void startMemberItem(const char *anchor, int i1, const QByteArray &id = 0) {
      forall(&OutputGenerator::startMemberItem, anchor, i1, id);
   }
   void endMemberItem() {
      forall(&OutputGenerator::endMemberItem);
   }
   void startMemberTemplateParams() {
      forall(&OutputGenerator::startMemberTemplateParams);
   }
   void endMemberTemplateParams(const char *anchor, const QByteArray &inheritId) {
      forall(&OutputGenerator::endMemberTemplateParams, anchor, inheritId);
   }
   void startMemberGroupHeader(bool b) {
      forall(&OutputGenerator::startMemberGroupHeader, b);
   }
   void endMemberGroupHeader() {
      forall(&OutputGenerator::endMemberGroupHeader);
   }
   void startMemberGroupDocs() {
      forall(&OutputGenerator::startMemberGroupDocs);
   }
   void endMemberGroupDocs() {
      forall(&OutputGenerator::endMemberGroupDocs);
   }
   void startMemberGroup() {
      forall(&OutputGenerator::startMemberGroup);
   }
   void endMemberGroup(bool last) {
      forall(&OutputGenerator::endMemberGroup, last);
   }
 
  void insertMemberAlign(bool templ = false) {
      forall(&OutputGenerator::insertMemberAlign, templ);
   }

   void writeRuler() {
      forall(&OutputGenerator::writeRuler);
   }
 
  void writeAnchor(const char *fileName, const char *name) {
      forall(&OutputGenerator::writeAnchor, fileName, name);
   }

   void startCodeFragment() {
      forall(&OutputGenerator::startCodeFragment);
   }

   void endCodeFragment() {
      forall(&OutputGenerator::endCodeFragment);
   }

   void startCodeLine(bool hasLineNumbers) {
      forall(&OutputGenerator::startCodeLine, hasLineNumbers);
   }
 
  void endCodeLine() {
      forall(&OutputGenerator::endCodeLine);
   }

   void writeLineNumber(const char *ref, const QByteArray &file, const char *anchor, int lineNumber) {
      forall(&OutputGenerator::writeLineNumber, ref, file, anchor, lineNumber);
   }

   void startEmphasis() {
      forall(&OutputGenerator::startEmphasis);
   }

   void endEmphasis() {
      forall(&OutputGenerator::endEmphasis);
   }

   void writeChar(char c) {
      forall(&OutputGenerator::writeChar, c);
   }

   void startMemberDoc(const char *clName, const char *memName,
                       const char *anchor, const char *title, bool showInline) {
      forall(&OutputGenerator::startMemberDoc, clName, memName, anchor, title, showInline);
   }
   void endMemberDoc(bool hasArgs) {
      forall(&OutputGenerator::endMemberDoc, hasArgs);
   }
   void startDoxyAnchor(const char *fName, const char *manName,
                        const char *anchor, const char *name,
                        const char *args) {
      forall(&OutputGenerator::startDoxyAnchor, fName, manName, anchor, name, args);
   }
   void endDoxyAnchor(const char *fn, const char *anchor) {
      forall(&OutputGenerator::endDoxyAnchor, fn, anchor);
   }
   void writeLatexSpacing() {
      forall(&OutputGenerator::writeLatexSpacing);
   }
   void startDescription() {
      forall(&OutputGenerator::startDescription);
   }
   void endDescription() {
      forall(&OutputGenerator::endDescription);
   }
   void startDescItem() {
      forall(&OutputGenerator::startDescItem);
   }
   void endDescItem() {
      forall(&OutputGenerator::endDescItem);
   }
   void startDescForItem() {
      forall(&OutputGenerator::startDescForItem);
   }
   void endDescForItem() {
      forall(&OutputGenerator::endDescForItem);
   }
   void startSubsection() {
      forall(&OutputGenerator::startSubsection);
   }
   void endSubsection() {
      forall(&OutputGenerator::endSubsection);
   }
   void startSubsubsection() {
      forall(&OutputGenerator::startSubsubsection);
   }
   void endSubsubsection() {
      forall(&OutputGenerator::endSubsubsection);
   }
   void startCenter() {
      forall(&OutputGenerator::startCenter);
   }
   void endCenter() {
      forall(&OutputGenerator::endCenter);
   }

   void startSmall() {
      forall(&OutputGenerator::startSmall);
   }

   void endSmall() {
      forall(&OutputGenerator::endSmall);
   }

   void lineBreak(const QByteArray &style = 0)  override {
      forall(&OutputGenerator::lineBreak, style);
   }

   void startBold() {
      forall(&OutputGenerator::startBold);
   }

   void endBold() {
      forall(&OutputGenerator::endBold);
   }

   void startMemberDescription(const char *anchor, const char *inheritId = 0) {
      forall(&OutputGenerator::startMemberDescription, anchor, inheritId);
   }

   void endMemberDescription() {
      forall(&OutputGenerator::endMemberDescription);
   }

   void startMemberDeclaration() {
      forall(&OutputGenerator::startMemberDeclaration);
   }

   void endMemberDeclaration(const char *anchor, const char *inheritId) {
      forall(&OutputGenerator::endMemberDeclaration, anchor, inheritId);
   }

   void writeInheritedSectionTitle(const char *id,   const char *ref, const char *file, const char *anchor,
                                   const char *title, const char *name) {
      forall(&OutputGenerator::writeInheritedSectionTitle, id, ref, file, anchor, title, name);
   }

   void startSimpleSect(SectionTypes t, const QByteArray &file, const char *anchor, const char *title) override {
      forall(&OutputGenerator::startSimpleSect, t, file, anchor, title);
   }

   void endSimpleSect() {
      forall(&OutputGenerator::endSimpleSect);
   }

   void startParamList(ParamListTypes t, const char *title) {
      forall(&OutputGenerator::startParamList, t, title);
   }

   void endParamList() {
      forall(&OutputGenerator::endParamList);
   }

   void startIndent() {
      forall(&OutputGenerator::startIndent);
   }

   void endIndent() {
      forall(&OutputGenerator::endIndent);
   }

   void startSection(const char *lab, const char *title, SectionInfo::SectionType t) {
      forall(&OutputGenerator::startSection, lab, title, t);
   }

   void endSection(const char *lab, SectionInfo::SectionType t) {
      forall(&OutputGenerator::endSection, lab, t);
   }

   void addIndexItem(const char *s1, const char *s2) {
      forall(&OutputGenerator::addIndexItem, s1, s2);
   }

   void writeSynopsis() {
      forall(&OutputGenerator::writeSynopsis);
   }

   void startClassDiagram() {
      forall(&OutputGenerator::startClassDiagram);
   }

   void endClassDiagram(const ClassDiagram &d, const char *f, const char *n) {
      forall(&OutputGenerator::endClassDiagram, d, f, n);
   }

   void startPageRef() {
      forall(&OutputGenerator::startPageRef);
   }

   void endPageRef(const QByteArray &c, const QByteArray &a) override {
      forall(&OutputGenerator::endPageRef, c, a);
   }

   void startQuickIndices() {
      forall(&OutputGenerator::startQuickIndices);
   }

   void endQuickIndices() {
      forall(&OutputGenerator::endQuickIndices);
   }

   void writeSplitBar(const QString &name) {
      forall(&OutputGenerator::writeSplitBar, name);
   }

   void writeNavigationPath(const char *s) {
      forall(&OutputGenerator::writeNavigationPath, s);
   }
   void writeLogo() {
      forall(&OutputGenerator::writeLogo);
   }
   void writeQuickLinks(bool compact, HighlightedItem hli, const QString &file) {
      forall(&OutputGenerator::writeQuickLinks, compact, hli, file);
   }

   void writeSummaryLink(const char *file, const char *anchor, const char *title, bool first) {
      forall(&OutputGenerator::writeSummaryLink, file, anchor, title, first);
   }

   void writeSummaryLink(const QString &file, const char *anchor, const QByteArray &title, bool first) {
      forall(&OutputGenerator::writeSummaryLink, qPrintable(file), anchor, title.constData(), first);
   }

   void startContents() {
      forall(&OutputGenerator::startContents);
   }
   void endContents() {
      forall(&OutputGenerator::endContents);
   }
   void writeNonBreakableSpace(int num) {
      forall(&OutputGenerator::writeNonBreakableSpace, num);
   }
   void startDescTable(const char *title) {
      forall(&OutputGenerator::startDescTable, title);
   }
   void endDescTable() {
      forall(&OutputGenerator::endDescTable);
   }
   void startDescTableTitle() {
      forall(&OutputGenerator::startDescTableTitle);
   }
   void endDescTableTitle() {
      forall(&OutputGenerator::endDescTableTitle);
   }
   void startDescTableData() {
      forall(&OutputGenerator::startDescTableData);
   }
   void endDescTableData() {
      forall(&OutputGenerator::endDescTableData);
   }
   void startDotGraph() {
      forall(&OutputGenerator::startDotGraph);
   }
   void endDotGraph(const DotClassGraph &g) {
      forall(&OutputGenerator::endDotGraph, g);
   }
   void startInclDepGraph() {
      forall(&OutputGenerator::startInclDepGraph);
   }
   void endInclDepGraph(const DotInclDepGraph &g) {
      forall(&OutputGenerator::endInclDepGraph, g);
   }
   void startCallGraph() {
      forall(&OutputGenerator::startCallGraph);
   }
   void endCallGraph(const DotCallGraph &g) {
      forall(&OutputGenerator::endCallGraph, g);
   }
   void startDirDepGraph() {
      forall(&OutputGenerator::startDirDepGraph);
   }
   void endDirDepGraph(const DotDirDeps &g) {
      forall(&OutputGenerator::endDirDepGraph, g);
   }
   void startGroupCollaboration() {
      forall(&OutputGenerator::startGroupCollaboration);
   }
   void endGroupCollaboration(const DotGroupCollaboration &g) {
      forall(&OutputGenerator::endGroupCollaboration, g);
   }
   void writeGraphicalHierarchy(const DotGfxHierarchyTable &g) {
      forall(&OutputGenerator::writeGraphicalHierarchy, g);
   }
   void startTextBlock(bool dense = false) {
      forall(&OutputGenerator::startTextBlock, dense);
   }
   void endTextBlock(bool paraBreak = false) {
      forall(&OutputGenerator::endTextBlock, paraBreak);
   }
   void lastIndexPage() {
      forall(&OutputGenerator::lastIndexPage);
   }
   void startMemberDocPrefixItem() {
      forall(&OutputGenerator::startMemberDocPrefixItem);
   }
   void endMemberDocPrefixItem() {
      forall(&OutputGenerator::endMemberDocPrefixItem);
   }
   void startMemberDocName(bool align) {
      forall(&OutputGenerator::startMemberDocName, align);
   }
   void endMemberDocName() {
      forall(&OutputGenerator::endMemberDocName);
   }
   void startParameterType(bool first, const char *key) {
      forall(&OutputGenerator::startParameterType, first, key);
   }
   void endParameterType() {
      forall(&OutputGenerator::endParameterType);
   }
   void startParameterName(bool one) {
      forall(&OutputGenerator::startParameterName, one);
   }
   void endParameterName(bool last, bool one, bool bracket) {
      forall(&OutputGenerator::endParameterName, last, one, bracket);
   }
   void startParameterList(bool openBracket) {
      forall(&OutputGenerator::startParameterList, openBracket);
   }
   void endParameterList() {
      forall(&OutputGenerator::endParameterList);
   }
   void exceptionEntry(const char *prefix, bool closeBracket) {
      forall(&OutputGenerator::exceptionEntry, prefix, closeBracket);
   }

   void startConstraintList(const char *header) {
      forall(&OutputGenerator::startConstraintList, header);
   }
   void startConstraintParam() {
      forall(&OutputGenerator::startConstraintParam);
   }
   void endConstraintParam() {
      forall(&OutputGenerator::endConstraintParam);
   }
   void startConstraintType() {
      forall(&OutputGenerator::startConstraintType);
   }
   void endConstraintType() {
      forall(&OutputGenerator::endConstraintType);
   }
   void startConstraintDocs() {
      forall(&OutputGenerator::startConstraintDocs);
   }
   void endConstraintDocs() {
      forall(&OutputGenerator::endConstraintDocs);
   }
   void endConstraintList() {
      forall(&OutputGenerator::endConstraintList);
   }

   void startMemberDocSimple() {
      forall(&OutputGenerator::startMemberDocSimple);
   }
   void endMemberDocSimple() {
      forall(&OutputGenerator::endMemberDocSimple);
   }
   void startInlineMemberType() {
      forall(&OutputGenerator::startInlineMemberType);
   }
   void endInlineMemberType() {
      forall(&OutputGenerator::endInlineMemberType);
   }
   void startInlineMemberName() {
      forall(&OutputGenerator::startInlineMemberName);
   }
   void endInlineMemberName() {
      forall(&OutputGenerator::endInlineMemberName);
   }
   void startInlineMemberDoc() {
      forall(&OutputGenerator::startInlineMemberDoc);
   }
   void endInlineMemberDoc() {
      forall(&OutputGenerator::endInlineMemberDoc);
   }

   void startLabels() {
      forall(&OutputGenerator::startLabels);
   }

   void writeLabel(const char *l, bool isLast) {
      forall(&OutputGenerator::writeLabel, l, isLast);
   }

   void endLabels() {
      forall(&OutputGenerator::endLabels);
   }

   void startFontClass(const char *c) {
      forall(&OutputGenerator::startFontClass, c);
   }
   void endFontClass() {
      forall(&OutputGenerator::endFontClass);
   }
   void writeCodeAnchor(const char *name) {
      forall(&OutputGenerator::writeCodeAnchor, name);
   }
   void setCurrentDoc(QSharedPointer<Definition> context, const char *anchor, bool isSourceFile) override {
      forall(&OutputGenerator::setCurrentDoc, context, anchor, isSourceFile);
   }

   void addWord(const QString &word, bool hiPriority) override {
      forall(&OutputGenerator::addWord, word, hiPriority);
   }

   void startPlainFile(const QString &name) {
      for (auto item : m_outputs) {
         if (item->isEnabled()) {
            (item->startPlainFile)(name);
         }
      }
   }
   void endPlainFile() {
      for (auto item : m_outputs) {
         if (item->isEnabled()) {
            (item->endPlainFile)();
         }
      }
   }

 private:
   void debug();
   void clear();
  
   template<class BaseClass, class... Args, class... Ts>
   void forall( void (BaseClass::*func)(Args...), Ts&&... vs)  {

      for (auto item : m_outputs ) {                              
         if (item->isEnabled()) {
            (item->*func)(vs...);                 
         }
      }      
   }

   OutputList(const OutputList &ol);
   QList<OutputGenerator *> m_outputs;
};

#endif
