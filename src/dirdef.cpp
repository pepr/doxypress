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

#include <QCryptographicHash>

#include <config.h>
#include <docparser.h>
#include <dirdef.h>
#include <dot.h>
#include <doxygen.h>
#include <filename.h>
#include <ftextstream.h>
#include <layout.h>
#include <language.h>
#include <message.h>
#include <outputlist.h>
#include <util.h>

// must appear after the previous include - resolve soon 
#include <doxy_globals.h>

static int g_dirCount = 0;

DirDef::DirDef(const char *path) : Definition(path, 1, 1, path), visited(false)
{
   bool fullPathNames = Config_getBool("FULL_PATH_NAMES");
   // get display name (stipping the paths mentioned in STRIP_FROM_PATH)
   // get short name (last part of path)

   m_shortName = path;
   m_diskName = path;

   if (m_shortName.at(m_shortName.length() - 1) == '/') {
      // strip trailing /
      m_shortName = m_shortName.left(m_shortName.length() - 1);
   }

   int pi = m_shortName.lastIndexOf('/');
   if (pi != -1) {
      // remove everything till the last /
      m_shortName = m_shortName.mid(pi + 1);
   }

   setLocalName(m_shortName);
   m_dispName = fullPathNames ? stripFromPath(path) : m_shortName;

   if (m_dispName.length() > 0 && m_dispName.at(m_dispName.length() - 1) == '/') {
      // strip trailing /
      m_dispName = m_dispName.left(m_dispName.length() - 1);
   }

   m_fileList = new FileList;
   
   m_dirCount = g_dirCount++;
   m_level    = -1;
   m_parent   = 0;
}

DirDef::~DirDef()
{
   delete m_fileList;   
}

bool DirDef::isLinkableInProject() const
{
   return ! isReference();
}

bool DirDef::isLinkable() const
{
   return isReference() || isLinkableInProject();
}

void DirDef::addSubDir(DirDef *subdir)
{
   m_subdirs.inSort(subdir);

   subdir->setOuterScope(this);
   subdir->m_parent = this;
}

void DirDef::addFile(FileDef *fd)
{
   m_fileList->inSort(fd);
   fd->setDirDef(this);
}

static QByteArray encodeDirName(const QByteArray &anchor)
{ 
   QByteArray sigStr;
   sigStr = QCryptographicHash::hash(anchor, QCryptographicHash::Md5).toHex();

   return sigStr;  
}

QByteArray DirDef::getOutputFileBase() const
{
   return "dir_" + encodeDirName(m_diskName.toUtf8() );
}

void DirDef::writeDetailedDescription(OutputList &ol, const QByteArray &title)
{
   if ((!briefDescription().isEmpty() && Config_getBool("REPEAT_BRIEF")) || ! documentation().isEmpty()) {
      ol.pushGeneratorState();
      ol.disable(OutputGenerator::Html);
      ol.writeRuler();
      ol.popGeneratorState();
      ol.pushGeneratorState();
      ol.disableAllBut(OutputGenerator::Html);
      ol.writeAnchor(0, "details");
      ol.popGeneratorState();
      ol.startGroupHeader();
      ol.parseText(title);
      ol.endGroupHeader();

      // repeat brief description
      if (!briefDescription().isEmpty() && Config_getBool("REPEAT_BRIEF")) {
         ol.generateDoc(briefFile(), briefLine(), this, 0, briefDescription(), false, false);
      }
      // separator between brief and details
      if (!briefDescription().isEmpty() && Config_getBool("REPEAT_BRIEF") && !documentation().isEmpty()) {
         ol.pushGeneratorState();
         ol.disable(OutputGenerator::Man);
         ol.disable(OutputGenerator::RTF);

         // ol.newParagraph();  // FIXME:PARA
         ol.enableAll();
         ol.disableAllBut(OutputGenerator::Man);
         ol.enable(OutputGenerator::Latex);
         ol.writeString("\n\n");
         ol.popGeneratorState();
      }

      // write documentation
      if (!documentation().isEmpty()) {
         ol.generateDoc(docFile(), docLine(), this, 0, documentation() + "\n", true, false);
      }
   }
}

