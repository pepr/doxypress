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

#ifndef MEMBERGROUP_H
#define MEMBERGROUP_H

#include <QList>
#include <QTextStream>

#include <stringmap.h>
#include <types.h>

class ClassDef;
class Definition;
class FileDef;
class GroupDef;
class MemberList;
class MemberDef;
class NamespaceDef;
class OutputList;
class StorageIntf;

struct ListItemInfo;

#define DOX_NOGROUP -1

/** A class representing a group of members. */
class MemberGroup
{
 public:
   MemberGroup();
   MemberGroup(QSharedPointer<Definition> parent, int id, const char *header, const char *docs, const char *docFile);

   ~MemberGroup();

   QByteArray header() const {
      return grpHeader;
   }

   int groupId() const {
      return grpId;
   }
   void insertMember(QSharedPointer<MemberDef> md);
   void setAnchors();
   void writePlainDeclarations(OutputList &ol, QSharedPointer<ClassDef> cd, QSharedPointer<NamespaceDef> nd, 
                  QSharedPointer<FileDef> fd, QSharedPointer<GroupDef> gd,
                  QSharedPointer<ClassDef> inheritedFrom, const char *inheritId);

   void writeDeclarations(OutputList &ol, QSharedPointer<ClassDef> cd, QSharedPointer<NamespaceDef> nd, 
                  QSharedPointer<FileDef> fd, QSharedPointer<GroupDef> gd, bool showInline = false);

   void writeDocumentation(OutputList &ol, const char *scopeName, QSharedPointer<Definition> container, bool showEnumValues, bool showInline);
   void writeDocumentationPage(OutputList &ol, const char *scopeName, QSharedPointer<Definition> container);
   void writeTagFile(QTextStream &);

   void addGroupedInheritedMembers(OutputList &ol, QSharedPointer<ClassDef> cd, MemberListType lt,
                                   QSharedPointer<ClassDef> inheritedFrom, const QByteArray &inheritId);

   const QByteArray &documentation() const {
      return doc;
   }

   bool allMembersInSameSection() const {
      return inSameSection;
   }

   void addToDeclarationSection();
   int countDecMembers(QSharedPointer<GroupDef> gd = QSharedPointer<GroupDef>());
   int countDocMembers();
   int countGroupedInheritedMembers(MemberListType lt);
   void distributeMemberGroupDocumentation();
   void findSectionsInDocumentation();
   int varCount() const;
   int funcCount() const;
   int enumCount() const;
   int enumValueCount() const;
   int typedefCount() const;
   int protoCount() const;
   int defineCount() const;
   int friendCount() const;
   int numDecMembers() const;
   int numDocMembers() const;
   int countInheritableMembers(QSharedPointer<ClassDef> inheritedFrom) const;
   void setInGroup(bool b);
   void addListReferences(QSharedPointer<Definition> d);
   void setRefItems(const QList<ListItemInfo> *sli);

   QSharedPointer<MemberList> members() const {
      return memberList;
   }

   QSharedPointer<Definition> parent() const {
      return m_parent;
   }

   QByteArray anchor() const;

   void marshal(StorageIntf *s);
   void unmarshal(StorageIntf *s);

 private:
   QSharedPointer<MemberList> memberList;      // list of all members in the group
   QSharedPointer<MemberList> inDeclSection;

   int grpId;
   QByteArray grpHeader;
   QByteArray fileName;           // base name of the generated file
   QByteArray doc;
   QByteArray m_docFile;

   QSharedPointer<Definition> scope;
   QSharedPointer<Definition> m_parent;
  
   bool inSameSection;
   int  m_numDecMembers;
   int  m_numDocMembers; 
   
   QList<ListItemInfo> *m_xrefListItems;
};

/** A sorted dictionary of MemberGroup objects. */
class MemberGroupSDict : public LongMap<QSharedPointer<MemberGroup>>
{
 public:
   MemberGroupSDict() : LongMap<QSharedPointer<MemberGroup>>() {}
   ~MemberGroupSDict() {}

 private:
   int compareMapValues(const QSharedPointer<MemberGroup> &item1, const QSharedPointer<MemberGroup> &item2) const override {
      return item1->groupId() - item2->groupId();
   }
};

/** Data collected for a member group */
struct MemberGroupInfo {
   MemberGroupInfo() : m_sli(0)
   {}

   ~MemberGroupInfo() {
      delete m_sli;
      m_sli = 0;
   }

   void setRefItems(const QList<ListItemInfo> *sli);

   QByteArray header;
   QByteArray doc;
   QByteArray docFile;
   QByteArray compoundName;

   QList<ListItemInfo> *m_sli;
};

#endif
