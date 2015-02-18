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

#ifndef MARSHAL_H
#define MARSHAL_H

#include <QList>
#include <QFile>
#include <QByteArray>
#include <QString>

#include <store.h>
#include <section.h>

class ArgumentList;
class Entry;
class MemberList;

struct BaseInfo;
struct Grouping;
struct SectionInfo;
struct ListItemInfo;
struct BriefInfo;

#define NULL_LIST 0xffffffff

//----- marshaling function: datatype -> byte stream --------------------

void marshalInt(StorageIntf *s,         int v);
void marshalUInt(StorageIntf *s,        uint v);
void marshalUInt64(StorageIntf *s,      uint64_t v);
void marshalBool(StorageIntf *s,        bool b);
void marshalObjPointer(StorageIntf *s,  void *obj);

void marshalQByteArray(StorageIntf *s,      const QByteArray &str);
void marshalQString(StorageIntf *s,         const QString &str);

void marshalArgumentList(StorageIntf *s,    ArgumentList argList);
void marshalArgumentLists(StorageIntf *s,   QList<ArgumentList> *argLists);
void marshalBaseInfoList(StorageIntf *s,    QList<BaseInfo> *baseList);
void marshalGroupingList(StorageIntf *s,    QList<Grouping> *groups);

void marshalSectionInfoList(StorageIntf *s, QList<SectionInfo> *anchors);
void marshalItemInfoList(StorageIntf *s,    QList<ListItemInfo> *sli);

void marshalBriefInfo(StorageIntf *s,       BriefInfo *briefInfo);
void marshalMemberList(StorageIntf *s,      MemberList *ml);
void marshalEntry(StorageIntf *s,           QSharedPointer<Entry> e);
void marshalEntryTree(StorageIntf *s,       QSharedPointer<Entry> e);


// ** unmarshaling functions
int                  unmarshalInt(StorageIntf *s);
uint                 unmarshalUInt(StorageIntf *s);
uint64_t             unmarshalUInt64(StorageIntf *s);
bool                 unmarshalBool(StorageIntf *s);
void                *unmarshalObjPointer(StorageIntf *s);

QByteArray           unmarshalQByteArray(StorageIntf *s);
QByteArray           unmarshalQGString(StorageIntf *s);

ArgumentList        *unmarshalArgumentList(StorageIntf *s);

QList<ArgumentList> *unmarshalArgumentLists(StorageIntf *s);
QList<BaseInfo>      unmarshalBaseInfoList(StorageIntf *s);
QList<Grouping>     *unmarshalGroupingList(StorageIntf *s);
QList<SectionInfo> *unmarshalSectionInfoList(StorageIntf *s);
QList<ListItemInfo> *unmarshalItemInfoList(StorageIntf *s);

BriefInfo           *unmarshalBriefInfo(StorageIntf *s);
MemberList          *unmarshalMemberList(StorageIntf *s);

QSharedPointer<Entry> unmarshalEntry(StorageIntf *s);
QSharedPointer<Entry> unmarshalEntryTree(StorageIntf *s);

#endif