void DirDef::writeBriefDescription(OutputList &ol)
{
   if (!briefDescription().isEmpty() && Config_getBool("BRIEF_MEMBER_DESC")) {
      DocRoot *rootNode = validatingParseDoc(
                             briefFile(), briefLine(), this, 0, briefDescription(), true, false);
      if (rootNode && !rootNode->isEmpty()) {
         ol.startParagraph();
         ol.writeDoc(rootNode, this, 0);
         ol.pushGeneratorState();
         ol.disable(OutputGenerator::RTF);
         ol.writeString(" \n");
         ol.enable(OutputGenerator::RTF);

         if (Config_getBool("REPEAT_BRIEF") ||
               !documentation().isEmpty()
            ) {
            ol.disableAllBut(OutputGenerator::Html);
            ol.startTextLink(0, "details");
            ol.parseText(theTranslator->trMore());
            ol.endTextLink();
         }
         ol.popGeneratorState();

         ol.endParagraph();
      }
      delete rootNode;
   }
   ol.writeSynopsis();
}

void DirDef::writeDirectoryGraph(OutputList &ol)
{
   // write graph dependency graph
   if (Config_getBool("DIRECTORY_GRAPH") && Config_getBool("HAVE_DOT")) {
      DotDirDeps dirDep(this);

      if (!dirDep.isTrivial()) {
         msg("Generating dependency graph for directory %s\n", displayName().data());
         ol.disable(OutputGenerator::Man);

         //ol.startParagraph();

         ol.startDirDepGraph();
         ol.parseText(theTranslator->trDirDepGraph(qPrintable(shortName())));
         ol.endDirDepGraph(dirDep);

         //ol.endParagraph();

         ol.enableAll();
      }
   }
}

void DirDef::writeSubDirList(OutputList &ol)
{
   // write subdir list
   if (m_subdirs.count() > 0) {
      ol.startMemberHeader("subdirs");
      ol.parseText(theTranslator->trDir(true, false));
      ol.endMemberHeader();
      ol.startMemberList();
      
      for (auto dd : m_subdirs) {
         ol.startMemberDeclaration();
         ol.startMemberItem(dd->getOutputFileBase(), 0);
         ol.parseText(theTranslator->trDir(false, true) + " ");
         ol.insertMemberAlign();
         ol.writeObjectLink(dd->getReference(), dd->getOutputFileBase(), 0, dd->shortName().toUtf8());
         ol.endMemberItem();

         if (!dd->briefDescription().isEmpty() && Config_getBool("BRIEF_MEMBER_DESC")) {
            ol.startMemberDescription(dd->getOutputFileBase());
            ol.generateDoc(briefFile(), briefLine(), dd, 0, dd->briefDescription(),
                           false, // indexWords
                           false, // isExample
                           0,     // exampleName
                           true,  // single line
                           true   // link from index
                          );
            ol.endMemberDescription();
         }
         ol.endMemberDeclaration(0, 0);
      }

      ol.endMemberList();
   }
}

