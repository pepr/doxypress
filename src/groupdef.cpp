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

#include <QRegExp>

#include <ctype.h>

#include <arguments.h>
#include <config.h>
#include <classdef.h>
#include <classlist.h>
#include <dirdef.h>
#include <doxy_globals.h>
#include <dot.h>
#include <docparser.h>
#include <entry.h>
#include <filedef.h>
#include <groupdef.h>
#include <language.h>
#include <layout.h>
#include <membername.h>
#include <memberlist.h>
#include <message.h>
#include <membergroup.h>
#include <namespacedef.h>
#include <outputlist.h>
#include <pagedef.h>
#include <searchindex.h>
#include <util.h>

GroupDef::GroupDef(const char *df, int dl, const char *na, const char *t, QString refFileName) 
                  : Definition(df, dl, 1, na)
{   
   classSDict     = new ClassSDict();   
   namespaceSDict = new NamespaceSDict();
   pageDict       = new PageSDict();
   exampleDict    = new PageSDict();

   dirList        = new SortedList<QSharedPointer<DirDef>>;
   fileList       = new FileList;
   groupList      = new SortedList<QSharedPointer<GroupDef>>;

   allMemberNameInfoSDict = new MemberNameInfoSDict();
   
   if (! refFileName.isEmpty() ) {
      fileName = stripExtension(refFileName);

   } else {
      fileName = "group-" + QString(na);
   }

   setGroupTitle(t);
   memberGroupSDict = new MemberGroupSDict;

   allMemberList = QMakeShared<MemberList>(MemberListType_allMembersList);

   visited = 0;   
   m_subGrouping = Config::getBool("allow-sub-grouping");
}

GroupDef::~GroupDef()
{
   delete dirList;
   delete fileList;
   delete groupList;

   delete classSDict;   
   delete namespaceSDict;
   delete pageDict;
   delete exampleDict;   
   delete allMemberNameInfoSDict;
   delete memberGroupSDict; 
}

void GroupDef::setGroupTitle( const char *t )
{
   if ( t && qstrlen(t) ) {
      title = t;
      titleSet = true;

   } else {
      title = name();
      title[0] = toupper(title[0]);
      titleSet = false;
   }
}


void GroupDef::distributeMemberGroupDocumentation()
{
   for (auto mg : *memberGroupSDict) {
      mg->distributeMemberGroupDocumentation();
   }
}

void GroupDef::findSectionsInDocumentation()
{
   QSharedPointer<GroupDef> self = sharedFrom(this);

   docFindSections(documentation(), self, 0, docFile());
  
   for (auto mg : *memberGroupSDict) {
      mg->findSectionsInDocumentation();
   }  

   for (auto ml : m_memberLists) {
      if (ml->listType() & MemberListType_declarationLists) {
         ml->findSectionsInDocumentation();
      }
   }
}

void GroupDef::addFile(QSharedPointer<FileDef> def)
{
   static bool sortBriefDocs = Config::getBool("sort-brief-docs");

   if (def->isHidden()) {
      return;
   }

   updateLanguage(def);

   if (sortBriefDocs) {
      fileList->inSort(def);

   } else {
      fileList->append(def);
   }
}

bool GroupDef::addClass(QSharedPointer<ClassDef> cd)
{
   if (cd->isHidden()) {
      return false;
   }

   updateLanguage(cd);
   QByteArray qn = cd->qualifiedName();  

   if (classSDict->find(qn) == 0) {          
      classSDict->insert(qn, cd); 
      return true;
   }

   return false;
}

bool GroupDef::addNamespace(QSharedPointer<NamespaceDef> def)
{
   if (def->isHidden()) {
      return false;
   }

   updateLanguage(def);

   if (namespaceSDict->find(def->name()) == 0) {
      namespaceSDict->insert(def->name(), def);      
      return true;
   }

   return false;
}

void GroupDef::addDir(QSharedPointer<DirDef> def)
{
   if (def->isHidden()) {
      return;
   }

   if (Config::getBool("sort-brief-docs")) {
      dirList->inSort(def);

   } else {
      dirList->append(def);
   }
}

void GroupDef::addPage(QSharedPointer<PageDef> def)
{   
   QSharedPointer<GroupDef> self = sharedFrom(this);

   if (def->isHidden()) {
      return;
   }
   
   pageDict->insert(def->name(), def);

   def->makePartOfGroup(self);
}

void GroupDef::addExample(QSharedPointer<PageDef> def)
{
   if (def->isHidden()) {
      return;
   }

   exampleDict->insert(def->name(), def);
}

void GroupDef::addMembersToMemberGroup()
{
   QSharedPointer<GroupDef> self = sharedFrom(this);

   for (auto ml : m_memberLists) {
      if (ml->listType() & MemberListType_declarationLists) {
         ::addMembersToMemberGroup(ml, &memberGroupSDict, self);
      }
   }
   
   for (auto mg : *memberGroupSDict) {
      mg->setInGroup(true);
   }
}