void DirDef::writeFileList(OutputList &ol)
{
   // write file list
   if (m_fileList->count() > 0) {
      ol.startMemberHeader("files");
      ol.parseText(theTranslator->trFile(true, false));
      ol.endMemberHeader();
      ol.startMemberList();
    
      for (auto fd : *m_fileList) {
         ol.startMemberDeclaration();
         ol.startMemberItem(fd->getOutputFileBase(), 0);
         ol.docify(theTranslator->trFile(false, true) + " ");
         ol.insertMemberAlign();

         if (fd->isLinkable()) {
            ol.writeObjectLink(fd->getReference(), fd->getOutputFileBase(), 0, fd->name());
         } else {
            ol.startBold();
            ol.docify(fd->name());
            ol.endBold();
         }

         if (fd->generateSourceFile()) {
            ol.pushGeneratorState();
            ol.disableAllBut(OutputGenerator::Html);
            ol.docify(" ");
            ol.startTextLink(fd->includeName(), 0);
            ol.docify("[");
            ol.parseText(theTranslator->trCode());
            ol.docify("]");
            ol.endTextLink();
            ol.popGeneratorState();
         }

         ol.endMemberItem();

         if (!fd->briefDescription().isEmpty() && Config_getBool("BRIEF_MEMBER_DESC")) {
            ol.startMemberDescription(fd->getOutputFileBase());
            ol.generateDoc(briefFile(), briefLine(), fd, 0, fd->briefDescription(),
                           false, // indexWords
                           false, // isExample
                           0,     // exampleName
                           true,  // single line
                           true   // link from index
                          );
            ol.endMemberDescription();
         }
         ol.endMemberDeclaration(0, 0);
      }
      ol.endMemberList();
   }
}

void DirDef::startMemberDeclarations(OutputList &ol)
{
   ol.startMemberSections();
}

void DirDef::endMemberDeclarations(OutputList &ol)
{
   ol.endMemberSections();
}

QByteArray DirDef::shortTitle() const
{
   return theTranslator->trDirReference(qPrintable(m_shortName));
}

bool DirDef::hasDetailedDescription() const
{
   static bool repeatBrief = Config_getBool("REPEAT_BRIEF");
   return (!briefDescription().isEmpty() && repeatBrief) || !documentation().isEmpty();
}

void DirDef::writeTagFile(FTextStream &tagFile)
{
   tagFile << "  <compound kind=\"dir\">" << endl;
   tagFile << "    <name>" << convertToXML(displayName()) << "</name>" << endl;
   tagFile << "    <path>" << convertToXML(name()) << "</path>" << endl;
   tagFile << "    <filename>" << convertToXML(getOutputFileBase()) << Doxygen::htmlFileExtension << "</filename>" << endl;

   QListIterator<LayoutDocEntry *> element(LayoutDocManager::instance().docEntries(LayoutDocManager::Directory));
   LayoutDocEntry *lde;  

   while (element.hasNext()) {
      lde = element.next();

      switch (lde->kind()) {
         case LayoutDocEntry::DirSubDirs: {

            if (m_subdirs.count() > 0) {

               for (auto dd : m_subdirs) {
                  tagFile << "    <dir>" << convertToXML(dd->displayName()) << "</dir>" << endl;
               }
            }
         }
         break;
         case LayoutDocEntry::DirFiles: {
            if (m_fileList->count() > 0) {
              
               for (auto fd : *m_fileList) {
                  tagFile << "    <file>" << convertToXML(fd->name()) << "</file>" << endl;
               }
            }
         }
         break;
         default:
            break;
      }
   }
   writeDocAnchorsToTagFile(tagFile);
   tagFile << "  </compound>" << endl;
}

void DirDef::writeDocumentation(OutputList &ol)
{
   static bool generateTreeView = Config_getBool("GENERATE_TREEVIEW");
   ol.pushGeneratorState();

   QByteArray title = theTranslator->trDirReference(qPrintable(m_dispName));
   startFile(ol, getOutputFileBase(), name(), title, HLI_None, !generateTreeView);

   if (!generateTreeView) {
      // write navigation path
      writeNavigationPath(ol);
      ol.endQuickIndices();
   }

   startTitle(ol, getOutputFileBase());
   ol.pushGeneratorState();
   ol.disableAllBut(OutputGenerator::Html);
   ol.parseText(shortTitle());
   ol.enableAll();
   ol.disable(OutputGenerator::Html);
   ol.parseText(title);
   ol.popGeneratorState();
   endTitle(ol, getOutputFileBase(), title);
   ol.startContents();

   //---------------------------------------- start flexible part -------------------------------

   SrcLangExt lang = getLanguage();

   QListIterator<LayoutDocEntry *> element(LayoutDocManager::instance().docEntries(LayoutDocManager::Directory));
   LayoutDocEntry *lde;  

   while (element.hasNext()) {
      lde = element.next();

      switch (lde->kind()) {
         case LayoutDocEntry::BriefDesc:
            writeBriefDescription(ol);
            break;
         case LayoutDocEntry::DirGraph:
            writeDirectoryGraph(ol);
            break;
         case LayoutDocEntry::MemberDeclStart:
            startMemberDeclarations(ol);
            break;
         case LayoutDocEntry::DirSubDirs:
            writeSubDirList(ol);
            break;
         case LayoutDocEntry::DirFiles:
            writeFileList(ol);
            break;
         case LayoutDocEntry::MemberDeclEnd:
            endMemberDeclarations(ol);
            break;
         case LayoutDocEntry::DetailedDesc: {
            LayoutDocEntrySection *ls = (LayoutDocEntrySection *)lde;
            writeDetailedDescription(ol, ls->title(lang));
         }
         break;
         case LayoutDocEntry::ClassIncludes:
         case LayoutDocEntry::ClassInlineClasses:
         case LayoutDocEntry::ClassInheritanceGraph:
         case LayoutDocEntry::ClassNestedClasses:
         case LayoutDocEntry::ClassCollaborationGraph:
         case LayoutDocEntry::ClassAllMembersLink:
         case LayoutDocEntry::ClassUsedFiles:
         case LayoutDocEntry::NamespaceNestedNamespaces:
         case LayoutDocEntry::NamespaceNestedConstantGroups:
         case LayoutDocEntry::NamespaceClasses:
         case LayoutDocEntry::NamespaceInlineClasses:
         case LayoutDocEntry::FileClasses:
         case LayoutDocEntry::FileNamespaces:
         case LayoutDocEntry::FileConstantGroups:
         case LayoutDocEntry::FileIncludes:
         case LayoutDocEntry::FileIncludeGraph:
         case LayoutDocEntry::FileIncludedByGraph:
         case LayoutDocEntry::FileSourceLink:
         case LayoutDocEntry::FileInlineClasses:
         case LayoutDocEntry::GroupClasses:
         case LayoutDocEntry::GroupInlineClasses:
         case LayoutDocEntry::GroupNamespaces:
         case LayoutDocEntry::GroupDirs:
         case LayoutDocEntry::GroupNestedGroups:
         case LayoutDocEntry::GroupFiles:
         case LayoutDocEntry::GroupGraph:
         case LayoutDocEntry::GroupPageDocs:
         case LayoutDocEntry::AuthorSection:
         case LayoutDocEntry::MemberGroups:
         case LayoutDocEntry::MemberDecl:
         case LayoutDocEntry::MemberDef:
         case LayoutDocEntry::MemberDefStart:
         case LayoutDocEntry::MemberDefEnd:
            err("Internal inconsistency: member %d should not be part of "
                "LayoutDocManager::Directory entry list\n", lde->kind());
            break;
      }
   }

   //---------------------------------------- end flexible part -------------------------------

   ol.endContents();

   endFileWithNavPath(this, ol);

   ol.popGeneratorState();
}

void DirDef::setLevel()
{
   if (m_level == -1) { // level not set before
      DirDef *p = parent();
      if (p) {
         p->setLevel();
         m_level = p->level() + 1;
      } else {
         m_level = 0;
      }
   }
}

/** Add as "uses" dependency between \a this dir and \a dir,
 *  that was caused by a dependency on file \a fd.
 */