bool GroupDef::insertMember(QSharedPointer<MemberDef> md, bool docOnly)
{
   if (md->isHidden()) {
      return false;
   }

   updateLanguage(md);
  
   QSharedPointer<MemberNameInfo> mni;

   if ((mni = (*allMemberNameInfoSDict)[md->name()])) {
      // member with this name already found
  
      for (auto srcMi : *mni) {
         QSharedPointer<MemberDef> srcMd = srcMi.memberDef;

         if (srcMd == md) {
            return false;   // already added before!
         }

         bool sameScope = srcMd->getOuterScope() == md->getOuterScope() || // same class or namespace
                          // both inside a file => definition and declaration do not have to be in the same file
                          (srcMd->getOuterScope()->definitionType() == Definition::TypeFile &&
                           md->getOuterScope()->definitionType() == Definition::TypeFile);

         ArgumentList *srcMdAl  = srcMd->argumentList();
         ArgumentList *mdAl     = md->argumentList();
         ArgumentList *tSrcMdAl = srcMd->templateArguments();
         ArgumentList *tMdAl    = md->templateArguments();

         if (srcMd->isFunction() && md->isFunction() && 
               ((tSrcMdAl == 0 && tMdAl == 0) || (tSrcMdAl != 0 && tMdAl != 0 && tSrcMdAl->count() == tMdAl->count()) ) &&      
               matchArguments2(srcMd->getOuterScope(), srcMd->getFileDef(), srcMdAl, md->getOuterScope(), md->getFileDef(), mdAl, true ) &&
               sameScope  ) {

            if (srcMd->getGroupAlias() == 0) {
               md->setGroupAlias(srcMd);

            } else if (md != srcMd->getGroupAlias()) {
               md->setGroupAlias(srcMd->getGroupAlias());

            }

            return false; // member is the same as one that is already added
         }
      }

      mni->append(MemberInfo(md, md->protection(), md->virtualness(), false));

   } else {
      QSharedPointer<MemberNameInfo> mni (new MemberNameInfo(md->name()));
      mni->append(MemberInfo(md, md->protection(), md->virtualness(), false));

      allMemberNameInfoSDict->insert(mni->memberName(), mni);
   }
 
   allMemberList->append(md);

   switch (md->memberType()) {
      case MemberType_Variable:
         if (!docOnly) {
            addMemberToList(MemberListType_decVarMembers, md);
         }

         addMemberToList(MemberListType_docVarMembers, md);
         break;

      case MemberType_Function:
         if (!docOnly) {
            addMemberToList(MemberListType_decFuncMembers, md);
         }

         addMemberToList(MemberListType_docFuncMembers, md);
         break;

      case MemberType_Typedef:
         if (!docOnly) {
            addMemberToList(MemberListType_decTypedefMembers, md);
         }

         addMemberToList(MemberListType_docTypedefMembers, md);
         break;

      case MemberType_Enumeration:
         if (!docOnly) {
            addMemberToList(MemberListType_decEnumMembers, md);
         }

         addMemberToList(MemberListType_docEnumMembers, md);
         break;

      case MemberType_EnumValue:
         if (!docOnly) {
            addMemberToList(MemberListType_decEnumValMembers, md);
         }

         addMemberToList(MemberListType_docEnumValMembers, md);
         break;

      case MemberType_Define:
         if (!docOnly) {
            addMemberToList(MemberListType_decDefineMembers, md);
         }

         addMemberToList(MemberListType_docDefineMembers, md);
         break;

      case MemberType_Signal:
         if (!docOnly) {
            addMemberToList(MemberListType_decSignalMembers, md);
         }

         addMemberToList(MemberListType_docSignalMembers, md);
         break;

      case MemberType_Slot:
         if (md->protection() == Public) {
            if (!docOnly) {
               addMemberToList(MemberListType_decPubSlotMembers, md);
            }

            addMemberToList(MemberListType_docPubSlotMembers, md);

         } else if (md->protection() == Protected) {
            if (!docOnly) {
               addMemberToList(MemberListType_decProSlotMembers, md);
            }
            addMemberToList(MemberListType_docProSlotMembers, md);
         } else {
            if (!docOnly) {
               addMemberToList(MemberListType_decPriSlotMembers, md);
            }
            addMemberToList(MemberListType_docPriSlotMembers, md);
         }
         break;
      case MemberType_Event:
         if (!docOnly) {
            addMemberToList(MemberListType_decEventMembers, md);
         }
         addMemberToList(MemberListType_docEventMembers, md);
         break;
      case MemberType_Property:
         if (!docOnly) {
            addMemberToList(MemberListType_decPropMembers, md);
         }
         addMemberToList(MemberListType_docPropMembers, md);
         break;
      case MemberType_Friend:
         if (!docOnly) {
            addMemberToList(MemberListType_decFriendMembers, md);
         }
         addMemberToList(MemberListType_docFriendMembers, md);
         break;
      default:
         err("GroupDef::insertMembers(): "
             "member `%s' (typeid=%d) with scope `%s' inserted in group scope `%s'!\n",
             md->name().data(), md->memberType(),
             md->getClassDef() ? md->getClassDef()->name().data() : "",
             name().data());
   }
   return true;
}

void GroupDef::removeMember(QSharedPointer<MemberDef> md)
{
   QSharedPointer<MemberNameInfo> mni(allMemberNameInfoSDict->find(md->name()));

   if (mni) {

      for (auto iter = mni->begin(); iter != mni->end(); ++iter) { 

         if (iter->memberDef == md) {
            mni->erase(iter);
            break;
          }
      }

      if ( mni->isEmpty() ) {
         allMemberNameInfoSDict->remove(md->name());
      }

      removeMemberFromList(MemberListType_allMembersList, md);
      switch (md->memberType()) {
         case MemberType_Variable:
            removeMemberFromList(MemberListType_decVarMembers, md);
            removeMemberFromList(MemberListType_docVarMembers, md);
            break;
         case MemberType_Function:
            removeMemberFromList(MemberListType_decFuncMembers, md);
            removeMemberFromList(MemberListType_docFuncMembers, md);
            break;
         case MemberType_Typedef:
            removeMemberFromList(MemberListType_decTypedefMembers, md);
            removeMemberFromList(MemberListType_docTypedefMembers, md);
            break;
         case MemberType_Enumeration:
            removeMemberFromList(MemberListType_decEnumMembers, md);
            removeMemberFromList(MemberListType_docEnumMembers, md);
            break;
         case MemberType_EnumValue:
            removeMemberFromList(MemberListType_decEnumValMembers, md);
            removeMemberFromList(MemberListType_docEnumValMembers, md);
            break;
         case MemberType_Define:
            removeMemberFromList(MemberListType_decDefineMembers, md);
            removeMemberFromList(MemberListType_docDefineMembers, md);
            break;
         case MemberType_Signal:
            removeMemberFromList(MemberListType_decSignalMembers, md);
            removeMemberFromList(MemberListType_docSignalMembers, md);
            break;
         case MemberType_Slot:
            if (md->protection() == Public) {
               removeMemberFromList(MemberListType_decPubSlotMembers, md);
               removeMemberFromList(MemberListType_docPubSlotMembers, md);
            } else if (md->protection() == Protected) {
               removeMemberFromList(MemberListType_decProSlotMembers, md);
               removeMemberFromList(MemberListType_docProSlotMembers, md);
            } else {
               removeMemberFromList(MemberListType_decPriSlotMembers, md);
               removeMemberFromList(MemberListType_docPriSlotMembers, md);
            }
            break;
         case MemberType_Event:
            removeMemberFromList(MemberListType_decEventMembers, md);
            removeMemberFromList(MemberListType_docEventMembers, md);
            break;
         case MemberType_Property:
            removeMemberFromList(MemberListType_decPropMembers, md);
            removeMemberFromList(MemberListType_docPropMembers, md);
            break;
         case MemberType_Friend:
            removeMemberFromList(MemberListType_decFriendMembers, md);
            removeMemberFromList(MemberListType_docFriendMembers, md);
            break;
         default:
            err("GroupDef::removeMember(): unexpected member remove in file!\n");
      }
   }
}

bool GroupDef::findGroup(QSharedPointer<const GroupDef> def) const
{
   if (this == def) {
      return true;

   } else if (groupList) {       
    
      for (auto gd : *groupList) {
         if (gd->findGroup(def)) {
            return true;
         }
      }
   }

   return false;   
}

void GroupDef::addGroup(QSharedPointer<GroupDef> def)
{   
   groupList->append(def);
}

bool GroupDef::isASubGroup() const
{
   SortedList<QSharedPointer<GroupDef>> *groups = partOfGroups();
   return groups != 0 && groups->count() != 0;
}

int GroupDef::countMembers() const
{
   return fileList->count() + classSDict->count() + namespaceSDict->count() + groupList->count() +
          allMemberList->count() + pageDict->count() + exampleDict->count();
}

/*! Compute the HTML anchor names for all members in the group */
void GroupDef::computeAnchors()
{   
   setAnchors(allMemberList);
}