void DirDef::addUsesDependency(DirDef *dir, FileDef *srcFd, FileDef *dstFd, bool inherited)
{
   if (this == dir) {
      return;   // do not add self-dependencies
   }
  
   // levels match => add direct dependency
   bool added = false;
   UsedDir *usedDir = m_usedDirs.value(dir->getOutputFileBase());

   if (usedDir) { 
      // dir dependency already present
      QSharedPointer<FilePair> usedPair = usedDir->findFilePair(srcFd->getOutputFileBase() + dstFd->getOutputFileBase());

      if (usedPair) { 
         // new file dependency
         // printf("  => new file\n");

         usedDir->addFileDep(srcFd, dstFd);
         added = true;

      } else {
         // dir & file dependency already added
      }

   } else { 
      // new directory dependency
      //printf("  => new file\n");

      usedDir = new UsedDir(dir, inherited);
      usedDir->addFileDep(srcFd, dstFd);

      m_usedDirs.insert(dir->getOutputFileBase(), usedDir);

      added = true;
   }

   if (added) {
      if (dir->parent()) {
         // add relation to parent of used dir
         addUsesDependency(dir->parent(), srcFd, dstFd, inherited);
      }
      if (parent()) {
         // add relation for the parent of this dir as well
         parent()->addUsesDependency(dir, srcFd, dstFd, true);
      }
   }
}

/** Computes the dependencies between directories
 */
void DirDef::computeDependencies()
{
   FileList *fl = m_fileList;

   if (fl) {      

      for (auto fd : *fl) {        
         QList<IncludeInfo> *ifl = fd->includeFileList();

         if (ifl) {           
            // foreach include file

            for (auto item : *ifl) {                  

               if (item.fileDef && item.fileDef->isLinkable()) { 
                  // linkable file
                  DirDef *usedDir = item.fileDef->getDirDef();

                  if (usedDir) {
                     // add dependency: thisDir->usedDir   
                     addUsesDependency(usedDir, fd, item.fileDef, false);
                  }
               }
            }
         }
      }
   }
}

bool DirDef::isParentOf(DirDef *dir) const
{
   if (dir->parent() == this) { // this is a parent of dir
      return true;

   } else if (dir->parent()) { // repeat for the parent of dir
      return isParentOf(dir->parent());

   } else {
      return false;
   }
}

bool DirDef::depGraphIsTrivial() const
{
   return false;
}

//----------------------------------------------------------------------

int FilePairDict::compareValues(const FilePair *left, const FilePair *right) const
{
   int orderHi = qstricmp(left->source()->name(), right->source()->name());
   int orderLo = qstricmp(left->destination()->name(), right->destination()->name());
   return orderHi == 0 ? orderLo : orderHi;
}

//----------------------------------------------------------------------

UsedDir::UsedDir(DirDef *dir, bool inherited)
   : m_dir(dir), m_inherited(inherited)
{ 
}

UsedDir::~UsedDir()
{
}

void UsedDir::addFileDep(FileDef *srcFd, FileDef *dstFd)
{
   m_filePairs.insert(srcFd->getOutputFileBase() + dstFd->getOutputFileBase(), 
                      QSharedPointer<FilePair> (new FilePair(srcFd, dstFd)) );
}

QSharedPointer<FilePair> UsedDir::findFilePair(const char *name)
{
   QByteArray n = name;

   if (n.isEmpty()) {
      return QSharedPointer<FilePair>(); 
   } else { 
      return m_filePairs.find(n);
   }
}

QSharedPointer<DirDef> DirDef::createNewDir(const char *path)
{
   assert(path != 0);
   QSharedPointer<DirDef> dir = Doxygen::directories.find(path);

   if (dir) { 
      // new dir
      //printf("Adding new dir %s\n",path);

      dir = QSharedPointer<DirDef> (new DirDef(path));

      //printf("createNewDir %s short=%s\n",path,dir->shortName().data());
      Doxygen::directories.insert(path, dir);
   }

   return dir;
}

bool DirDef::matchPath(const QByteArray &path, QStringList &list)
{  
   for (auto s : list) {
      QByteArray prefix = s.toUtf8();

      if (qstricmp(prefix.left(path.length()), path) == 0) { 
         // case insensitive compare
         return true;
      }      
   }

   return false;
}