void GroupDef::writeTagFile(QTextStream &tagFile)
{
   tagFile << "  <compound kind=\"group\">" << endl;
   tagFile << "    <name>" << convertToXML(name()) << "</name>" << endl;
   tagFile << "    <title>" << convertToXML(title) << "</title>" << endl;
   tagFile << "    <filename>" << convertToXML(getOutputFileBase()) << Doxygen::htmlFileExtension << "</filename>" << endl;
 
   for (auto lde : LayoutDocManager::instance().docEntries(LayoutDocManager::Group)) {
      switch (lde->kind()) {

         case LayoutDocEntry::GroupClasses:
         {
            if (classSDict) {              
               for (auto cd : *classSDict) {
                  if (cd->isLinkableInProject()) {
                     tagFile << "    <class kind=\"" << cd->compoundTypeString()
                             << "\">" << convertToXML(cd->name()) << "</class>" << endl;
                  }
               }
            }
         }
         break;

         case LayoutDocEntry::GroupNamespaces: 
         {
            if (namespaceSDict) {            
               for (auto nd : *namespaceSDict) {
                  if (nd->isLinkableInProject()) {
                     tagFile << "    <namespace>" << convertToXML(nd->name())
                             << "</namespace>" << endl;
                  }
               }
            }
         }
         break;

         case LayoutDocEntry::GroupFiles: 
         {
            if (fileList) {               
               for (auto item : *fileList)  {
                  if (item->isLinkableInProject()) {
                     tagFile << "    <file>" << convertToXML(item->name()) << "</file>" << endl;
                  }
               }
            }
         }
         break;

         case LayoutDocEntry::GroupPageDocs: 
         {
            if (pageDict) {              
               for (auto item : *pageDict)  {
                  QByteArray pageName = item->getOutputFileBase();

                  if (item->isLinkableInProject()) {
                     tagFile << "    <page>" << convertToXML(pageName) << "</page>" << endl;
                  }
               }
            }
         }
         break;

         case LayoutDocEntry::GroupDirs:
         {
            if (dirList) {          
               for (auto item : *dirList)  {
                  if (item->isLinkableInProject()) {
                     tagFile << "    <dir>" << convertToXML(item->displayName()) << "</dir>" << endl;
                  }
               }
            }
         }
         break;

         case LayoutDocEntry::GroupNestedGroups: 
         {
            if (groupList) {              
               for (auto item : *groupList)  {
                  if (item->isVisible()) {
                     tagFile << "    <subgroup>" << convertToXML(item->name()) << "</subgroup>" << endl;
                  }
               }
            }
         }
         break;

         case LayoutDocEntry::MemberDecl: {
            LayoutDocEntryMemberDecl *lmd = (LayoutDocEntryMemberDecl *)lde;
            QSharedPointer<MemberList> ml = getMemberList(lmd->type);

            if (ml) {
               ml->writeTagFile(tagFile);
            }
         }
         break;

         case LayoutDocEntry::MemberGroups: {
            if (memberGroupSDict) {              
               for (auto mg : *memberGroupSDict) {
                  mg->writeTagFile(tagFile);
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

void GroupDef::writeDetailedDescription(OutputList &ol, const QByteArray &title)
{
   QSharedPointer<GroupDef> self = sharedFrom(this);

   if ((! briefDescription().isEmpty() && Config::getBool("repeat-brief"))
         || !documentation().isEmpty() || !inbodyDocumentation().isEmpty()) {

      if (pageDict->count() != countMembers()) { // not only pages -> classical layout
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
      }

      // repeat brief description
      if (!briefDescription().isEmpty() && Config::getBool("repeat-brief")) {
         ol.generateDoc(briefFile(), briefLine(), self, QSharedPointer<MemberDef>(), briefDescription(), false, false);
      }

      // write separator between brief and details
      if (!briefDescription().isEmpty() && Config::getBool("repeat-brief") && ! documentation().isEmpty()) {
         ol.pushGeneratorState();
         ol.disable(OutputGenerator::Man);
         ol.disable(OutputGenerator::RTF);
         // ol.newParagraph(); // FIXME:PARA
         ol.enableAll();
         ol.disableAllBut(OutputGenerator::Man);
         ol.enable(OutputGenerator::Latex);
         ol.writeString("\n\n");
         ol.popGeneratorState();
      }

      // write detailed documentation
      if (!documentation().isEmpty()) {
         ol.generateDoc(docFile(), docLine(), self, QSharedPointer<MemberDef>(), documentation() + "\n", true, false);
      }

      // write inbody documentation
      if (!inbodyDocumentation().isEmpty()) {
         ol.generateDoc(inbodyFile(), inbodyLine(), self, QSharedPointer<MemberDef>(), inbodyDocumentation() + "\n", true, false);
      }
   }
}

void GroupDef::writeBriefDescription(OutputList &ol)
{
   QSharedPointer<GroupDef> self = sharedFrom(this);

   if (! briefDescription().isEmpty() && Config::getBool("brief-member-desc")) {
      DocRoot *rootNode = validatingParseDoc(briefFile(), briefLine(), self, QSharedPointer<MemberDef>(), 
                                             briefDescription(), true, false, 0, true, false);

      if (rootNode && !rootNode->isEmpty()) {
         ol.startParagraph();
         ol.writeDoc(rootNode, self, QSharedPointer<MemberDef>());
         ol.pushGeneratorState();
         ol.disable(OutputGenerator::RTF);
         ol.writeString(" \n");
         ol.enable(OutputGenerator::RTF);

         if (Config::getBool("repeat-brief") || ! documentation().isEmpty() ) {
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
}

void GroupDef::writeGroupGraph(OutputList &ol)
{
   QSharedPointer<GroupDef> self = sharedFrom(this);

   if (Config::getBool("have-dot") /*&& Config::getBool("group-graphs")*/ ) {
      DotGroupCollaboration graph(self);

      if (! graph.isTrivial()) {
         msg("Generating dependency graph for group %s\n", qualifiedName().data());
         ol.pushGeneratorState();
         ol.disable(OutputGenerator::Man);
         //ol.startParagraph();
         ol.startGroupCollaboration();
         ol.parseText(theTranslator->trCollaborationDiagram(title));
         ol.endGroupCollaboration(graph);
         //ol.endParagraph();
         ol.popGeneratorState();
      }
   }
}

void GroupDef::writeFiles(OutputList &ol, const QByteArray &title)
{
   // write list of files
   if (fileList->count() > 0) {
      ol.startMemberHeader("files");
      ol.parseText(title);
      ol.endMemberHeader();
      ol.startMemberList();   

      for (auto item : *fileList) {
         ol.startMemberDeclaration();
         ol.startMemberItem(item->getOutputFileBase(), 0);
         ol.docify(theTranslator->trFile(false, true) + " ");

         ol.insertMemberAlign();
         ol.writeObjectLink(item->getReference(), item->getOutputFileBase(), 0, item->name());
         ol.endMemberItem();

         if (! item->briefDescription().isEmpty() && Config::getBool("brief-member-desc")) {
            ol.startMemberDescription(item->getOutputFileBase());
            ol.generateDoc(briefFile(), briefLine(), item, QSharedPointer<MemberDef>(), 
                           item->briefDescription(), false, false, 0, true, false);

            ol.endMemberDescription();
         }
         ol.endMemberDeclaration(0, 0);
      }

      ol.endMemberList();
   }
}

void GroupDef::writeNamespaces(OutputList &ol, const QByteArray &title)
{
   // write list of namespaces
   namespaceSDict->writeDeclaration(ol, title);
}

void GroupDef::writeNestedGroups(OutputList &ol, const QByteArray &title)
{
   // write list of groups
   int count = 0;

   if (groupList->count() > 0) {    
      for (auto gd : *groupList) {
         if (gd->isVisible()) {
            count++;
         }
      }
   }

   if (count > 0) {
      ol.startMemberHeader("groups");
      ol.parseText(title);
      ol.endMemberHeader();
      ol.startMemberList();

      if (Config::getBool("sort-group-names")) {
         groupList->sort();
      }

      for (auto gd : *groupList) {
         if (gd->isVisible()) {
            ol.startMemberDeclaration();
            ol.startMemberItem(gd->getOutputFileBase(), 0);
     
            ol.insertMemberAlign();
            ol.writeObjectLink(gd->getReference(), gd->getOutputFileBase(), 0, gd->groupTitle());
            ol.endMemberItem();

            if (! gd->briefDescription().isEmpty() && Config::getBool("brief-member-desc")) {
               ol.startMemberDescription(gd->getOutputFileBase());
               ol.generateDoc(briefFile(), briefLine(), gd, QSharedPointer<MemberDef>(), gd->briefDescription(), 
                              false, false, 0, true, false);

               ol.endMemberDescription();
            }
            ol.endMemberDeclaration(0, 0);
         }
      }
      ol.endMemberList();
   }
}

void GroupDef::writeDirs(OutputList &ol, const QByteArray &title)
{
   // write list of directories
   if (dirList->count() > 0) {
      ol.startMemberHeader("dirs");
      ol.parseText(title);
      ol.endMemberHeader();
      ol.startMemberList();
      
      for (auto dd : *dirList ) { 
         ol.startMemberDeclaration();
         ol.startMemberItem(dd->getOutputFileBase(), 0);
         ol.parseText(theTranslator->trDir(false, true));

         ol.insertMemberAlign();
         ol.writeObjectLink(dd->getReference(), dd->getOutputFileBase(), 0, dd->shortName().toUtf8());
         ol.endMemberItem();

         if (!dd->briefDescription().isEmpty() && Config::getBool("brief-member-desc")) {
            ol.startMemberDescription(dd->getOutputFileBase());
            ol.generateDoc(briefFile(), briefLine(), dd, QSharedPointer<MemberDef>(), dd->briefDescription(), 
                           false, false, 0, true, false);

            ol.endMemberDescription();
         }
         ol.endMemberDeclaration(0, 0);
      }

      ol.endMemberList();
   }
}

void GroupDef::writeClasses(OutputList &ol, const QByteArray &title)
{
   // write list of classes
   classSDict->writeDeclaration(ol, 0, title, false);
}

void GroupDef::writeInlineClasses(OutputList &ol)
{
   classSDict->writeDocumentation(ol);
}

void GroupDef::writePageDocumentation(OutputList &ol)
{
   for (auto pd : *pageDict) {
      if (! pd->isReference()) {
         QSharedPointer<SectionInfo> si;

         if (! pd->title().isEmpty() && ! pd->name().isEmpty() && (si = Doxygen::sectionDict->find(pd->name())) != 0) {
            ol.startSection(si->label, si->title, SectionInfo::Subsection);
            ol.docify(si->title);
            ol.endSection(si->label, SectionInfo::Subsection);
         }

         ol.startTextBlock();
         ol.generateDoc(pd->docFile(), pd->docLine(), pd, QSharedPointer<MemberDef>(), 
                        pd->documentation() + pd->inbodyDocumentation(), true, false, 0, true, false);

         ol.endTextBlock();
      }
   }
}

void GroupDef::writeMemberGroups(OutputList &ol)
{  
   QSharedPointer<GroupDef> self = sharedFrom(this);

   if (memberGroupSDict) {      
      /* write user defined member groups */
      
      for (auto mg : *memberGroupSDict) {
         mg->writeDeclarations(ol, QSharedPointer<ClassDef>(), QSharedPointer<NamespaceDef>(), 
                               QSharedPointer<FileDef>(), self);
      }
   }
}

void GroupDef::startMemberDeclarations(OutputList &ol)
{
   ol.startMemberSections();
}

void GroupDef::endMemberDeclarations(OutputList &ol)
{
   ol.endMemberSections();
}

void GroupDef::startMemberDocumentation(OutputList &ol)
{  
   if (Config::getBool("separate-member-pages")) {
      ol.pushGeneratorState();
      ol.disable(OutputGenerator::Html);
      Doxygen::suppressDocWarnings = true;
   }
}

void GroupDef::endMemberDocumentation(OutputList &ol)
{   
   if (Config::getBool("separate-member-pages")) {
      ol.popGeneratorState();
      Doxygen::suppressDocWarnings = false;
   }
}

void GroupDef::writeAuthorSection(OutputList &ol)
{
   // write Author section (Man only)
   ol.pushGeneratorState();
   ol.disableAllBut(OutputGenerator::Man);
   ol.startGroupHeader();
   ol.parseText(theTranslator->trAuthor(true, true));
   ol.endGroupHeader();
   ol.parseText(theTranslator->trGeneratedAutomatically(Config::getString("project-name").toUtf8()));
   ol.popGeneratorState();
}

void GroupDef::writeSummaryLinks(OutputList &ol)
{
   ol.pushGeneratorState();
   ol.disableAllBut(OutputGenerator::Html);

   bool first = true;
   SrcLangExt lang = getLanguage();
   
   for (auto lde : LayoutDocManager::instance().docEntries(LayoutDocManager::Group) ) {

      if ((lde->kind() == LayoutDocEntry::GroupClasses && classSDict->declVisible()) ||
            (lde->kind() == LayoutDocEntry::GroupNamespaces && namespaceSDict->declVisible()) ||
            (lde->kind() == LayoutDocEntry::GroupFiles && fileList->count() > 0) ||
            (lde->kind() == LayoutDocEntry::GroupNestedGroups && groupList->count() > 0) ||
            (lde->kind() == LayoutDocEntry::GroupDirs && dirList->count() > 0) ) {

         LayoutDocEntrySection *ls = (LayoutDocEntrySection *)lde;

         QByteArray label;

         if (lde->kind() == LayoutDocEntry::GroupClasses) {
            label = "nested-classes";

         } else if (lde->kind() == LayoutDocEntry::GroupNamespaces)  {
            label = "namespaces";

         } else if (lde->kind() == LayoutDocEntry::GroupFiles)  {
            label = "files";

         } else if (lde->kind() == LayoutDocEntry::GroupNestedGroups) {
            label = "groups"; 

         } else {
            label = "dirs";
         }

         ol.writeSummaryLink(QString(""), label, ls->title(lang), first);
         first = false;

      } else if (lde->kind() == LayoutDocEntry::MemberDecl) {

         LayoutDocEntryMemberDecl *lmd = (LayoutDocEntryMemberDecl *)lde;
         QSharedPointer<MemberList> ml = getMemberList(lmd->type);

         if (ml && ml->declVisible()) {
            ol.writeSummaryLink(QString(""), MemberList::listTypeAsString(ml->listType()), lmd->title(lang), first);
            first = false;
         }
      }
   }

   if (! first) {
      ol.writeString("  </div>\n");
   }
   ol.popGeneratorState();
}

void GroupDef::writeDocumentation(OutputList &ol)
{
   QSharedPointer<GroupDef> self = sharedFrom(this);

   // static bool generateTreeView = Config::getBool("generate-treeview");

   ol.pushGeneratorState();
   startFile(ol, getOutputFileBase(), name(), title, HLI_None);

   ol.startHeaderSection();
   writeSummaryLinks(ol);
   ol.startTitleHead(getOutputFileBase());
   ol.pushGeneratorState();
   ol.disable(OutputGenerator::Man);
   ol.parseText(title);
   ol.popGeneratorState();
   addGroupListToTitle(ol, self);
   ol.pushGeneratorState();
   ol.disable(OutputGenerator::Man);
   ol.endTitleHead(getOutputFileBase(), title);
   ol.popGeneratorState();
   ol.pushGeneratorState();
   ol.disableAllBut(OutputGenerator::Man);
   ol.endTitleHead(getOutputFileBase(), name());
   ol.parseText(title);
   ol.popGeneratorState();
   ol.endHeaderSection();
   ol.startContents();

   if (Doxygen::searchIndex) {
      Doxygen::searchIndex->setCurrentDoc(self, anchor(), false);
      static QRegExp we("[a-zA-Z_][-a-zA-Z_0-9]*");
      int i = 0, p = 0, l = 0;

      while ((i = we.indexIn(title, p)) != -1) { 
         // foreach word in the title
         l = we.matchedLength();
   
         Doxygen::searchIndex->addWord(title.mid(i, l), true);
         p = i + l;
      }
   }

   Doxygen::indexList->addIndexItem(self, QSharedPointer<MemberDef>(), 0, title);

   //---------------------------------------- start flexible part -------------------------------

   SrcLangExt lang = getLanguage();
   
   for (auto lde : LayoutDocManager::instance().docEntries(LayoutDocManager::Group)) {
      switch (lde->kind()) {
         case LayoutDocEntry::BriefDesc:
            writeBriefDescription(ol);
            break;
         case LayoutDocEntry::MemberDeclStart:
            startMemberDeclarations(ol);
            break;
         case LayoutDocEntry::GroupClasses: {
            LayoutDocEntrySection *ls = (LayoutDocEntrySection *)lde;
            writeClasses(ol, ls->title(lang));
         }
         break;
         case LayoutDocEntry::GroupInlineClasses: {
            writeInlineClasses(ol);
         }
         break;
         case LayoutDocEntry::GroupNamespaces: {
            LayoutDocEntrySection *ls = (LayoutDocEntrySection *)lde;
            writeNamespaces(ol, ls->title(lang));
         }
         break;
         case LayoutDocEntry::MemberGroups:
            writeMemberGroups(ol);
            break;
         case LayoutDocEntry::MemberDecl: {
            LayoutDocEntryMemberDecl *lmd = (LayoutDocEntryMemberDecl *)lde;
            writeMemberDeclarations(ol, lmd->type, lmd->title(lang));
         }
         break;
         case LayoutDocEntry::MemberDeclEnd:
            endMemberDeclarations(ol);
            break;
         case LayoutDocEntry::DetailedDesc: {
            LayoutDocEntrySection *ls = (LayoutDocEntrySection *)lde;
            writeDetailedDescription(ol, ls->title(lang));
         }
         break;
         case LayoutDocEntry::MemberDefStart:
            startMemberDocumentation(ol);
            break;
         case LayoutDocEntry::MemberDef: {
            LayoutDocEntryMemberDef *lmd = (LayoutDocEntryMemberDef *)lde;
            writeMemberDocumentation(ol, lmd->type, lmd->title(lang));
         }
         break;
         case LayoutDocEntry::MemberDefEnd:
            endMemberDocumentation(ol);
            break;
         case LayoutDocEntry::GroupNestedGroups: {
            LayoutDocEntrySection *ls = (LayoutDocEntrySection *)lde;
            writeNestedGroups(ol, ls->title(lang));
         }
         break;
         case LayoutDocEntry::GroupPageDocs:
            writePageDocumentation(ol);
            break;
         case LayoutDocEntry::GroupDirs: {
            LayoutDocEntrySection *ls = (LayoutDocEntrySection *)lde;
            writeDirs(ol, ls->title(lang));
         }
         break;
         case LayoutDocEntry::GroupFiles: {
            LayoutDocEntrySection *ls = (LayoutDocEntrySection *)lde;
            writeFiles(ol, ls->title(lang));
         }
         break;
         case LayoutDocEntry::GroupGraph:
            writeGroupGraph(ol);
            break;
         case LayoutDocEntry::AuthorSection:
            writeAuthorSection(ol);
            break;
         case LayoutDocEntry::ClassIncludes:
         case LayoutDocEntry::ClassInheritanceGraph:
         case LayoutDocEntry::ClassNestedClasses:
         case LayoutDocEntry::ClassCollaborationGraph:
         case LayoutDocEntry::ClassAllMembersLink:
         case LayoutDocEntry::ClassUsedFiles:
         case LayoutDocEntry::ClassInlineClasses:
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
         case LayoutDocEntry::DirSubDirs:
         case LayoutDocEntry::DirFiles:
         case LayoutDocEntry::DirGraph:
            err("Internal inconsistency: member %d should not be part of "
                "LayoutDocManager::Group entry list\n", lde->kind());
            break;
      }
   }

   //---------------------------------------- end flexible part -------------------------------

   endFile(ol);

   ol.popGeneratorState();

   if (Config::getBool("separate-member-pages")) {
      allMemberList->sort();
      writeMemberPages(ol);
   }
}

void GroupDef::writeMemberPages(OutputList &ol)
{
   QSharedPointer<GroupDef> self = sharedFrom(this);

   ol.pushGeneratorState();
   ol.disableAllBut(OutputGenerator::Html);
 
   for (auto ml : m_memberLists) {
      if (ml->listType() & MemberListType_documentationLists) {
         ml->writeDocumentationPage(ol, name(), self);
      }
   }

   ol.popGeneratorState();
}

void GroupDef::writeQuickMemberLinks(OutputList &ol, MemberDef *currentMd) const
{
   static bool createSubDirs = Config::getBool("create-subdirs");

   ol.writeString("      <div class=\"navtab\">\n");
   ol.writeString("        <table>\n");
   
   for (auto md : *allMemberList) {

      if (md->getGroupDef() == this && md->isLinkable() && !md->isEnumValue()) {
         ol.writeString("          <tr><td class=\"navtab\">");

         if (md->isLinkableInProject()) {
            if (md == currentMd) { // selected item => highlight
               ol.writeString("<a class=\"qindexHL\" ");
            } else {
               ol.writeString("<a class=\"qindex\" ");
            }

            ol.writeString("href=\"");

            if (createSubDirs) {
               ol.writeString("../../");
            }

            ol.writeString(md->getOutputFileBase() + Doxygen::htmlFileExtension.toUtf8() + "#" + md->anchor());
            ol.writeString("\">");
            ol.writeString(convertToHtml(md->localName()));
            ol.writeString("</a>");
         }
         ol.writeString("</td></tr>\n");
      }
   }

   ol.writeString("        </table>\n");
   ol.writeString("      </div>\n");
}

void addClassToGroups(QSharedPointer<Entry> root, QSharedPointer<ClassDef> cd)
{
    for (auto g : *root->groups) {
      QSharedPointer<GroupDef> gd;

      if (! g.groupname.isEmpty() && (gd = Doxygen::groupSDict->find(g.groupname))) {
         if (gd->addClass(cd)) {
            cd->makePartOfGroup(gd);
         }
      }
   }
}

void addNamespaceToGroups(QSharedPointer<Entry> root, QSharedPointer<NamespaceDef> nd)
{ 
   for (auto g : *root->groups) {
      QSharedPointer<GroupDef> gd;

      if (! g.groupname.isEmpty() && (gd = Doxygen::groupSDict->find(g.groupname))) {
         if (gd->addNamespace(nd)) {
            nd->makePartOfGroup(gd);
         }       
      }
   }
}

void addDirToGroups(QSharedPointer<Entry> root, QSharedPointer<DirDef> dd)
{
   for (auto g : *root->groups) {
       QSharedPointer<GroupDef> gd;

      if (! g.groupname.isEmpty() && (gd = Doxygen::groupSDict->find(g.groupname))) {
         gd->addDir(dd);
         dd->makePartOfGroup(gd);
      }
   }
}

void addGroupToGroups(QSharedPointer<Entry> root, QSharedPointer<GroupDef> subGroup)
{ 
   for (auto g : *root->groups) {
      QSharedPointer<GroupDef> gd;

      if (! g.groupname.isEmpty() && (gd = Doxygen::groupSDict->find(g.groupname))) {

         if (gd == subGroup) {
            warn(root->fileName, root->startLine, "Refusing to add group %s to itself", gd->name().constData());

         } else if (subGroup->findGroup(gd)) {
            warn(root->fileName, root->startLine, "Refusing to add group %s to group %s, since the latter is already a "
                 "subgroup of the former\n", subGroup->name().data(), gd->name().data());

         } else if (! gd->findGroup(subGroup)) {
            gd->addGroup(subGroup);
            subGroup->makePartOfGroup(gd);
         }
      }
   }
}

/*! Add a member to the group with the highest priority */
void addMemberToGroups(QSharedPointer<Entry> root, QSharedPointer<MemberDef> md)
{
   // Search entry's group list for group with highest pri.

   Grouping::GroupPri_t pri = Grouping::GROUPING_LOWEST;
   QSharedPointer<GroupDef> fgd;
  
   for (auto g : *root->groups) {
      QSharedPointer<GroupDef> gd;

      if (! g.groupname.isEmpty() && (gd = Doxygen::groupSDict->find(g.groupname)) && g.pri >= pri) {

         if (fgd && gd != fgd && g.pri == pri) {
            warn(root->fileName.data(), root->startLine, "Member %s found in multiple %s groups! "
                 "The member will be put in group %s, and not in group %s", 
                 md->name().data(), Grouping::getGroupPriName( pri ), gd->name().data(), fgd->name().data() );
         }

         fgd = gd;
         pri = g.pri;
      }
   }

   // put member into group defined by this entry?
   if (fgd) {
      QSharedPointer<GroupDef> mgd = md->getGroupDef();
     
      bool insertit = false;

      if (mgd == 0) {
         insertit = true;

      } else if (mgd != fgd) {
         bool moveit = false;

         // move member from one group to another if
         // - the new one has a higher priority
         // - the new entry has the same priority, but with docs where the old one had no docs

         if (md->getGroupPri() < pri) {
            moveit = true;

         } else {

            if (md->getGroupPri() == pri) {
               if (!root->doc.isEmpty() && !md->getGroupHasDocs()) {
                  moveit = true;
               } else if (!root->doc.isEmpty() && md->getGroupHasDocs()) {
                  warn(md->getGroupFileName(), md->getGroupStartLine(),
                       "Member documentation for %s found several times in %s groups!\n"
                       "%s:%d: The member will remain in group %s, and won't be put into group %s",
                       md->name().data(), Grouping::getGroupPriName( pri ),
                       root->fileName.data(), root->startLine,
                       mgd->name().data(),
                       fgd->name().data()
                      );
               }
            }
         }

         if (moveit) {          
            mgd->removeMember(md);
            insertit = true;
         }
      }

      if (insertit) {        
         bool success = fgd->insertMember(md);

         if (success) {         
            md->setGroupDef(fgd, pri, root->fileName, root->startLine, !root->doc.isEmpty());
            QSharedPointer<ClassDef> cd = md->getClassDefOfAnonymousType();

            if (cd) {
               cd->setGroupDefForAllMembers(fgd, pri, root->fileName, root->startLine, root->doc.length() != 0);
            }
         }
      }
   }
}

void addExampleToGroups(Entry *root, QSharedPointer<PageDef> eg)
{
   for (auto g : *root->groups) {
      QSharedPointer<GroupDef> gd;

      if (! g.groupname.isEmpty() && (gd = Doxygen::groupSDict->find(g.groupname))) {
         gd->addExample(eg);
         eg->makePartOfGroup(gd);        
      }
   }
}

QByteArray GroupDef::getOutputFileBase() const
{
   if (isReference()) {
      return fileName.toUtf8();

   } else {
      return convertNameToFile(fileName.toUtf8()).toUtf8();

   }
}

void GroupDef::addListReferences()
{
   QSharedPointer<GroupDef> self = sharedFrom(this);

   QList<ListItemInfo> *xrefItems = xrefListItems();
   addRefItem(xrefItems, getOutputFileBase(), theTranslator->trGroup(true, true), getOutputFileBase(), 
              name(), 0, QSharedPointer<Definition>() );
    
   for (auto mg : *memberGroupSDict) {
      mg->addListReferences(self);
   }

   for (auto ml : m_memberLists) {
      if (ml->listType() & MemberListType_documentationLists) {
         ml->addListReferences(self);
      }
   }
}

QSharedPointer<MemberList> GroupDef::createMemberList(MemberListType lt)
{    
   for (auto ml : m_memberLists) { 
      if (ml->listType() == lt) {
         return ml;
      }
   }

   // not found, create a new member list
   QSharedPointer<MemberList> ml = QMakeShared<MemberList>(lt);
   
   ml->setInGroup(true);
   m_memberLists.append(ml);
  
   return m_memberLists.last();
}

void GroupDef::addMemberToList(MemberListType lt, QSharedPointer<MemberDef> md)
{ 
   QSharedPointer<MemberList> ml = createMemberList(lt);  

   static bool sortBriefDocs  = Config::getBool("sort-brief-docs");
   static bool sortMemberDocs = Config::getBool("sort-member-docs");

   bool isSorted = false;

   if (sortBriefDocs && (ml->listType() & MemberListType_declarationLists)) {
      isSorted = true;
   } else if (sortMemberDocs && (ml->listType() & MemberListType_documentationLists)) {
      isSorted = true;
   }

   if (isSorted) {
      ml->inSort(md);
   } else {
      ml->append(md);
   } 
}

QSharedPointer<MemberList> GroupDef::getMemberList(MemberListType lt) 
{
   for (auto ml : m_memberLists) { 
      if (ml->listType() == lt) {
         return ml;
      }
   }

   return QSharedPointer<MemberList>();
}

void GroupDef::writeMemberDeclarations(OutputList &ol, MemberListType lt, const QByteArray &title)
{   
   QSharedPointer<GroupDef> self = sharedFrom(this);
   QSharedPointer<MemberList> ml = getMemberList(lt);
  
   if (ml) {
      ml->writeDeclarations(ol, QSharedPointer<ClassDef>(), QSharedPointer<NamespaceDef>(),
                            QSharedPointer<FileDef>(), self, title, 0);
   }
}

void GroupDef::writeMemberDocumentation(OutputList &ol, MemberListType lt, const QByteArray &title)
{
   QSharedPointer<GroupDef> self = sharedFrom(this);
   QSharedPointer<MemberList> ml = getMemberList(lt);

   if (ml) {
      ml->writeDocumentation(ol, name(), self, title);
   }
}

void GroupDef::removeMemberFromList(MemberListType lt, QSharedPointer<MemberDef> md)
{
    QSharedPointer<MemberList> ml = getMemberList(lt);

   if (ml) {
      ml->removeOne(md);
   }
}

bool GroupDef::isLinkableInProject() const
{
   return !isReference() && isLinkable();
}

bool GroupDef::isLinkable() const
{
   return hasUserDocumentation();
}

// let the "programming language" for a group depend on what is inserted into it.
// First item that has an associated languages determines the language for the whole group.

void GroupDef::updateLanguage(QSharedPointer<const Definition> d)
{
   if (getLanguage() == SrcLangExt_Unknown && d->getLanguage() != SrcLangExt_Unknown) {
      setLanguage(d->getLanguage());
   }
}

bool GroupDef::hasDetailedDescription() const
{
   static bool repeatBrief = Config::getBool("repeat-brief");
   return ((! briefDescription().isEmpty() && repeatBrief) || !documentation().isEmpty());
}