/*! strip part of \a path if it matches
 *  one of the paths in the Config_getList("STRIP_FROM_PATH") list
 */
QSharedPointer<DirDef> DirDef::mergeDirectoryInTree(const QByteArray &path)
{
   //printf("DirDef::mergeDirectoryInTree(%s)\n",path.data());
   int p = 0;
   int i = 0;

   QSharedPointer<DirDef> dir;

   while ((i = path.indexOf('/', p)) != -1) {
      QByteArray part = path.left(i + 1);

      if (! matchPath(part, Config_getList("STRIP_FROM_PATH")) && (part != "/" && part != "//")) {
         dir = createNewDir(part);
      }

      p = i + 1;
   }

   return dir;
}

void DirDef::writeDepGraph(FTextStream &t)
{
   writeDotDirDepGraph(t, this);
}

//----------------------------------------------------------------------

static void writePartialDirPath(OutputList &ol, const DirDef *root, const DirDef *target)
{
   if (target->parent() != root) {
      writePartialDirPath(ol, root, target->parent());
      ol.writeString("&#160;/&#160;");
   }

   ol.writeObjectLink(target->getReference(), target->getOutputFileBase(), 0, target->shortName().toUtf8());
}

static void writePartialFilePath(OutputList &ol, const DirDef *root, const FileDef *fd)
{
   if (fd->getDirDef() && fd->getDirDef() != root) {
      writePartialDirPath(ol, root, fd->getDirDef());
      ol.writeString("&#160;/&#160;");
   }

   if (fd->isLinkable()) {
      ol.writeObjectLink(fd->getReference(), fd->getOutputFileBase(), 0, fd->name());

   } else {
      ol.startBold();
      ol.docify(fd->name());
      ol.endBold();
   }
}

void DirRelation::writeDocumentation(OutputList &ol)
{
   static bool generateTreeView = Config_getBool("GENERATE_TREEVIEW");
   ol.pushGeneratorState();
   ol.disableAllBut(OutputGenerator::Html);

   QByteArray shortTitle = theTranslator->trDirRelation( qPrintable(m_src->shortName() + " &rarr; " + m_dst->dir()->shortName()) );
   QByteArray title = theTranslator->trDirRelation( qPrintable(m_src->displayName() + " -> " + m_dst->dir()->shortName()) );

   startFile(ol, getOutputFileBase(), getOutputFileBase(), 
             title, HLI_None, !generateTreeView, m_src->getOutputFileBase());

   if (!generateTreeView) {
      // write navigation path
      m_src->writeNavigationPath(ol);
      ol.endQuickIndices();
   }

   ol.startContents();

   ol.writeString("<h3>" + shortTitle + "</h3>");
   ol.writeString("<table class=\"dirtab\">");
   ol.writeString("<tr class=\"dirtab\">");
   ol.writeString("<th class=\"dirtab\">");

   ol.parseText(theTranslator->trFileIn(m_src->pathFragment()));

   ol.writeString("</th>");
   ol.writeString("<th class=\"dirtab\">");

   ol.parseText(theTranslator->trIncludesFileIn(m_dst->dir()->pathFragment()));

   ol.writeString("</th>");
   ol.writeString("</tr>");
  
   for (auto fp : m_dst->filePairs())  {
      ol.writeString("<tr class=\"dirtab\">");
      ol.writeString("<td class=\"dirtab\">");

      writePartialFilePath(ol, m_src, fp->source());

      ol.writeString("</td>");
      ol.writeString("<td class=\"dirtab\">");

      writePartialFilePath(ol, m_dst->dir(), fp->destination());

      ol.writeString("</td>");
      ol.writeString("</tr>");
   }

   ol.writeString("</table>");

   ol.endContents();

   endFileWithNavPath(m_src, ol);

   ol.popGeneratorState();
}

//----------------------------------------------------------------------
// external functions

/** In order to create stable, but unique directory names,
 *  we compute the common part of the path shared by all directories.
 */
static void computeCommonDirPrefix()
{
   QByteArray path;
   QSharedPointer<DirDef> dir;

   DirSDict::Iterator sdi(Doxygen::directories);

   if (Doxygen::directories.count() > 0) { 
      // we have at least one dir, start will full path of first dir
      sdi.toFirst();

      dir   = sdi.current();
      path  = dir->name();

      int i = path.lastIndexOf('/', path.length() - 2);

      path = path.left(i + 1);
      bool done = false;

      if (i == -1) {
         path = "";

      } else {
         while (! done) {
            int l = path.length();
            int count = 0;

            for (sdi.toFirst(); (dir = sdi.current()); ++sdi) {
               QByteArray dirName = dir->name();

               if (dirName.length() > path.length()) {

                  if (qstrncmp(dirName, path, l) != 0) { 
                     // dirName does not start with path
                     int i = path.lastIndexOf('/', l - 2);

                     if (i == -1) { 
                        // no unique prefix -> stop
                        path = "";
                        done = true;

                     } else { 
                        // restart with shorter path
                        path = path.left(i + 1);
                        break;
                     }
                  }

               } else { 
                  // dir is shorter than path -> take path of dir as new start
                  path = dir->name();
                  int i = path.lastIndexOf('/', l - 2);

                  if (i == -1) { 
                     // no unique prefix -> stop
                     path = "";
                     done = true;

                  } else { // restart with shorter path
                     path = path.left(i + 1);
                  }

                  break;
               }
               count++;
            }

            if (count == Doxygen::directories.count()) {
               // path matches for all directories -> found the common prefix            
               done = true;
            }
         }
      }
   }
   
   for (sdi.toFirst(); (dir = sdi.current()); ++sdi) {   
      QByteArray diskName = dir->name().right(dir->name().length() - path.length());
      dir->setDiskName(diskName);

      //  printf("set disk name: %s -> %s\n",dir->name().data(),diskName.data());
   }
}

void buildDirectories()
{
   // for each input file  
   for (auto fn : *Doxygen::inputNameList) {   
    
      for (auto fd : *fn) {  
         // printf("buildDirectories %s\n",fd->name().data());

         if (fd->getReference().isEmpty() && !fd->isDocumentationFile()) {
            QSharedPointer<DirDef> dir;

            if ((dir = Doxygen::directories.find(fd->getPath())) == 0) { 
               // new directory
               dir = DirDef::mergeDirectoryInTree(fd->getPath());
            }

            if (dir) {
               dir->addFile(fd);
            }

         } else {
            // do something for file imported via tag files.
         }
      }
   }
   
   // compute relations between directories => introduce container dirs.
 
   for (auto dir : Doxygen::directories) {  
      // printf("New dir %s\n",dir->displayName().data());

      QByteArray name = dir->name();
      int i = name.lastIndexOf('/', name.length() - 2);

      if (i > 0) {
         QSharedPointer<DirDef> parent = Doxygen::directories.find(name.left(i + 1));
        
         if (parent) {                       
            parent->addSubDir(dir.data());
         }
      }
   }

   computeCommonDirPrefix();
}

void computeDirDependencies()
{
   // compute nesting level for each directory
   for (auto dir : Doxygen::directories) {  
      dir->setLevel();
   }

   // compute uses dependencies between directories
   for (auto dir : Doxygen::directories) { 
      //printf("computeDependencies for %s: #dirs=%d\n",dir->name().data(),Doxygen::directories.count());
      dir->computeDependencies();
   }
}

void generateDirDocs(OutputList &ol)
{
   for (auto dir : Doxygen::directories) { 
      dir->writeDocumentation(ol);
   }

   if (Config_getBool("DIRECTORY_GRAPH")) {    
      for (auto item : Doxygen::dirRelations) { 
         item->writeDocumentation(ol);
      }
   }
}
