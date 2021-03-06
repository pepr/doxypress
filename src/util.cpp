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
#include <QCache>
#include <QCryptographicHash>
#include <QDateTime>
#include <QHash>
#include <QRegExp>
#include <QTextCodec>

#include <stdlib.h>
#include <errno.h>
#include <math.h>

#include <util.h>

#include <config.h>
#include <defargs.h>
#include <doxy_globals.h>
#include <doxy_build_info.h>
#include <entry.h>
#include <example.h>
#include <htmlentity.h>
#include <image.h>
#include <language.h>
#include <message.h>
#include <textdocvisitor.h>

struct FindFileCacheElem {
   FindFileCacheElem(QSharedPointer<FileDef> fd, bool ambig)
      : fileDef(fd), isAmbig(ambig) {}

   QSharedPointer<FileDef> fileDef;
   bool isAmbig;
};

#define ENABLE_TRACINGSUPPORT 0

#if defined(_OS_MAC_) && ENABLE_TRACINGSUPPORT
#define TRACINGSUPPORT
#endif

#ifdef TRACINGSUPPORT
#include <execinfo.h>
#include <unistd.h>
#endif

const int MAX_STACK_SIZE = 1000;

static QHash<QString, QSharedPointer<MemberDef>>   s_resolvedTypedefs;
static QHash<QString, QSharedPointer<Definition>>  s_visitedNamespaces;

static QSet<QString> s_aliasesProcessed;

static QCache<QPair<const FileNameDict *, QString>, FindFileCacheElem> s_findFileDefCache;

// forward declaration
static QSharedPointer<ClassDef> getResolvedClassRec(QSharedPointer<Definition> scope, QSharedPointer<FileDef> fileScope,
                  const QString &n, QSharedPointer<MemberDef> *pTypeDef, QString *pTemplSpec, QString *pResolvedType);

static int isAccessibleFromWithExpScope(QSharedPointer<Definition> scope, QSharedPointer<FileDef> fileScope,
                  QSharedPointer<Definition> item, const QString &explicitScopePart);

// selects one of the name to sub-dir mapping algorithms that is used
// to select a sub directory when CREATE_SUBDIRS is set to YES

#define ALGO_COUNT 1
#define ALGO_CRC16 2
#define ALGO_MD5   3

// **
#define MAP_ALGO      ALGO_MD5
// #define MAP_ALGO   ALGO_COUNT
// #define MAP_ALGO   ALGO_CRC16

// debug support for matchArguments
#define DOX_MATCH
#define DOX_NOMATCH

// #define DOX_MATCH     printf("Match at line %d\n",__LINE__);
// #define DOX_NOMATCH   printf("Nomatch at line %d\n",__LINE__);

#define HEXTONUM(x) (((x)>='0' && (x)<='9') ? ((x)-'0') :       \
                     ((x)>='a' && (x)<='f') ? ((x)-'a'+10) :    \
                     ((x)>='A' && (x)<='F') ? ((x)-'A'+10) : 0)

TextGeneratorOLImpl::TextGeneratorOLImpl(OutputDocInterface &od) : m_od(od)
{
}

void TextGeneratorOLImpl::writeString(const QString &text, bool keepSpaces) const
{
   if (text.isEmpty()) {
      return;
   }

   if (keepSpaces) {
      const QChar *p  = text.constData();
      QChar c;

      while ((c = *p++) != 0) {
         if (c == ' ') {
            m_od.writeNonBreakableSpace(1);

         } else {
            m_od.docify(c);
         }
      }


   } else {
      m_od.docify(text);

   }
}

void TextGeneratorOLImpl::writeBreak(int indent) const
{
   m_od.lineBreak("typebreak");

   for (int i = 0; i < indent; i++) {
      m_od.writeNonBreakableSpace(3);
   }
}

void TextGeneratorOLImpl::writeLink(const QString &extRef, const QString &file,
                  const QString &anchor, const QString &text) const
{
   m_od.writeObjectLink(extRef, file, anchor, text);
}


// an inheritance tree of depth of 100000 should be enough
const int maxInheritanceDepth = 100000;

/*!
  Removes all anonymous scopes from string s
  Possible examples:
\verbatim
   "bla::@10::blep"      => "bla::blep"
   "bla::@10::@11::blep" => "bla::blep"
   "@10::blep"           => "blep"
   " @10::blep"          => "blep"
   "@9::@10::blep"       => "blep"
   "bla::@1"             => "bla"
   "bla::@1::@2"         => "bla"
   "bla @1"              => "bla"
\endverbatim
 */
QString removeAnonymousScopes(const QString &s)
{
   QString result;

   if (s.isEmpty()) {
      return result;
   }

   static QRegExp re("[ :]*@[0-9]+[: ]*");
   int i;
   int len;
   int sl = s.length();
   int p  = 0;

   while ((i = re.indexIn(s, p)) != -1) {
      len = re.matchedLength();

      result += s.mid(p, i - p);

      int c   = i;
      bool b1 = false;
      bool b2 = false;

      while (c < i + len && s.at(c) != '@')  {
         if (s.at(c++) == ':') {
            b1 = true;
         }
      }

      c = i + len - 1;
      while (c >= i && s.at(c) != '@')  {
         if (s.at(c--) == ':') {
            b2 = true;
         }
      }

      if (b1 && b2) {
         result += "::";
      }

      p = i + len;
   }

   result += s.right(sl - p);

   return result;
}

// replace anonymous scopes with __anonymous__ or replacement if provided
QString replaceAnonymousScopes(const QString &s, const QString&replacement)
{
   QString result;

   if (s.isEmpty()) {
      return result;
   }

   static QRegExp re("@[0-9]+");
   int i;
   int len;
   int sl = s.length();
   int p = 0;

   while ((i = re.indexIn(s, p)) != -1) {
      len = re.matchedLength();

      result += s.mid(p, i - p);

      if (! replacement.isEmpty()) {
         result += replacement;
      } else {
         result += "__anonymous__";
      }

      p = i + len;
   }

   result += s.right(sl - p);

   return result;
}

// strip anonymous left hand side part of the scope
QString stripAnonymousNamespaceScope(const QString &s)
{
   int i;
   int p = 0;
   int l;

   QString newScope;

   int sl = s.length();

   while ((i = getScopeFragment(s, p, &l)) != -1) {

      if (Doxy_Globals::namespaceSDict->find(s.left(i + l)) != 0) {

         if (s.at(i) != '@') {
            if (! newScope.isEmpty()) {
               newScope += "::";
            }

            newScope += s.mid(i, l);
         }

      } else if (i < sl) {

         if (! newScope.isEmpty()) {
            newScope += "::";
         }

         newScope += s.right(sl - i);
         break;
      }

      p = i + l;
   }

   return newScope;
}

void writePageRef(OutputDocInterface &od, const QString &cn, const QString &mn)
{
   od.pushGeneratorState();

   od.disable(OutputGenerator::Html);
   od.disable(OutputGenerator::Man);

   if (Config::getBool("latex-hyper-pdf")) {
      od.disable(OutputGenerator::Latex);
   }

   if (Config::getBool("rtf-hyperlinks")) {
      od.disable(OutputGenerator::RTF);
   }

   od.startPageRef();
   od.docify(theTranslator->trPageAbbreviation());
   od.endPageRef(cn, mn);

   od.popGeneratorState();
}

static QString stripFromPath(const QString &path, const QStringList &list)
{
   // look at all the strings in the list and strip the longest match
   QString retval = path;

   unsigned int length = 0;

   for (auto prefix : list) {

      if (prefix.length() > length) {
         if (path.startsWith(prefix, Qt::CaseInsensitive)) {
            length = prefix.length();
            retval = path.right(path.length() - prefix.length());
         }
      }
   }

   return retval;
}

/*! strip part of \a path if it matches
 *  one of the paths in the Config::getList("strip-from-path") list
 */
QString stripFromPath(const QString &path)
{
   return stripFromPath(path, Config::getList("strip-from-path"));
}

/*! strip part of \a path if it matches
 *  one of the paths in the Config::getList("include-path") list
 */
QString stripFromIncludePath(const QString &path)
{
   return stripFromPath(path, Config::getList("strip-from-inc-path"));
}

/*! check if \a fname is a source or a header file name by looking at the extension
 */
int determineSection(const QString &fname)
{
   const QStringList suffixSource = Config::getList("suffix-source-navtree");
   const QStringList suffixHeader = Config::getList("suffix-header-navtree");

   QFileInfo fi(fname);
   QString suffix = fi.suffix().toLower();

   if (suffixSource.contains(suffix) ) {
      return Entry::SOURCE_SEC;
   }

   if (suffixHeader.contains(suffix) ) {
      return Entry::HEADER_SEC;
   }

   return 0;
}

QString resolveTypeDef(QSharedPointer<Definition> context, const QString &qualifiedName,
                          QSharedPointer<Definition> *typedefContext)
{
   QString result;

   if (qualifiedName.isEmpty()) {
      return result;
   }

   QSharedPointer<Definition> mContext = context;
   if (typedefContext) {
      *typedefContext = context;
   }

   // see if the qualified name has a scope part
   int scopeIndex  = qualifiedName.lastIndexOf("::");
   QString resName = qualifiedName;

   if (scopeIndex != -1) { // strip scope part for the name
      resName = qualifiedName.right(qualifiedName.length() - scopeIndex - 2);

      if (resName.isEmpty()) {
         return result;
      }
   }

   QSharedPointer<MemberDef> md;

   while (mContext && md == 0) {
      // step 1: get the right scope
      QSharedPointer<Definition> resScope = mContext;

      if (scopeIndex != -1) {
         // split-off scope part
         QString resScopeName = qualifiedName.left(scopeIndex);

         // look-up scope in context
         int is;
         int ps = 0;
         int l;

         while ((is = getScopeFragment(resScopeName, ps, &l)) != -1) {
            QString qualScopePart = resScopeName.mid(is, l);
            QString tmp = resolveTypeDef(mContext, qualScopePart);

            if (! tmp.isEmpty()) {
               qualScopePart = tmp;
            }

            resScope = resScope->findInnerCompound(qualScopePart);

            if (resScope == 0) {
               break;
            }
            ps = is + l;
         }
      }

      // step 2: get the member
      if (resScope) {
         // no scope or scope found in the current context
         MemberNameSDict *mnd = 0;

         if (resScope->definitionType() == Definition::TypeClass) {
            mnd = Doxy_Globals::memberNameSDict;

         } else {
            mnd = Doxy_Globals::functionNameSDict;
         }

         QSharedPointer<MemberName> mn = mnd->find(resName);

         if (mn) {
            int minDist = -1;

            for (auto tmd : *mn) {

               if (tmd->isTypedef()) {
                  int dist = isAccessibleFrom(resScope, QSharedPointer<FileDef>(), tmd);

                  if (dist != -1 && (md == 0 || dist < minDist)) {
                     md = tmd;
                     minDist = dist;
                  }
               }
            }
         }
      }

      mContext = mContext->getOuterScope();
   }

   // step 3: get the member's type
   if (md) {

      result = md->typeString();
      QString args = md->argsString();

      if (args.indexOf(")(") != -1) {
         // typedef of a function/member pointer
         result += args;

      } else if (args.indexOf('[') != -1) {
         // typedef of an array
         result += args;
      }

      if (typedefContext) {
         *typedefContext = md->getOuterScope();
      }
   }

   return result;
}

/*! Get a class definition given its name.
 *  Returns 0 if the class is not found.
 */
QSharedPointer<ClassDef> getClass(const QString &name)
{
   if (name.isEmpty()) {
      return QSharedPointer<ClassDef>();
   }

   QSharedPointer<ClassDef> result = Doxy_Globals::classSDict->find(name);

   return result;
}

QSharedPointer<NamespaceDef> getResolvedNamespace(const QString &name)
{
   if (name.isEmpty()) {
      return QSharedPointer<NamespaceDef>();
   }

   QString subst = Doxy_Globals::namespaceAliasDict[name];

   if (subst.isEmpty()) {
      return Doxy_Globals::namespaceSDict->find(name);

   } else {
      int count = 0;

      // recursion detection guard
      QString newSubst;

      while ( ! (newSubst = Doxy_Globals::namespaceAliasDict[subst]).isEmpty() && count < 10) {
         subst = newSubst;
         count++;
      }

      if (count == 10) {
         warn_uncond("Possible recursive namespace alias detected for %s\n", qPrintable(name) );
      }

      return Doxy_Globals::namespaceSDict->find(subst);
   }
}


/*! Returns the class representing the value of the typedef represented by \a md
 *  within file \a fileScope.
 *
 *  Example: typedef A T; will return the class representing A if it is a class.
 *
 *  Example: typedef int T; will return 0, since "int" is not a class.
 */
QSharedPointer<ClassDef> newResolveTypedef(QSharedPointer<FileDef> fileScope, QSharedPointer<MemberDef> md,
                  QSharedPointer<MemberDef> *pMemType, QString *pTemplSpec, QString *pResolvedType,
                  ArgumentList *actTemplParams)
{
   bool isCached = md->isTypedefValCached(); // value already cached

   if (isCached) {

      if (pTemplSpec) {
         *pTemplSpec    = md->getCachedTypedefTemplSpec();
      }

      if (pResolvedType) {
         *pResolvedType = md->getCachedResolvedTypedef();
      }

      return md->getCachedTypedefVal();
   }

   QString qname = md->qualifiedName();
   if (s_resolvedTypedefs.contains(qname)) {
      return QSharedPointer<ClassDef>();   // typedef already done
   }

   // put on the trace list
   s_resolvedTypedefs.insert(qname, md);

   QSharedPointer<ClassDef> typeClass = md->getClassDef();
   QString type = md->typeString();

   // get the "value" of the typedef
   if (typeClass && typeClass->isTemplate() && actTemplParams && actTemplParams->count() > 0) {
      type = substituteTemplateArgumentsInString(type, typeClass->templateArguments(), actTemplParams);
   }

   QString typedefValue = type;
   int tl = type.length();
   int ip = tl - 1; // remove * and & at the end

   while (ip >= 0 && (type.at(ip) == '*' || type.at(ip) == '&' || type.at(ip) == ' ')) {
      ip--;
   }

   type = type.left(ip + 1);
   type = stripPrefix(type, "const ");     // strip leading "const"
   type = stripPrefix(type, "struct ");    // strip leading "struct"
   type = stripPrefix(type, "union ");     // strip leading "union"

   int sp = 0;
   tl = type.length();

   // length may have been changed
   while (sp < tl && type.at(sp) == ' ') {
      sp++;
   }

   QSharedPointer<MemberDef> memTypeDef;
   QSharedPointer<ClassDef> result = getResolvedClassRec(md->getOuterScope(), fileScope, type, &memTypeDef, 0, pResolvedType);

   // if type is a typedef then return what it resolves to.
   if (memTypeDef && memTypeDef->isTypedef()) {
      result = newResolveTypedef(fileScope, memTypeDef, pMemType, pTemplSpec);
      goto done;

   } else if (memTypeDef && memTypeDef->isEnumerate() && pMemType) {
      *pMemType = memTypeDef;
   }

   if (result == 0) {
      // try unspecialized version if type is template
      int si = type.lastIndexOf("::");
      int i  = type.indexOf('<');

      if (si == -1 && i != -1) { // typedef of a template => try the unspecialized version
         if (pTemplSpec) {
            *pTemplSpec = type.mid(i);
         }

         result = getResolvedClassRec(md->getOuterScope(), fileScope, type.left(i), 0, 0, pResolvedType);

      } else if (si != -1) { // A::B
         i = type.indexOf('<', si);

         if (i == -1) {
            // Something like A<T>::B => lookup A::B
            i = type.length();

         } else {
            // Something like A<T>::B<S> => lookup A::B, spec=<S>
            if (pTemplSpec) {
               *pTemplSpec = type.mid(i);
            }
         }

         result = getResolvedClassRec(md->getOuterScope(), fileScope, stripTemplateSpecifiersFromScope(type.left(i), false), 0, 0, pResolvedType);
      }

   }

done:
   if (pResolvedType) {
      if (result) {
         *pResolvedType = result->qualifiedName();

         if (sp > 0) {
            pResolvedType->prepend(typedefValue.left(sp));
         }

         if (ip < tl - 1) {
            pResolvedType->append(typedefValue.right(tl - ip - 1));
         }

      } else {
         *pResolvedType = typedefValue;
      }
   }

   // remember computed value for next time
   if (result && result->getDefFileName() != "<code>") {
      // this check is needed to prevent that temporary classes that are
      // introduced while parsing code fragments are being cached here.

      md->cacheTypedefVal(result, pTemplSpec ? *pTemplSpec : QString(), pResolvedType ? *pResolvedType : QString() );
   }

   // remove from the trace list
   s_resolvedTypedefs.remove(qname);

   return result;
}

/*! Substitutes a simple unqualified name within a scope. Returns the
 *  value of the typedef or name if no typedef was found.
 */
static QString substTypedef(QSharedPointer<Definition> scopeDef, QSharedPointer<FileDef> fileScope,
                  const QString &phraseName, QSharedPointer<MemberDef> *pTypeDef = nullptr)
{
   QString result = phraseName;

   if (phraseName.isEmpty()) {
      return result;
   }

   auto iter = Doxy_Globals::glossary().find(phraseName);

   if (iter == Doxy_Globals::glossary().end()) {
      // could not find a matching def
      return "";
   }

   int minDistance = 10000;    // init at "infinite"

   QSharedPointer<MemberDef> bestMatch;

   while (iter != Doxy_Globals::glossary().end() && iter.key() == phraseName)  {
      // search for the best match, only look at members

      if (iter.value()->definitionType() == Definition::TypeMember) {
         // which are also typedefs
         QSharedPointer<Definition> sharedPtr = sharedFrom(iter.value());
         QSharedPointer<MemberDef> md = sharedPtr.dynamicCast<MemberDef>();

         if (md->isTypedef()) {
            // md is a typedef, test accessibility of typedef within scope
            int distance = isAccessibleFromWithExpScope(scopeDef, fileScope, sharedPtr, "");

            if (distance != -1 && distance < minDistance) {
               // definition is accessible and a better match

               minDistance = distance;
               bestMatch = md;
            }
         }
      }

      ++iter;
   }

   if (bestMatch) {
      result = bestMatch->typeString();

      if (pTypeDef) {
         *pTypeDef = bestMatch;
      }
   }

   return result;
}

static QSharedPointer<Definition> endOfPathIsUsedClass(StringMap<QSharedPointer<Definition>> &cl, const QString &localName)
{
   for (auto cd : cl) {
      if (cd->localName() == localName) {
         return cd;
      }
   }

   return QSharedPointer<Definition>();
}

/*! Starting with scope \a start, the string \a path is interpreted as
 *  a part of a qualified scope name (e.g. A::B::C), and the scope is
 *  searched. If found the scope definition is returned, otherwise 0
 *  is returned.
 */
static QSharedPointer<Definition> followPath(QSharedPointer<Definition> start, QSharedPointer<FileDef> fileScope, const QString &path)
{
   int is;
   int ps;
   int l;

   QSharedPointer<Definition> current = start;
   ps = 0;

   // for each part of the explicit scope

   while ((is = getScopeFragment(path, ps, &l)) != -1) {
      // try to resolve the part if it is a typedef
      QSharedPointer<MemberDef> typeDef;

      QString qualScopePart = substTypedef(current, fileScope, path.mid(is, l), &typeDef);

      if (typeDef) {
         QSharedPointer<ClassDef> type = newResolveTypedef(fileScope, typeDef);

         if (type) {
            return type;
         }
      }

      QSharedPointer<Definition> next = current->findInnerCompound(qualScopePart);

      if (next == 0) {
         // failed to follow the path

         if (current->definitionType() == Definition::TypeNamespace) {

            QSharedPointer<NamespaceDef> nd = current.dynamicCast<NamespaceDef>();
            auto temp = nd->getUsedClasses();

            next = endOfPathIsUsedClass(temp, qualScopePart);

         } else if (current->definitionType() == Definition::TypeFile) {

            QSharedPointer<FileDef> fd = current.dynamicCast<FileDef>();
            auto temp = fd->getUsedClasses();

            if (temp) {
               next = endOfPathIsUsedClass(*temp, qualScopePart);
            }
         }

         current = next;

         if (current == 0) {
            break;
         }

      } else {
         // continue to follow scope
         current = next;
      }

      ps = is + l;
   }

   // path could be followed

   return current;
}

bool accessibleViaUsingClass(const StringMap<QSharedPointer<Definition>> *cl, QSharedPointer<FileDef> fileScope,
                             QSharedPointer<Definition> item, const QString &explicitScopePart = "" )
{
   if (cl) {
      // see if the class was imported via a using statement
      bool explicitScopePartEmpty = explicitScopePart.isEmpty();

      for (auto ucd : *cl) {

         QSharedPointer<Definition> sc;

         if (explicitScopePartEmpty) {
            sc = ucd;

         } else {
            sc = followPath(ucd, fileScope, explicitScopePart);

         }

         if (sc && sc == item) {
            return true;
         }
      }
   }

   return false;
}

static bool accessibleViaUsingNamespace(const NamespaceSDict *nl, QSharedPointer<FileDef> fileScope, QSharedPointer<Definition> item,
                                 const QString &explicitScopePart = "")
{
   static QSet<QString> visitedDict;

   if (nl) {
      // check used namespaces for the class

      for (auto und : *nl) {
         QSharedPointer<Definition> sc;

         if (explicitScopePart.isEmpty()) {
            sc = und;

         } else {
            sc = followPath(und, fileScope, explicitScopePart);

         }

         if (sc && item->getOuterScope() == sc) {
            return true;
         }

         QString key = und->name();

         if (! visitedDict.contains(key)) {
            visitedDict.insert(key);

            if (accessibleViaUsingNamespace(&(und->getUsedNamespaces()), fileScope, item, explicitScopePart)) {
               return true;
            }

            visitedDict.remove(key);
         }

      }
   }

   return false;
}

/** Helper class representing the stack of items considered while resolving the scope.
 */
class AccessStack
{
 public:
   AccessStack() : m_index(0)
   {}

   void push(QSharedPointer<Definition> scopeDef, QSharedPointer<FileDef> fileScope, QSharedPointer<Definition> item) {
      if (m_index < MAX_STACK_SIZE) {
         m_elements[m_index].scopeDef  = scopeDef;
         m_elements[m_index].fileScope = fileScope;
         m_elements[m_index].item      = item;
         m_index++;
      }
   }

   void push(QSharedPointer<Definition> scopeDef, QSharedPointer<FileDef> fileScope, QSharedPointer<Definition> item,
            const QString &expScope) {

      if (m_index < MAX_STACK_SIZE) {
         m_elements[m_index].scopeDef  = scopeDef;
         m_elements[m_index].fileScope = fileScope;
         m_elements[m_index].item      = item;
         m_elements[m_index].expScope  = expScope;
         m_index++;
      }
   }

   void pop() {
      if (m_index > 0) {
         m_index--;
      }
   }

   bool find(QSharedPointer<Definition> scopeDef, QSharedPointer<FileDef> fileScope, QSharedPointer<Definition> item) {

      for (int i = 0; i < m_index; i++) {
         AccessElem *e = &m_elements[i];

         if (e->scopeDef == scopeDef && e->fileScope == fileScope && e->item == item) {
            return true;
         }
      }

      return false;
   }

   bool find(QSharedPointer<Definition> scopeDef, QSharedPointer<FileDef> fileScope, QSharedPointer<Definition> item,
             const QString &expScope) {

      for (int i = 0; i < m_index; i++) {
         AccessElem *e = &m_elements[i];

         if (e->scopeDef == scopeDef && e->fileScope == fileScope && e->item == item && e->expScope == expScope) {
            return true;
         }
      }

      return false;
   }

 private:

   /** Element in the stack */
   struct AccessElem {
      QSharedPointer<Definition>  scopeDef;
      QSharedPointer<FileDef>     fileScope;
      QSharedPointer<Definition>  item;

      QString expScope;
   };

   int m_index;
   AccessElem m_elements[MAX_STACK_SIZE];
};

/* Returns the "distance" (=number of levels up) from item to scope, or -1
 * if item in not inside scope.
 */
int isAccessibleFrom(QSharedPointer<Definition> scopeDef, QSharedPointer<FileDef> fileScope, QSharedPointer<Definition> item)
{
   static AccessStack accessStack;

   if (accessStack.find(scopeDef, fileScope, item)) {
      return -1;
   }
   accessStack.push(scopeDef, fileScope, item);

   // assume we found it
   int result = 0;
   int i;

   QSharedPointer<Definition> itemScope     = item->getOuterScope();

   QSharedPointer<ClassDef>   tempPtr       = scopeDef.dynamicCast<ClassDef>();
   QSharedPointer<MemberDef>  tempItem      = item.dynamicCast<MemberDef>();
   QSharedPointer<ClassDef>   tempItemScope = item.dynamicCast<ClassDef>();

   bool memberAccessibleFromScope = (item->definitionType() == Definition::TypeMember &&
       itemScope && itemScope->definitionType() == Definition::TypeClass  &&
       scopeDef->definitionType() == Definition::TypeClass && tempPtr->isAccessibleMember(tempItem));

   bool nestedClassInsideBaseClass = (item->definitionType() == Definition::TypeClass &&
       itemScope && itemScope->definitionType() == Definition::TypeClass &&
       scopeDef->definitionType() == Definition::TypeClass && tempPtr->isBaseClass(tempItemScope, true));

   if (itemScope == scopeDef || memberAccessibleFromScope || nestedClassInsideBaseClass) {

      if (nestedClassInsideBaseClass) {
         // penalty for base class to prevent
         result++;
      }

      // this is preferred over nested class in this class

   } else if (scopeDef == Doxy_Globals::globalScope) {

      if (fileScope) {
         StringMap<QSharedPointer<Definition>> *cl = fileScope->getUsedClasses();

         if (accessibleViaUsingClass(cl, fileScope, item)) {
            // found via used class
            goto done;
         }

         NamespaceSDict *nl = fileScope->getUsedNamespaces();

         if (accessibleViaUsingNamespace(nl, fileScope, item)) {
            // found via used namespace
            goto done;
         }
      }

      result = -1; // not found in path to globalScope

   } else {
      // keep searching, check if scope is a namespace, which is using other classes and namespaces

      if (scopeDef->definitionType() == Definition::TypeNamespace) {
         QSharedPointer<NamespaceDef> nscope = scopeDef.dynamicCast<NamespaceDef>();

         StringMap<QSharedPointer<Definition>> cl = nscope->getUsedClasses();

         if (accessibleViaUsingClass(&cl, fileScope, item)) {
            goto done;
         }

         NamespaceSDict nl = nscope->getUsedNamespaces();

         if (accessibleViaUsingNamespace(&nl, fileScope, item)) {
            goto done;
         }
      }

      // repeat for the parent scope
      i = isAccessibleFrom(scopeDef->getOuterScope(), fileScope, item);
      result = (i == -1) ? -1 : i + 2;
   }

done:
   accessStack.pop();

   return result;
}


/* Returns the "distance" (=number of levels up) from item to scope, or -1
 * if item in not in this scope. The explicitScopePart limits the search
 * to scopes that match \a scope (or its parent scope(s)) plus the explicit part.
 * Example:
 *
 * class A { public: class I {}; };
 * class B { public: class J {}; };
 *
 * - Looking for item=='J' inside scope=='B' will return 0.
 * - Looking for item=='I' inside scope=='B' will return -1
 *   (as it is not found in B nor in the global scope).
 * - Looking for item=='A::I' inside scope=='B', first the match B::A::I is tried but
 *   not found and then A::I is searched in the global scope, which matches and
 *   thus the result is 1.
 */
int isAccessibleFromWithExpScope(QSharedPointer<Definition> scopeDef, QSharedPointer<FileDef> fileScope,
                                 QSharedPointer<Definition> item, const QString &explicitScopePart)
{
   if (explicitScopePart.isEmpty()) {
      // handle degenerate case where there is no explicit scope
      return isAccessibleFrom(scopeDef, fileScope, item);
   }

   static AccessStack accessStack;
   if (accessStack.find(scopeDef, fileScope, item, explicitScopePart)) {
      return -1;
   }

   accessStack.push(scopeDef, fileScope, item, explicitScopePart);

   // assume we found it
   int result = 0;

   QSharedPointer<Definition> newScope = followPath(scopeDef, fileScope, explicitScopePart);

   if (newScope) {
      // explicitScope is inside scope => newScope is the result
      QSharedPointer<Definition> itemScope = item->getOuterScope();

      if (itemScope == newScope) {
         // exact match of scopes => distance == 0

      } else if (itemScope && newScope && itemScope->definitionType() == Definition::TypeClass &&
                 newScope->definitionType() == Definition::TypeClass &&
                 (newScope.dynamicCast<ClassDef>())->isBaseClass( (itemScope.dynamicCast<ClassDef>()), true, 0) )  {

         // inheritance is also ok. Example: looking for B::I, where
         // class A { public: class I {} };
         // class B : public A {}
         // but looking for B::I, where
         // class A { public: class I {} };
         // class B { public: class I {} };
         // will find A::I, so we still prefer a direct match and give this one a distance of 1

         result = 1;

      } else {
         int i = -1;

         if (newScope->definitionType() == Definition::TypeNamespace) {
            s_visitedNamespaces.insert(newScope->name(), newScope);

            // this part deals with the case where item is a class
            // A::B::C but is explicit referenced as A::C, where B is imported
            // in A via a using directive.

            QSharedPointer<NamespaceDef> nscope = newScope.dynamicCast<NamespaceDef>();
            StringMap<QSharedPointer<Definition>> cl = nscope->getUsedClasses();

            for (auto cd : cl) {
               if (cd == item) {
                  goto done;
               }
            }

            NamespaceSDict nl = nscope->getUsedNamespaces();

            for (auto nd : nl) {
               if (! s_visitedNamespaces.contains(nd->name())) {
                  i = isAccessibleFromWithExpScope(scopeDef, fileScope, item, nd->name());
                  if (i != -1) {
                     goto done;
                  }
               }
            }

         }

         // repeat for the parent scope
         if (scopeDef != Doxy_Globals::globalScope) {
            i = isAccessibleFromWithExpScope(scopeDef->getOuterScope(), fileScope, item, explicitScopePart);
         }

         result = (i == -1) ? -1 : i + 2;
      }

   } else {
      // failed to resolve explicitScope

      if (scopeDef->definitionType() == Definition::TypeNamespace) {
         QSharedPointer<NamespaceDef> nscope = scopeDef.dynamicCast<NamespaceDef>();
         NamespaceSDict nl = nscope->getUsedNamespaces();

         if (accessibleViaUsingNamespace(&nl, fileScope, item, explicitScopePart)) {
            // found in used namespace
            goto done;
         }
      }

      if (scopeDef == Doxy_Globals::globalScope) {

         if (fileScope) {
            NamespaceSDict *nl = fileScope->getUsedNamespaces();

            if (accessibleViaUsingNamespace(nl, fileScope, item, explicitScopePart)) {
               // found in used namespace
               goto done;
            }
         }

         // not found
         result = -1;

      } else {
         // continue by looking into the parent scope
         int i = isAccessibleFromWithExpScope(scopeDef->getOuterScope(), fileScope, item, explicitScopePart);

         result = (i == -1) ? -1 : i + 2;
      }
   }

done:
   accessStack.pop();
   return result;
}

int computeQualifiedIndex(const QString &name)
{
   int i = name.indexOf('<');

   if (i == -1) {
      i = name.length() - 1;
   }

   return name.lastIndexOf("::", i);
}

static void getResolvedSymbol(QSharedPointer<Definition> scope, QSharedPointer<FileDef> fileScope, QSharedPointer<Definition> def,
                  const QString &explicitScopePart, ArgumentList *actTemplParams, int &minDistance,
                  QSharedPointer<ClassDef> &bestMatch, QSharedPointer<MemberDef> &bestTypedef,
                  QString &bestTemplSpec, QString &bestResolvedType)
{
   // only look at classes and members which are enums or typedefs

   bool isClass  = (def->definitionType() == Definition::TypeClass);
   bool isMember = (def->definitionType() == Definition::TypeMember);

   QSharedPointer<MemberDef> md;
   if (isMember) {
      md = def.dynamicCast<MemberDef>();
   }

   if (isClass || (isMember && (md->isTypedef() || md->isEnumerate())) )  {

      s_visitedNamespaces.clear();

      // test accessibility of definition within scope.
      int distance = isAccessibleFromWithExpScope(scope, fileScope, def, explicitScopePart);

      if (distance != -1) {
         // definition is accessible, see if we are dealing with a class or a typedef

         if (def->definitionType() == Definition::TypeClass) {
            // def is a class
            QSharedPointer<ClassDef> cd = def.dynamicCast<ClassDef>();

            if (! cd->isTemplateArgument()) {
               // skip classes whice are only there to  represent a template argument

               if (distance < minDistance) {
                  // found a definition that is "closer"

                  minDistance = distance;
                  bestMatch   = cd;
                  bestTypedef = QSharedPointer<MemberDef>();
                  bestTemplSpec.resize(0);
                  bestResolvedType = cd->qualifiedName();

               } else if (distance == minDistance && fileScope && bestMatch && fileScope->getUsedNamespaces() &&
                          def->getOuterScope()->definitionType() == Definition::TypeNamespace &&
                          bestMatch->getOuterScope() == Doxy_Globals::globalScope) {

                  // in case the distance is equal it could be that a class X
                  // is defined in a namespace and in the global scope. When searched
                  // in the global scope the distance is 0 in both cases. We have
                  // to choose one of the definitions: we choose the one in the
                  // namespace if the fileScope imports namespaces and the definition
                  // found was in a namespace while the best match so far isn't.
                  // Just a non-perfect heuristic but it could help in some situations (kdecore code is an example)

                  minDistance = distance;
                  bestMatch   = cd;
                  bestTypedef = QSharedPointer<MemberDef>();
                  bestTemplSpec.resize(0);
                  bestResolvedType = cd->qualifiedName();
               }

            } else {

            }

         } else if (def->definitionType() == Definition::TypeMember) {

            if (md->isTypedef()) {
               // def is a typedef
               QString args = md->argsString();

               if (args.isEmpty()) {
                  // do not expand "typedef t a[4];"

                  // we found a phrase at this distance, but if it did not resolve to a class,
                  // we still have to make sure that something at a greater distance does not
                  // match, since that phrase is hidden by this one.

                  if (distance < minDistance) {
                     QString spec;
                     QString type;

                     minDistance = distance;

                     QSharedPointer<MemberDef> enumType;
                     QSharedPointer<ClassDef> cd = newResolveTypedef(fileScope, md, &enumType, &spec, &type, actTemplParams);

                     if (cd) {
                        // type resolves to a class

                        bestMatch     = cd;
                        bestTypedef   = md;
                        bestTemplSpec = spec;
                        bestResolvedType = type;

                     } else if (enumType) {
                        // type resolves to a enum

                        bestMatch     = QSharedPointer<ClassDef>();
                        bestTypedef   = enumType;
                        bestTemplSpec = "";
                        bestResolvedType = enumType->qualifiedName();

                     } else if (md->isReference()) {
                        // external reference

                        bestMatch     = QSharedPointer<ClassDef>();
                        bestTypedef   = md;
                        bestTemplSpec = spec;
                        bestResolvedType = type;

                     } else {
                        // no match

                        bestMatch = QSharedPointer<ClassDef>();
                        bestTypedef = md;
                        bestTemplSpec.resize(0);
                        bestResolvedType.resize(0);

                     }
                  }

               } else {
                  // not a simple typedef
               }

            } else if (md->isEnumerate()) {
               if (distance < minDistance) {
                  minDistance   = distance;
                  bestMatch     = QSharedPointer<ClassDef>();
                  bestTypedef   = md;
                  bestTemplSpec = "";
                  bestResolvedType = md->qualifiedName();
               }
            }
         }

      } else {
         //  mot accessible

      }
   }
}

/* Find the fully qualified class name referred to by the input class or typedef name in the input scope
 * Loops through scope and each of its parent scopes looking for a match with the input name
 * Can recursively call itself when resolving typedefs
 */
static QSharedPointer<ClassDef> getResolvedClassRec(QSharedPointer<Definition> scope, QSharedPointer<FileDef> fileScope,
                  const QString &nameType, QSharedPointer<MemberDef> *pTypeDef, QString *pTemplSpec, QString *pResolvedType )
{
   QString name;
   QString explicitScopePart;
   QString strippedTemplateParams;

   name = stripTemplateSpecifiersFromScope(removeRedundantWhiteSpace(nameType), true, &strippedTemplateParams);

   ArgumentList actTemplParams;

   if (! strippedTemplateParams.isEmpty()) {
      // template part that was stripped
      stringToArgumentList(strippedTemplateParams, &actTemplParams);
   }

   int qualifierIndex = computeQualifiedIndex(name);

   if (qualifierIndex != -1) {
      // qualified name, split off the explicit scope part
      explicitScopePart = name.left(qualifierIndex);

      // todo: improve namespace alias substitution
      replaceNamespaceAliases(explicitScopePart, explicitScopePart.length());
      name = name.mid(qualifierIndex + 2);
   }

   if (name.isEmpty()) {
      return QSharedPointer<ClassDef>();
   }

   if (! Doxy_Globals::glossary().contains(name)) {
      // -p (for ObjC protocols)

      if (! Doxy_Globals::glossary().contains(name + "-p")) {
         return QSharedPointer<ClassDef>();
      }

   }

   bool hasUsingStatements = (fileScope && ((fileScope->getUsedNamespaces() &&
                  fileScope->getUsedNamespaces()->count() > 0) ||
                  (fileScope->getUsedClasses() && fileScope->getUsedClasses()->count() > 0)) );

   // Since it is often the case that the same name is searched in the same
   // scope over an over again (especially for the linked source code generation)
   // we use a cache to collect previous results. This is possible since the
   // result of a lookup is deterministic. As the key we use the concatenated
   // scope, the name to search for and the explicit scope prefix. The speedup
   // achieved by this simple cache can be enormous.

   int scopeNameLen = scope->name().length() + 1;
   int nameLen = name.length() + 1;
   int explicitPartLen = explicitScopePart.length();

   int fileScopeLen;

   if (hasUsingStatements) {
       fileScopeLen = 1 + fileScope->getFilePath().length();

   } else {
      fileScopeLen = 0;

   }

   QString key = scope->name( ) + "+" + name + "+" + explicitScopePart;

   // if a file scope is given and contains using statements we should also use the file part
   // in the key (as a class name can be in two different namespaces and a using statement in
   // a file can select one of them).

   if (hasUsingStatements) {
      key += "+" + fileScope->name();
   }

   LookupInfo *pval = Doxy_Globals::lookupCache->object(key);

   if (pval) {

      if (pTemplSpec) {
         *pTemplSpec = pval->templSpec;
      }

      if (pTypeDef) {
         *pTypeDef = pval->typeDef;
      }

      if (pResolvedType) {
         *pResolvedType = pval->resolvedType;
      }

      return pval->classDef;

   } else {
      // not found, we already add a 0 to avoid the possibility of endless recursion
      Doxy_Globals::lookupCache->insert(key, new LookupInfo);
   }

   QSharedPointer<ClassDef> bestMatch;
   QSharedPointer<MemberDef> bestTypedef;

   QString bestTemplSpec;
   QString bestResolvedType;

   // init at "infinite"
   int minDistance = 10000;

   auto iter = Doxy_Globals::glossary().find(name);

   while (iter != Doxy_Globals::glossary().end() && iter.key() == name)  {
      QSharedPointer<Definition> def = sharedFrom(iter.value());

      getResolvedSymbol(scope, fileScope, def, explicitScopePart, &actTemplParams,
                        minDistance, bestMatch, bestTypedef, bestTemplSpec, bestResolvedType);

      ++iter;
   }

   if (pTypeDef) {
      *pTypeDef = bestTypedef;
   }

   if (pTemplSpec) {
      *pTemplSpec = bestTemplSpec;
   }

   if (pResolvedType) {
      *pResolvedType = bestResolvedType;
   }

   pval = Doxy_Globals::lookupCache->object(key);

   if (pval) {
      pval->classDef     = bestMatch;
      pval->typeDef      = bestTypedef;
      pval->templSpec    = bestTemplSpec;
      pval->resolvedType = bestResolvedType;

   } else {
      Doxy_Globals::lookupCache->insert(key, new LookupInfo(bestMatch, bestTypedef, bestTemplSpec, bestResolvedType));

   }

   return bestMatch;
}

/* Find the fully qualified class name referred to by the input class
 * or typedef name against the input scope.
 * Loops through scope and each of its parent scopes looking for a
 * match against the input name.
 */
QSharedPointer<ClassDef> getResolvedClass(QSharedPointer<Definition> scope, QSharedPointer<FileDef> fileScope, const QString &key,
                     QSharedPointer<MemberDef> *pTypeDef, QString *pTemplSpec, bool mayBeUnlinkable, bool mayBeHidden, QString *pResolvedType)
{
   s_resolvedTypedefs.clear();

   if (scope == 0 || (scope->definitionType() != Definition::TypeClass && scope->definitionType() != Definition::TypeNamespace ) ||
            (scope->getLanguage() == SrcLangExt_Java && key.contains("::")) ) {
      scope = Doxy_Globals::globalScope;
   }

   QSharedPointer<ClassDef>result;

   result = getResolvedClassRec(scope, fileScope, key, pTypeDef, pTemplSpec, pResolvedType);

   if (result == 0)  {
      // for nested classes imported via tag files, the scope may not
      // present, so we check the class name directly as well. See also bug701314
      result = getClass(key);
   }

   if (! mayBeUnlinkable && result && ! result->isLinkable()) {
      if (! mayBeHidden || ! result->isHidden()) {
         result = QSharedPointer<ClassDef>();  // do not link to artificial/hidden classes unless explicitly allowed
      }
   }

   return result;
}

static bool findOperator(const QString &s, int i)
{
   int b = s.lastIndexOf("operator", i);

   if (b == -1) {
      return false;   // not found
   }

   b += 8;

   while (b < i)  {
      // check if there are only spaces in between
      // the operator and the >

      if (! s.at(b).isSpace() ) {
         return false;
      }

      b++;
   }

   return true;
}

static bool findOperator2(const QString &s, int i)
{
   int b = s.lastIndexOf("operator", i);

   if (b == -1) {
      return false;   // not found
   }

   b += 8;

   while (b < i)  {
      // check if there are only non-ascii
      // characters in front of the operator

      if ( isId( s.at(b)) ) {
         return false;
      }

      b++;
   }

   return true;
}

static const char constScope[]   = { 'c', 'o', 'n', 's', 't', ':' };
static const char virtualScope[] = { 'v', 'i', 'r', 't', 'u', 'a', 'l', ':' };

// note: this function is not reentrant due to the use of a static buffer
QString removeRedundantWhiteSpace(const QString &str)
{
   if (str.isEmpty()) {
      return str;
   }

   static bool cliSupport = Config::getBool("cpp-cli-support");

   QString retval;
   QChar c;

   uint len = str.length();
   uint csp = 0;
   uint vsp = 0;

   for (uint i = 0; i < len; i++) {
      c = str.at(i);

      // search for "const"
      if (csp < 6 && c == constScope[csp] && (csp > 0 || i == 0  || ! isId(str.at(i - 1))) ) {
         // character matches substring "const", if it is the first character, the previous may not be a digit
         csp++;

      } else {
         // reset counter
         csp = 0;
      }

      // search for "virtual"
      if (vsp < 8 && c == virtualScope[vsp] && (vsp > 0 || i == 0  || ! isId(str.at(i - 1))) ) {
         // character matches substring "virtual", it is the first character, the previous may not be a digit
         vsp++;

      } else {
         // reset counter
         vsp = 0;

      }

      if (c == '"') {
         // quoted string
         bool isDone = false;

         i++;
         retval += c;

         while (i < len) {
            QChar cc = str.at(i);
            retval += cc;

            if (cc == '\\') {
               // escaped character
               retval += str.at(i + 1);
               i += 2;

            } else if (cc == '"') {
               // end of string
               isDone = true;
               break;

            } else {
               // any other character
               i++;

            }
         }

         if (isDone) {
            continue;
         }

      // current char is a <
      } else if (i < len - 2 && c == '<' &&
                 (isId(str.at(i + 1)) || str.at(i + 1).isSpace()) && (i < 8 || ! findOperator(str, i)) ) {

         // string in front is not "operator"
         retval += '<';
         retval += ' ';

      // current char is a >
      } else if (i > 0 && c == '>' &&
                 (isId(str.at(i - 1)) || str.at(i - 1).isSpace() || str.at(i - 1) == '*'
                      || str.at(i - 1) == '&' || str.at(i-1)=='.') && (i < 8 || ! findOperator(str, i)) ) {

         // prev char is an id char or space
         retval += ' ';
         retval += '>';

      // current char is a comma
      } else if (i > 0 && c == ',' && ! str.at(i - 1).isSpace() &&
                  ((i < len - 1 && (isId(str.at(i + 1)) || str.at(i + 1) == '['))
                     || (i < len - 2 && str.at(i + 1) == '$' && isId(str.at(i + 2)))
                     || (i < len - 3 && str.at(i + 1) == '&' && str.at(i + 2) == '$' && isId(str.at(i + 3))))) {

         // for PHP
         retval += ',';
         retval += ' ';

      } else if (i > 0 && ( (str.at(i - 1) == ')' && isId(c)) || (c == '\'' && str.at(i -1) == ' ')  ||
                  (i > 1 && str.at(i - 2) == ' ' && str.at(i - 1) == ' ') )) {

         retval += ' ';
         retval += c;

         // fix spacing "var = 15"
         if (c == '=')  {
            retval += ' ';
         }

      } else if (c == 't' && csp == 5  &&  i < len -1  &&  ! (isId(str.at(i + 1)) ||
                   str.at(i + 1) == ')' || str.at(i + 1) == ',' ) )  {

         // prevent const ::A from being converted to const::A

         retval += 't';
         retval += ' ';

         if (str.at(i + 1) == ' ') {
            i++;
         }

         csp = 0;

      } else if (c == ':' && csp == 6)  {
         // replace const::A by const ::A

         retval += ' ';
         retval += ':';
         csp = 0;

      } else if (c == 'l' && vsp == 7 && i < len -1 && ! (isId(str.at(i + 1)) || str.at(i + 1) == ')' || str.at(i + 1) == ',')  ) {
         // prevent virtual ::A from being converted to virtual::A

         retval += 'l';
         retval += ' ';

         if (str.at(i + 1) == ' ') {
            i++;
         }

         vsp = 0;

      } else if (c == ':' && vsp == 8)  {
         // replace virtual::A by virtual ::A

         retval += ' ';
         retval += ':';

         vsp = 0;

      } else if (! c.isSpace() ||
                 ( i > 0 && i < len - 1 &&
                   (isId(str.at(i - 1)) || str.at(i - 1) == ')' || str.at(i - 1) == ',' || str.at(i - 1) == '>' || str.at(i - 1) == ']') &&
                   (isId(str.at(i + 1)) ||
                    (i < len - 2 && str.at(i + 1) == '$' && isId(str.at(i + 2))) ||
                    (i < len - 3 && str.at(i + 1) == '&' && str.at(i + 2) == '$' && isId(str.at(i + 3))) ))) {

         if (c == '\t') {
            c = ' ';
         }

         if (c == '*' || c == '&' || c == '@' || c == '$') {
            uint rl = retval.length();

            if ((rl > 0 && (isId(retval.at(rl - 1)) || retval.at(rl - 1) == '>')) &&
                  ((c != '*' && c != '&') || ! findOperator2(str, i)) ) {

               // avoid splitting operator* and operator->* and operator&
               retval += ' ';

            } else if (c == '&' && (str.at(i-1) == ' ' || str.at(i-1) == ')') ) {
               retval += ' ';

            }

         } else if (c == '-') {
            uint rl = retval.length();

            if (rl > 0 && retval.at(rl - 1) == ')' && i < len - 1 && str.at(i + 1) == '>') {
               // trailing return type ')->' => ') ->'
               retval += ' ';
            }

         } else if (c == '=')  {
            // deals with "= delete"
            retval += ' ';

         }

         retval += c;

         if (c == '=')  {
            // deals with "= delete"
            retval += ' ';
         }

         if (cliSupport && (c == '^' || c == '%') && i > 1 && isId(str.at(i - 1)) && ! findOperator(str, i) ) {
            // C++ or CLI: Type^ name and Type% name
            retval += ' ';
         }
      }
   }

   return retval;
}

/**
 * Returns the position in the string where a function parameter list
 * begins, or -1 if one is not found.
 */
int findParameterList(const QString &name)
{
   int pos = -1;
   int templateDepth = 0;

   do {
      if (templateDepth > 0) {
         int nextOpenPos  = name.lastIndexOf('>', pos);
         int nextClosePos = name.lastIndexOf('<', pos);

         if (nextOpenPos != -1 && nextOpenPos > nextClosePos) {
            ++templateDepth;
            pos = nextOpenPos - 1;

         } else if (nextClosePos != -1) {
            --templateDepth;
            pos = nextClosePos - 1;

         } else { // more >'s than <'s, see bug701295
            return -1;
         }

      } else {
         int lastAnglePos = name.lastIndexOf('>', pos);
         int bracePos = name.lastIndexOf('(', pos);

         if (lastAnglePos != -1 && lastAnglePos > bracePos) {
            ++templateDepth;
            pos = lastAnglePos - 1;

         } else {
            int bp = bracePos > 0 ? name.lastIndexOf('(', bracePos - 1) : -1;

            // bp test is to allow foo(int(&)[10]), but we need to make an exception for operator()
            return bp == -1 || (bp >= 8 && name.mid(bp - 8, 10) == "operator()") ? bracePos : bp;
         }
      }

   } while (pos != -1);

   return -1;
}

bool rightScopeMatch(const QString &scope, const QString &name)
{
   int sl = scope.length();
   int nl = name.length();

   return (name == scope || // equal
           (scope.right(nl) == name && // substring
            sl - nl > 1 && scope.at(sl - nl - 1) == ':' && scope.at(sl - nl - 2) == ':'));
}

bool leftScopeMatch(const QString &scope, const QString &name)
{
   int sl = scope.length();
   int nl = name.length();

   return (name == scope || // equal
           (scope.left(nl) == name && // substring
            sl > nl + 1 && scope.at(nl) == ':' && scope.at(nl + 1) == ':') );
}

void linkifyText(const TextGeneratorIntf &out, QSharedPointer<Definition> scope, QSharedPointer<FileDef> fileScope,
                  QSharedPointer<Definition> def, const QString &text, bool autoBreak, bool external,
                  bool keepSpaces, int indentLevel)
{
   int strLen = text.length();

   if (strLen == 0) {
      return;
   }

   static QRegExp regExp("[a-z_A-Z\\x80-\\xFF][~!a-z_A-Z0-9$\\\\.:\\x80-\\xFF]*");
   static QRegExp regExpSplit(",");

   int matchLen;
   int index = 0;
   int newIndex;
   int skipIndex = 0;
   int floatingIndex = 0;

   // read a word from the text string
   while ((newIndex = regExp.indexIn(text, index)) != -1 && (newIndex == 0 ||
             ! (text.at(newIndex - 1) >= '0' && text.at(newIndex - 1) <= '9')) ) {

      // avoid matching part of hex numbers
      // add non-word part to the result

      matchLen = regExp.matchedLength();

      floatingIndex += newIndex - skipIndex + matchLen;
      bool insideString = false;
      int i;

      for (i = index; i < newIndex; i++) {
         if (text.at(i) == '"') {
            insideString = ! insideString;
         }
      }

      if (strLen > 35 && floatingIndex > 30 && autoBreak) {
         // try to insert a split point
         QString splitText = text.mid(skipIndex, newIndex - skipIndex);

         int splitLength = splitText.length();
         int offset = 1;

         i = regExpSplit.indexIn(splitText, 0);

         if (i == -1) {
            i = splitText.indexOf('<');
            if (i != -1) {
               offset = 0;
            }
         }

         if (i == -1) {
            i = splitText.indexOf('>');
         }

         if (i == -1) {
            i = splitText.indexOf(' ');
         }

         if (i != -1) {
            // add a link-break at i in case of Html output
            out.writeString(splitText.left(i + offset), keepSpaces);
            out.writeBreak(indentLevel == 0 ? 0 : indentLevel + 1);
            out.writeString(splitText.right(splitLength - i - offset), keepSpaces);

            floatingIndex = splitLength - i - offset + matchLen;

         } else {
            out.writeString(splitText, keepSpaces);
         }

      } else {
         out.writeString(text.mid(skipIndex, newIndex - skipIndex), keepSpaces);
      }

      // get word from string
      QString word      = text.mid(newIndex, matchLen);
      QString matchWord = substitute(substitute(word, "\\", "::"), ".", "::");

      bool found = false;

      if (! insideString) {
         QSharedPointer<ClassDef>     cd;
         QSharedPointer<FileDef>      fd;
         QSharedPointer<MemberDef>    md;
         QSharedPointer<NamespaceDef> nd;
         QSharedPointer<GroupDef>     gd;
         QSharedPointer<MemberDef>    typeDef;

         cd = getResolvedClass(scope, fileScope, matchWord, &typeDef);

         if (typeDef) {
            // first look at typedef then class

            if (external ? typeDef->isLinkable() : typeDef->isLinkableInProject()) {

               if (typeDef->getOuterScope() != def) {
                  out.writeLink(typeDef->getReference(), typeDef->getOutputFileBase(), typeDef->anchor(), word);
                  found = true;
               }
            }
         }

         if (! found && (cd || (cd = getClass(matchWord)))) {

            if (external ? cd->isLinkable() : cd->isLinkableInProject()) {

               if (cd == def || (scope && cd->name() == scope->name()) ) {
                  // do not link to the current scope (added 01/2016)

               } else {
                  // add link to the result
                  out.writeLink(cd->getReference(), cd->getOutputFileBase(), cd->anchor(), word);
                  found = true;
               }
            }

         } else if ((cd = getClass(matchWord + "-p"))) {
            // search for Obj-C protocols
            // add link to the result

            if (external ? cd->isLinkable() : cd->isLinkableInProject()) {
               if (cd != def) {
                  out.writeLink(cd->getReference(), cd->getOutputFileBase(), cd->anchor(), word);
                  found = true;
               }
            }
         }

         int m = matchWord.lastIndexOf("::");
         QString scopeName;

         if (scope && (scope->definitionType() == Definition::TypeClass ||
                  scope->definitionType() == Definition::TypeNamespace) ) {

            scopeName = scope->name();

         } else if (m != -1) {
            scopeName = matchWord.left(m);
            matchWord = matchWord.mid(m + 2);
         }

         if (! found && getDefs(scopeName, matchWord, "", md, cd, fd, nd, gd)) {
            bool ok;

            if (external) {
               ok = md->isLinkable();
            } else {
               ok = md->isLinkableInProject();
            }

            if (ok) {
               if (md != def && (def == nullptr || md->name() != def->name()) ) {
                  // name check is needed for overloaded members, where getDefs returns one

                  if (word.contains("(")) {
                     // ensure word refers to a method name, (added 01/2016)

                     out.writeLink(md->getReference(), md->getOutputFileBase(), md->anchor(), word);
                     found = true;
                  }
               }
            }
         }
      }

      if (! found) {
         // add word to the result
         out.writeString(word, keepSpaces);
      }

      // set next start point in the string
      skipIndex = index = newIndex + matchLen;
   }

   out.writeString(text.right(text.length() - skipIndex), keepSpaces);
}

void writeExample(OutputList &ol, ExampleSDict *ed)
{
   QString exampleLine = theTranslator->trWriteList(ed->count());

   //bool latexEnabled = ol.isEnabled(OutputGenerator::Latex);
   //bool manEnabled   = ol.isEnabled(OutputGenerator::Man);
   //bool htmlEnabled  = ol.isEnabled(OutputGenerator::Html);

   QRegExp marker("@[0-9]+");
   int index = 0, newIndex, matchLen;

   auto iter = ed->begin();

   // now replace all markers in inheritLine with links to the classes
   while ((newIndex = marker.indexIn(exampleLine, index)) != -1) {
      matchLen = marker.matchedLength();

      ol.parseText(exampleLine.mid(index, newIndex - index));
      QSharedPointer<Example> e = *iter;

      if (e) {
         ol.pushGeneratorState();

         ol.disable(OutputGenerator::Latex);
         ol.disable(OutputGenerator::RTF);

         // link for Html / man
         ol.writeObjectLink(0, e->file, e->anchor, e->name);
         ol.popGeneratorState();

         ol.pushGeneratorState();

         ol.disable(OutputGenerator::Man);
         ol.disable(OutputGenerator::Html);

         // link for Latex / pdf with anchor because the sources
         // are not hyperlinked (not possible with a verbatim environment)

         ol.writeObjectLink(0, e->file, 0, e->name);
         ol.popGeneratorState();
      }

      index = newIndex + matchLen;
   }

   ol.parseText(exampleLine.right(exampleLine.length() - index));
   ol.writeString(".");
}

QString argListToString(ArgumentList *al, bool useCanonicalType, bool showDefVals)
{
   QString result;

   if (al == nullptr) {
      return result;
   }

   result += "(";

   auto nextItem = al->begin();

   for (auto a : *al)  {
      ++nextItem;

      QString type1 = (useCanonicalType && ! a.canType.isEmpty()) ? a.canType : a.type;
      QString type2;

      // deal with function pointers
      int i = type1.indexOf(")(");

      if (i != -1) {
         type2 = type1.mid(i);
         type1 = type1.left(i);
      }

      if (! a.attrib.isEmpty()) {
         result += a.attrib + " ";
      }

      if (! a.name.isEmpty() || ! a.array.isEmpty()) {
         result += type1 + " " + a.name + type2 + a.array;
      } else {
         result += type1 + type2;
      }

      if (! a.defval.isEmpty() && showDefVals) {
         result += "=" + a.defval;
      }

      if (nextItem != al->end()) {
         result += ", ";
      }
   }

   result += ")";

   if (al->constSpecifier) {
      result += " const";
   }

   if (al->volatileSpecifier) {
      result += " volatile";
   }

   if (! al->trailingReturnType.isEmpty()) {
      result += " -> " + al->trailingReturnType;
   }

   if (al->pureSpecifier) {
      result += " =0";
   }

   return removeRedundantWhiteSpace(result);
}

QString tempArgListToString(const ArgumentList *al, SrcLangExt lang)
{
   QString  result;

   if (al == nullptr) {
      return result;
   }

   result = "<";

   auto nextItem = al->begin();

   for (auto a : *al)  {
      ++nextItem;

      if (! a.name.isEmpty()) {
         // add template argument name

         if (a.type.left(4) == "out") {
            // C# covariance
            result += "out ";

         } else if (a.type.left(3) == "in") {
            // C# contravariance
            result += "in ";
         }

         if (lang == SrcLangExt_Java || lang == SrcLangExt_CSharp) {
            result += a.type + " ";
         }

         result += a.name;

      } else {
         // extract name from type
         int i = a.type.length() - 1;

         while (i >= 0 && isId(a.type.at(i))) {
            i--;
         }

         if (i > 0) {
            result += a.type.right(a.type.length() - i - 1);

            if (a.type.contains("..."))   {
               result += "...";
            }

         } else {
            // nothing found -> take whole name
            result += a.type;
         }
      }


      if (! a.typeConstraint.isEmpty() && lang == SrcLangExt_Java) {
         // TODO: now Java specific, C# has where...

         result += " extends ";
         result += a.typeConstraint;
      }

      if (nextItem != al->end()) {
         result += ", ";
      }
   }

   result += ">";

   return removeRedundantWhiteSpace(result);
}

// compute the HTML anchors for a list of members
void setAnchors(QSharedPointer<MemberList> ml)
{
   if (ml == 0) {
      return;
   }

   for (auto md : *ml) {
      if (! md->isReference()) {
         md->setAnchor();
      }
   }
}


/*! takes the \a buf of the given length \a len and converts CR LF (DOS)
 * or CR (MAC) line ending to LF (Unix).  Returns the length of the
 * converted content (i.e. the same as \a len (Unix, MAC) or smaller (DOS).
 */
QString filterCRLF(const QString &buffer)
{
   QString retval = buffer;

   retval.replace("\r\n",   "\n");
   retval.replace(QChar(0), " ");

   return retval;
}

static QString getFilterFromList(const QString &name, const QStringList &filterList, bool &found)
{
   found = false;

   // compare the file name to the filter pattern list
   for (auto filterStr : filterList) {
      QString fs = filterStr;

      int i_equals = fs.indexOf('=');

      if (i_equals != -1) {
         QString filterPattern = fs.left(i_equals);

         QRegExp fpat(filterPattern, portable_fileSystemIsCaseSensitive(), QRegExp::Wildcard);

         if (fpat.indexIn(name) != -1) {
            // found a match
            QString filterName = fs.mid(i_equals + 1);

            if (filterName.indexOf(' ') != -1) {
               // add quotes if the name has spaces
               filterName = "\"" + filterName + "\"";
            }

            found = true;
            return filterName;
         }
      }
   }

   // no match
   return "";
}

/*! looks for a filter for the file \a name.  Returns the name of the filter
 *  if there is a match for the file name, otherwise an empty string.
 *  In case \a inSourceCode is true then first the source filter list is
 *  considered.
 */
QString getFileFilter(const QString &name, bool isSourceCode)
{
   if (name.isEmpty()) {
      return "";
   }

   const QStringList filterSrcList = Config::getList("filter-source-patterns");
   const QStringList filterList    = Config::getList("filter-patterns");

   QString filterName;
   bool found = false;

   if (isSourceCode && ! filterSrcList.isEmpty()) {
      // first look for source filter pattern list
      filterName = getFilterFromList(name, filterSrcList, found);
   }

   if (! found && filterName.isEmpty()) {
      // then look for filter pattern list
      filterName = getFilterFromList(name, filterList, found);
   }

   if (! found) {
      // then use the generic input filter
      return Config::getString("filter-program");

   } else {
      return filterName;
   }
}

QString transcodeToQString(const QByteArray &input)
{
   static QString inputEncoding = Config::getString("input-encoding");

   if (inputEncoding.isEmpty()) {
      inputEncoding = "UTF-8";
   }

   QTextCodec *temp = QTextCodec::codecForName(inputEncoding.toUtf8());

   if (! temp) {
      err("Unsupported character encoding: '%s'\n", qPrintable(inputEncoding));
      return input;
   }

   return temp->toUnicode(input);
}

/*! reads a file with name \a name and returns it as a string. If \a filter
 *  is true the file will be filtered by any user specified input filter.
 *  If \a name is "-" the string will be read from standard input.
 */
QString fileToString(const QString &name, bool filter, bool isSourceCode)
{
   if (name.isEmpty()) {
      return "";
   }

   QFile f;

   bool isFileOpened = false;

   if (name == "-") {
      // read from stdin
      isFileOpened = f.open(stdin, QIODevice::ReadOnly);

      if (isFileOpened) {

         QByteArray data;
         data = f.readAll();

         QString contents = QString::fromUtf8(data);

         contents = filterCRLF(contents);
         contents.append('\n');             // to help the scanner

         return contents;
      }

   } else {
      // read from file
      QFileInfo fi(name);

      if (! fi.exists() || ! fi.isFile()) {
         err("Unable to find file `%s'\n", qPrintable(name));
         return "";
      }

      QString fileContents;
      isFileOpened = readInputFile(name, fileContents, filter, isSourceCode);

      if (isFileOpened) {
         int s = fileContents.size();

         if (! fileContents.endsWith('\n')) {
            fileContents += '\n';
         }

         return fileContents;
      }
   }

   if (! isFileOpened) {
      err("Unable to open file `%s' for reading\n", qPrintable(name));
   }

   return "";
}

QString dateTimeHHMM()
{
   const QString format = "ddd MMM d yyyy hh:mm";
   static const QString retval = Doxy_Globals::dateTime.toString(format);

   return retval;
}

QString dateToString(bool includeTime)
{
   if (includeTime) {
      const QString format = "ddd MMM d yyyy hh:mm:ss";
      return Doxy_Globals::dateTime.toString(format);

   } else {
      const QString format = "ddd MMM d yyyy";
      return Doxy_Globals::dateTime.toString(format);

   }
}

QString yearToString()
{
   const QDate &d = QDate::currentDate();

   QString result;
   result = QString("%1").arg(d.year());

   return result;
}

// recursive function that returns the number of branches in the
// inheritance tree that the base class `bcd' is below the class `cd'
int minClassDistance(QSharedPointer<const ClassDef> cd, QSharedPointer<const ClassDef> bcd, int level)
{
   if (bcd->categoryOf())  {
      // use class that is being extended in case of, an Objective-C category

      bcd = bcd->categoryOf();
   }

   if (cd == bcd) {
      return level;
   }

   if (level == 256) {
      warn_uncond("class %s seem to have a recursive inheritance relation!\n", qPrintable(cd->name()));
      return -1;
   }

   int m = maxInheritanceDepth;

   if (cd->baseClasses()) {

      for (auto bcdi : *cd->baseClasses()) {
         int mc = minClassDistance(bcdi->classDef, bcd, level + 1);
         if (mc < m) {
            m = mc;
         }

         if (m < 0) {
            break;
         }
      }
   }

   return m;
}

Protection classInheritedProtectionLevel(QSharedPointer<ClassDef> cd, QSharedPointer<ClassDef> bcd, Protection prot, int level)
{
   if (bcd->categoryOf())  {
      // use class that is being extended in case of
      // an Objective-C category

      bcd = bcd->categoryOf();
   }

   if (cd == bcd) {
      return prot;
   }

   if (level == 256) {
      err("Internal issue found in class %s: recursive inheritance relation problem."
            "Please submit a bug report\n", qPrintable(cd->name()) );

   } else if (cd->baseClasses()) {

      for (auto bcdi : *cd->baseClasses()) {

         if (prot == Private) {
            break;
         }

         Protection baseProt = classInheritedProtectionLevel(bcdi->classDef, bcd, bcdi->prot, level + 1);

         if (baseProt == Private) {
            prot = Private;

         } else if (baseProt == Protected) {
            prot = Protected;
         }
      }
   }

   return prot;
}

#ifndef NEWMATCH
// strip any template specifiers that follow className in string s
static QString trimTemplateSpecifiers(const QString &namespaceName, const QString &className, const QString &s)
{
   QString scopeName = mergeScopes(namespaceName, className);
   QSharedPointer<ClassDef> cd = getClass(scopeName);

   if (cd == 0) {
      return s;   // should not happen, but guard anyway.
   }

   QString result = s;

   int i = className.length() - 1;

   if (i >= 0 && className.at(i) == '>') { // template specialization
      // replace unspecialized occurrences in s, with their specialized versions.
      int count = 1;
      int cl = i + 1;

      while (i >= 0) {
         QChar c = className.at(i);

         if (c == '>') {
            count++, i--;

         } else if (c == '<') {
            count--;
            if (count == 0) {
               break;
            }

         } else {
            i--;
         }
      }

      QString unspecClassName = className.left(i);
      int l = i;
      int p = 0;

      while ((i = result.indexOf(unspecClassName, p)) != -1) {
         if (result.at(i + l) != '<') { // unspecialized version

            result = result.left(i) + className + result.right(result.length() - i - l);
            l = cl;
         }
         p = i + l;
      }
   }

   QString qualName = cd->qualifiedNameWithTemplateParameters();


   // strip the template arguments following className (if any)
   if (! qualName.isEmpty()) {
      // there is a class name

      int is, ps = 0;
      int p = 0, l, i;

      while ((is = getScopeFragment(qualName, ps, &l)) != -1) {
         QString qualNamePart = qualName.right(qualName.length() - is);

         while ((i = result.indexOf(qualNamePart, p)) != -1) {
            int ql = qualNamePart.length();
            result = result.left(i) + cd->name() + result.right(result.length() - i - ql);
            p = i + cd->name().length();
         }
         ps = is + l;
      }
   }

   return result.trimmed();
}

/*!
 * @param pattern pattern to look for
 * @param s string to search in
 * @param p position to start
 * @param len resulting pattern length
 * @returns position on which string is found, or -1 if not found
 */
static int findScopePattern(const QString &pattern, const QString &s, int p, int *len)
{
   int sl = s.length();
   int pl = pattern.length();
   int sp = 0;

   *len = 0;

   while (p < sl) {
      sp = p; // start of match
      int pp = 0; // pattern position

      while (p < sl && pp < pl) {
         if (s.at(p) == '<') { // skip template arguments while matching
            int bc = 1;

            p++;

            while (p < sl) {
               if (s.at(p) == '<') {
                  bc++;

               } else if (s.at(p) == '>') {
                  bc--;
                  if (bc == 0) {
                     p++;
                     break;
                  }
               }

               p++;
            }
         } else if (s.at(p) == pattern.at(pp)) {
            p++;
            pp++;

         } else { // no match
            p = sp + 1;
            break;
         }
      }

      if (pp == pl) { // whole pattern matches
         *len = p - sp;
         return sp;
      }
   }

   return -1;
}

static QString trimScope(const QString &name, const QString &s)
{
   int scopeOffset = name.length();
   QString result = s;

   do { // for each scope
      QString tmp;
      QString scope = name.left(scopeOffset) + "::";

      int i;
      int p = 0;
      int l;

      while ((i = findScopePattern(scope, result, p, &l)) != -1) { // for each occurrence
         tmp += result.mid(p, i - p); // add part before pattern
         p = i + l;
      }

      tmp += result.right(result.length() - p); // add trailing part

      scopeOffset = name.lastIndexOf("::", scopeOffset - 1);
      result = tmp;

   } while (scopeOffset > 0);

   return result;
}
#endif

void trimBaseClassScope(SortedList<BaseClassDef *> *bcl, QString &s, int level = 0)
{
   for (auto bcd : *bcl) {
      QSharedPointer<ClassDef> cd = bcd->classDef;

      int spos = s.indexOf(cd->name() + "::");

      if (spos != -1) {
         s = s.left(spos) + s.right(
                s.length() - spos - cd->name().length() - 2
             );
      }

      if (cd->baseClasses()) {
         trimBaseClassScope(cd->baseClasses(), s, level + 1);
      }
   }
}
static void stripIrrelevantString(QString &target, const QString &str)
{
   if (target == str) {
      target.resize(0);
      return;
   }

   int i;
   int p = 0;
   int l = str.length();
   bool changed = false;

   while ((i = target.indexOf(str, p)) != -1) {
      bool isMatch = (i == 0 || ! isId(target.at(i - 1))) && // not a character before str
                     (i + l == (int)target.length() || !isId(target.at(i + l))); // not a character after str

      if (isMatch) {
         int i1 = target.indexOf('*', i + l);
         int i2 = target.indexOf('&', i + l);

         if (i1 == -1 && i2 == -1) {
            // strip str from target at index i
            target = target.left(i) + target.right(target.length() - i - l);
            changed = true;
            i -= l;

         } else if ((i1 != -1 && i < i1) || (i2 != -1 && i < i2)) { // str before * or &
            // move str to front
            target = str + " " + target.left(i) + target.right(target.length() - i - l);
            changed = true;
            i++;
         }
      }

      p = i + l;
   }

   if (changed) {
      target = target.trimmed();
   }
}

/*!
  The following example shows what is stripped by this routine
  for const. The same is done for volatile.

  \code
  const T param     ->   T param          // not relevant
  const T& param    ->   const T& param   // const needed
  T* const param    ->   T* param         // not relevant
  const T* param    ->   const T* param   // const needed
  \endcode
 */
void stripIrrelevantConstVolatile(QString &s)
{
   stripIrrelevantString(s, "const");
   stripIrrelevantString(s, "volatile");
}

#ifndef NEWMATCH
static bool matchArgument(const Argument *srcA, const Argument *dstA, const QString &className,
                          const QString &namespaceName, NamespaceSDict *usingNamespaces,
                          StringMap<QSharedPointer<Definition>> *usingClasses)
{
   // TODO: resolve any typedefs names that are part of srcA->type
   //       before matching. This should use className and namespaceName
   //       and usingNamespaces and usingClass to determine which typedefs
   //       are in-scope, so it will not be very efficient :-(

   QString srcAType = trimTemplateSpecifiers(namespaceName, className, srcA->type);
   QString dstAType = trimTemplateSpecifiers(namespaceName, className, dstA->type);
   QString srcAName = srcA->name.trimmed();
   QString dstAName = dstA->name.trimmed();

   srcAType = stripPrefix(srcAType, "class ");
   dstAType = stripPrefix(dstAType, "class ");

   // allow distinguishing "const A" from "const B" even though
   // from a syntactic point of view they would be two names of the same
   // type "const". This is not fool prove of course, but should at least
   // catch the most common cases.

   if ((srcAType == "const" || srcAType == "volatile") && ! srcAName.isEmpty()) {
      srcAType += " ";
      srcAType += srcAName;
   }

   if ((dstAType == "const" || dstAType == "volatile") && ! dstAName.isEmpty()) {
      dstAType += " ";
      dstAType += dstAName;
   }

   if (srcAName == "const" || srcAName == "volatile") {
      srcAType += srcAName;
      srcAName.resize(0);

   } else if (dstA->name == "const" || dstA->name == "volatile") {
      dstAType += dstA->name;
      dstAName.resize(0);
   }

   stripIrrelevantConstVolatile(srcAType);
   stripIrrelevantConstVolatile(dstAType);

   // strip typename keyword
   if (srcAType.startsWith("typename ")) {
      srcAType = srcAType.right(srcAType.length() - 9);
   }

   if (dstAType.startsWith("typename ")) {
      dstAType = dstAType.right(dstAType.length() - 9);
   }

   srcAType = removeRedundantWhiteSpace(srcAType);
   dstAType = removeRedundantWhiteSpace(dstAType);

   if (srcA->array != dstA->array) {
      // nomatch for char[] against char
      DOX_NOMATCH
      return false;
   }

   if (srcAType != dstAType) {
      // check if the argument only differs on name

      // remove a namespace scope that is only in one type
      // (assuming a using statement was used)

      // strip redundant scope specifiers
      if (! className.isEmpty()) {
         srcAType = trimScope(className, srcAType);
         dstAType = trimScope(className, dstAType);

         QSharedPointer<ClassDef> cd;
         if (!namespaceName.isEmpty()) {
            cd = getClass(namespaceName + "::" + className);
         } else {
            cd = getClass(className);
         }
         if (cd && cd->baseClasses()) {
            trimBaseClassScope(cd->baseClasses(), srcAType);
            trimBaseClassScope(cd->baseClasses(), dstAType);
         }
      }

      if (! namespaceName.isEmpty()) {
         srcAType = trimScope(namespaceName, srcAType);
         dstAType = trimScope(namespaceName, dstAType);
      }

      if (usingNamespaces && usingNamespaces->count() > 0) {

         for (auto nd : *usingNamespaces) {
            srcAType = trimScope(nd->name(), srcAType);
            dstAType = trimScope(nd->name(), dstAType);
         }
      }

      if (usingClasses && usingClasses->count() > 0) {
         for (auto cd : *usingClasses) {
            srcAType = trimScope(cd->name(), srcAType);
            dstAType = trimScope(cd->name(), dstAType);
         }
      }

      if (! srcAName.isEmpty() && ! dstA->type.isEmpty() && (srcAType + " " + srcAName) == dstAType) {
         DOX_MATCH
         return true;

      } else if (!dstAName.isEmpty() && !srcA->type.isEmpty() && (dstAType + " " + dstAName) == srcAType) {
         DOX_MATCH
         return true;
      }

      uint srcPos = 0;
      uint dstPos = 0;
      bool equal = true;

      while (srcPos < srcAType.length() && dstPos < dstAType.length() && equal) {
         equal = srcAType.at(srcPos) == dstAType.at(dstPos);

         if (equal) {
            srcPos++, dstPos++;
         }
      }
      uint srcATypeLen = srcAType.length();
      uint dstATypeLen = dstAType.length();
      if (srcPos < srcATypeLen && dstPos < dstATypeLen) {
         // if nothing matches or the match ends in the middle or at the
         // end of a string then there is no match
         if (srcPos == 0 || dstPos == 0) {
            DOX_NOMATCH
            return false;
         }

         if (isId(srcAType.at(srcPos)) && isId(dstAType.at(dstPos))) {
            //printf("partial match srcPos=%d dstPos=%d!\n",srcPos,dstPos);
            // check if a name if already found -> if no then there is no match
            if (!srcAName.isEmpty() || !dstAName.isEmpty()) {
               DOX_NOMATCH
               return false;
            }
            // types only
            while (srcPos < srcATypeLen && isId(srcAType.at(srcPos))) {
               srcPos++;
            }
            while (dstPos < dstATypeLen && isId(dstAType.at(dstPos))) {
               dstPos++;
            }
            if (srcPos < srcATypeLen ||
                  dstPos < dstATypeLen ||
                  (srcPos == srcATypeLen && dstPos == dstATypeLen)
               ) {
               DOX_NOMATCH
               return false;
            }
         } else {
            // otherwise we assume that a name starts at the current position.
            while (srcPos < srcATypeLen && isId(srcAType.at(srcPos))) {
               srcPos++;
            }
            while (dstPos < dstATypeLen && isId(dstAType.at(dstPos))) {
               dstPos++;
            }

            // if nothing more follows for both types then we assume we have
            // found a match. Note that now `signed int' and `signed' match, but
            // seeing that int is not a name can only be done by looking at the
            // semantics.

            if (srcPos != srcATypeLen || dstPos != dstATypeLen) {
               DOX_NOMATCH
               return false;
            }
         }

      } else if (dstPos < dstAType.length()) {
         if (! dstAType.at(dstPos).isSpace()) {
            // maybe the names differ

            if (! dstAName.isEmpty()) {
               // dst has its name separated from its type
               DOX_NOMATCH
               return false;
            }

            while (dstPos < dstAType.length() && isId(dstAType.at(dstPos))) {
               dstPos++;
            }

            if (dstPos != dstAType.length()) {
               DOX_NOMATCH
               return false; // more than a difference in name -> no match
            }

         } else {
            // maybe dst has a name while src has not
            dstPos++;

            while (dstPos < dstAType.length() && isId(dstAType.at(dstPos))) {
               dstPos++;
            }

            if (dstPos != dstAType.length() || !srcAName.isEmpty()) {
               DOX_NOMATCH
               return false; // nope not a name -> no match
            }
         }

      } else if (srcPos < srcAType.length()) {
         if (! srcAType.at(srcPos).isSpace() ) {
            // maybe the names differ

            if (! srcAName.isEmpty()) {
               // src has its name separated from its type
               DOX_NOMATCH
               return false;
            }

            while (srcPos < srcAType.length() && isId(srcAType.at(srcPos))) {
               srcPos++;
            }

            if (srcPos != srcAType.length()) {
               DOX_NOMATCH
               return false; // more than a difference in name -> no match
            }

         } else {
            // maybe src has a name while dst has not
            srcPos++;
            while (srcPos < srcAType.length() && isId(srcAType.at(srcPos))) {
               srcPos++;
            }

            if (srcPos != srcAType.length() || ! dstAName.isEmpty()) {
               DOX_NOMATCH
               return false; // nope not a name -> no match
            }
         }
      }
   }

   DOX_MATCH

   return true;
}
#endif

static QString stripDeclKeywords(const QString &s)
{
   int i = s.indexOf(" class ");
   if (i != -1) {
      return s.left(i) + s.mid(i + 6);
   }

   i = s.indexOf(" typename ");
   if (i != -1) {
      return s.left(i) + s.mid(i + 9);
   }

   i = s.indexOf(" union ");
   if (i != -1) {
      return s.left(i) + s.mid(i + 6);
   }

   i = s.indexOf(" struct ");
   if (i != -1) {
      return s.left(i) + s.mid(i + 7);
   }

   return s;
}

// forward decl for circular dependencies
static QString extractCanonicalType(QSharedPointer<Definition> d, QSharedPointer<FileDef> fs, QString type);

QString getCanonicalTemplateSpec(QSharedPointer<Definition> d, QSharedPointer<FileDef> fs, const QString &spec)
{
   QString templSpec = spec.trimmed();

   if (templSpec.startsWith("<")) {
      templSpec = "< " + extractCanonicalType(d, fs, templSpec.right(templSpec.length() - 1).trimmed());
   }

   QString resolvedType = resolveTypeDef(d, templSpec);

   if (! resolvedType.isEmpty()) { // not known as a typedef either
      templSpec = resolvedType;
   }

   return templSpec;
}

static QString getCanonicalTypeForIdentifier(QSharedPointer<Definition> d, QSharedPointer<FileDef> fs,
                  const QString &word, QString *tSpec, int count = 0)
{
   if (count > 10) {
      return word;   // recursion
   }

   QString symName;
   QString result;
   QString templSpec;
   QString tmpName;

   if (tSpec && ! tSpec->isEmpty()) {
      templSpec = stripDeclKeywords(getCanonicalTemplateSpec(d, fs, *tSpec));
   }

   if (word.lastIndexOf("::") != -1 && ! (tmpName = stripScope(word)).isEmpty()) {
      // name without scope
      symName = tmpName;
   } else {
      symName = word;
   }

   QSharedPointer<ClassDef> cd;
   QSharedPointer<MemberDef> mType;

   QString ts;
   QString resolvedType;

   // lookup class / class template instance
   cd = getResolvedClass(d, fs, word + templSpec, &mType, &ts, true, true, &resolvedType);

   bool isTemplInst = (cd && ! templSpec.isEmpty());


   if (! cd && ! templSpec.isEmpty()) {
      // class template specialization not known, look up class template
      cd = getResolvedClass(d, fs, word, &mType, &ts, true, true, &resolvedType);
   }

   if (cd && cd->isUsedOnly()) {
      // ignore types introduced by usage relations
      cd = QSharedPointer<ClassDef>();
   }

   if (cd) {
      // resolves to a known class type

      if (cd == d && tSpec) {
         *tSpec = "";
      }

      if (mType && mType->isTypedef()) { // but via a typedef
         result = resolvedType + ts; // the +ts was added for bug 685125

      } else {
         if (isTemplInst) {
            // spec is already part of class type
            templSpec = "";

            if (tSpec) {
               *tSpec = "";
            }

         } else if (!ts.isEmpty() && templSpec.isEmpty()) {
            // use formal template args for spec
            templSpec = stripDeclKeywords(getCanonicalTemplateSpec(d, fs, ts));
         }

         result = removeRedundantWhiteSpace(cd->qualifiedName() + templSpec);

         if (cd->isTemplate() && tSpec) {
            if (! templSpec.isEmpty()) {
               // specific instance
               result = cd->name() + templSpec;

            } else {
               // use template type
               result = cd->qualifiedNameWithTemplateParameters();
            }

            // template class, so remove the template part (it is part of the class name)
            *tSpec = "";

         } else if (ts.isEmpty() && !templSpec.isEmpty() && cd && !cd->isTemplate() && tSpec) {
            // obscure case where a class is used as a template, but DoxyPress thinks it is
            // not (could happen when loading the class from a tag file).
            *tSpec = "";
         }
      }

   } else if (mType && mType->isEnumerate()) {
      // an enum
      result = mType->qualifiedName();

   } else if (mType && mType->isTypedef()) {
      // a typedef

      if (word != mType->typeString()) {
         result = getCanonicalTypeForIdentifier(d, fs, mType->typeString(), tSpec, count + 1);
      } else {
         result = mType->typeString();
      }

   } else {
      // fallback
      resolvedType = resolveTypeDef(d, word);

      if (resolvedType.isEmpty()) {
         // not known as a typedef either
         result = word;
      } else {
         result = resolvedType;
      }
   }

   return result;
}

static QString extractCanonicalType(QSharedPointer<Definition> def, QSharedPointer<FileDef> fs, QString type)
{
   type = type.trimmed();

   // strip const and volatile keywords that are not relevant for the type
   stripIrrelevantConstVolatile(type);

   // strip leading keywords
   type = stripPrefix(type, "class ");
   type = stripPrefix(type, "struct ");
   type = stripPrefix(type, "union ");
   type = stripPrefix(type, "enum ");
   type = stripPrefix(type, "typename ");

   type = removeRedundantWhiteSpace(type);

   QString canType;
   QString templSpec;
   QString word;

   int i;
   int p  = 0;
   int pp = 0;

   while ((i = extractClassNameFromType(type, p, word, templSpec)) != -1) {
      // foreach identifier in the type

      if (i > pp) {
         canType += type.mid(pp, i - pp);
      }

      QString ct = getCanonicalTypeForIdentifier(def, fs, word, &templSpec);

      // in case the ct is empty it means that "word" represents scope "d"
      // and this does not need to be added to the canonical
      // type (it is redundant), so/ we skip it. This solves problem 589616.

      if (ct.isEmpty() && type.mid(p, 2) == "::") {
         p += 2;

      } else {
         canType += ct;
      }

      if (!templSpec.isEmpty())  {
         // if we did not use up the templSpec already (i.e. type is not a template specialization)
         // then resolve any identifiers inside

         static QRegExp re("[a-z_A-Z\\x80-\\xFF][a-z_A-Z0-9\\x80-\\xFF]*");
         int tp = 0;
         int tl;
         int ti;

         // for each identifier template specifier
         while ((ti = re.indexIn(templSpec, tp)) != -1) {
            tl = re.matchedLength();

            canType += templSpec.mid(tp, ti - tp);
            canType += getCanonicalTypeForIdentifier(def, fs, templSpec.mid(ti, tl), 0);
            tp = ti + tl;
         }

         canType += templSpec.right(templSpec.length() - tp);
      }

      pp = p;
   }

   canType += type.right(type.length() - pp);

   return removeRedundantWhiteSpace(canType);
}

static QString extractCanonicalArgType(QSharedPointer<Definition> d, QSharedPointer<FileDef> fs, const Argument *arg)
{
   QString type = arg->type.trimmed();
   QString name = arg->name;

   if ((type == "const" || type == "volatile") && ! name.isEmpty()) {
      // name is part of type => correct
      type += " ";
      type += name;
   }

   if (name == "const" || name == "volatile") {
      // name is part of type => correct
      if (! type.isEmpty()) {
         type += " ";
      }

      type += name;
   }

   if (! arg->array.isEmpty()) {
      type += arg->array;
   }

   return extractCanonicalType(d, fs, type);
}

static bool matchArgument2(QSharedPointer<Definition> srcScope, QSharedPointer<FileDef> srcFileScope, Argument *srcA,
                           QSharedPointer<Definition> dstScope, QSharedPointer<FileDef> dstFileScope, Argument *dstA)
{
   QString sSrcName = " " + srcA->name;
   QString sDstName = " " + dstA->name;
   QString srcType  = srcA->type;
   QString dstType  = dstA->type;

   stripIrrelevantConstVolatile(srcType);
   stripIrrelevantConstVolatile(dstType);

   if (sSrcName == dstType.right(sSrcName.length())) {
      // case "unsigned int" <-> "unsigned int i"
      srcA->type += sSrcName;
      srcA->name = "";
      srcA->canType = ""; // invalidate cached type value

   } else if (sDstName == srcType.right(sDstName.length())) {
      // case "unsigned int i" <-> "unsigned int"
      dstA->type += sDstName;
      dstA->name = "";
      dstA->canType = ""; // invalidate cached type value
   }

   if (srcA->canType.isEmpty()) {
      srcA->canType = extractCanonicalArgType(srcScope, srcFileScope, srcA);
   }


   if (dstA->canType.isEmpty()) {
      dstA->canType = extractCanonicalArgType(dstScope, dstFileScope, dstA);
   }

   if (srcA->canType == dstA->canType) {
      DOX_MATCH
      return true;

   } else {
      DOX_NOMATCH
      return false;
   }
}

// algorithm for argument matching
bool matchArguments2(QSharedPointer<Definition> srcScope, QSharedPointer<FileDef> srcFileScope, ArgumentList *srcAl,
                     QSharedPointer<Definition> dstScope, QSharedPointer<FileDef> dstFileScope, ArgumentList *dstAl, bool checkCV )
{
   assert(srcScope != 0 && dstScope != 0);

   if (srcAl == 0 || dstAl == 0) {
      // at least one of the members is not a function
      bool match = srcAl == dstAl;

      if (match) {
         DOX_MATCH
         return true;

      } else {
         DOX_NOMATCH
         return false;
      }
   }

   // handle special case with void argument
   if (srcAl->count() == 0 && dstAl->count() == 1 && dstAl->first().type == "void" ) {
      // special case for finding match between func() and func(void)
      Argument a;
      a.type = "void";

      srcAl->append(a);

      DOX_MATCH
      return true;
   }

   if (dstAl->count() == 0 && srcAl->count() == 1 && srcAl->first().type == "void" ) {
      // special case for finding match between func(void) and func()
      Argument a;
      a.type = "void";

      dstAl->append(a);

      DOX_MATCH
      return true;
   }

   if (srcAl->count() != dstAl->count()) {
      DOX_NOMATCH
      return false; // different number of arguments -> no match
   }

   if (checkCV) {
      if (srcAl->constSpecifier != dstAl->constSpecifier) {
         DOX_NOMATCH
         return false; // one member is const, the other not -> no match
      }

      if (srcAl->volatileSpecifier != dstAl->volatileSpecifier) {
         DOX_NOMATCH
         return false; // one member is volatile, the other not -> no match
      }
   }

   // so far the argument list could match, so we need to compare the types of all arguments
   auto item = dstAl->begin();

   for (auto &srcA : *srcAl ) {

      if (item == dstAl->end()) {
         return false;
      }

      if (! matchArgument2(srcScope, srcFileScope, &srcA, dstScope, dstFileScope, &(*item))) {
         DOX_NOMATCH
         return false;
      }

      ++item;
   }

   DOX_MATCH
   return true; // all arguments match
}

// merges the initializer of two argument lists
// pre:  the types of the arguments in the list should match.
void mergeArguments(ArgumentList *srcAl, ArgumentList *dstAl, bool forceNameOverwrite)
{
   if (srcAl == 0 || dstAl == 0 || srcAl->count() != dstAl->count()) {
      return; // invalid argument lists -> do not merge
   }

   auto dstA = dstAl->begin();

   for (auto &srcA : *srcAl) {

      if (srcA.defval.isEmpty() && ! dstA->defval.isEmpty()) {
         srcA.defval = dstA->defval;

      } else if (! srcA.defval.isEmpty() && dstA->defval.isEmpty()) {
         dstA->defval = srcA.defval;
      }

      // fix wrongly detected const or volatile specifiers before merging.
      // example: "const A *const" is detected as type="const A *" name="const"
      if (srcA.name == "const" || srcA.name == "volatile") {
         srcA.type += " " + srcA.name;
         srcA.name.resize(0);
      }

      if (dstA->name == "const" || dstA->name == "volatile") {
         dstA->type += " " + dstA->name;
         dstA->name.resize(0);
      }

      if (srcA.type == dstA->type) {
         if (srcA.name.isEmpty() && ! dstA->name.isEmpty()) {
            srcA.type = dstA->type;
            srcA.name = dstA->name;

         } else if (! srcA.name.isEmpty() && dstA->name.isEmpty()) {
            dstA->type = srcA.type;
            dstA->name = srcA.name;      // copperspice (code fixed)

         } else if (! srcA.name.isEmpty() && ! dstA->name.isEmpty()) {
            if (forceNameOverwrite) {
               srcA.name = dstA->name;

            } else {
               if (srcA.docs.isEmpty() && ! dstA->docs.isEmpty()) {
                  srcA.name = dstA->name;

               } else if (!srcA.docs.isEmpty() && dstA->docs.isEmpty()) {
                  dstA->name = srcA.name;
               }
            }
         }

      } else {
         srcA.type = srcA.type.trimmed();
         dstA->type = dstA->type.trimmed();

         if (srcA.type + " " + srcA.name == dstA->type) { // "unsigned long:int" <-> "unsigned long int:bla"
            srcA.type += " " + srcA.name;
            srcA.name = dstA->name;

         } else if (dstA->type + " " + dstA->name == srcA.type) { // "unsigned long int bla" <-> "unsigned long int"
            dstA->type += " " + dstA->name;
            dstA->name = srcA.name;

         } else if (srcA.name.isEmpty() && ! dstA->name.isEmpty()) {
            srcA.name = dstA->name;

         } else if (dstA->name.isEmpty() && !srcA.name.isEmpty()) {
            dstA->name = srcA.name;
         }
      }

      int i1 = srcA.type.indexOf("::"),
          i2 = dstA->type.indexOf("::"),
          j1 = srcA.type.length() - i1 - 2,
          j2 = dstA->type.length() - i2 - 2;

      if (i1 != -1 && i2 == -1 && srcA.type.right(j1) == dstA->type) {
         dstA->type = srcA.type.left(i1 + 2) + dstA->type;
         dstA->name = srcA.name;    // copperspice (code fixed)

      } else if (i1 == -1 && i2 != -1 && dstA->type.right(j2) == srcA.type) {
         srcA.type = dstA->type.left(i2 + 2) + srcA.type;
         srcA.name = dstA->name;
      }

      if (srcA.docs.isEmpty() && ! dstA->docs.isEmpty()) {
         srcA.docs = dstA->docs;

      } else if (dstA->docs.isEmpty() && !srcA.docs.isEmpty()) {
         dstA->docs = srcA.docs;
      }

      ++dstA;
   }
}

static void findMembersWithSpecificName(QSharedPointer<MemberName> mn, const QString &args, bool checkStatics, QSharedPointer<FileDef> currentFile,
                                        bool checkCV, const QString &forceTagFile, QList<QSharedPointer<MemberDef>> &members)
{
   for (auto md : *mn) {
      QSharedPointer<FileDef> fd  = md->getFileDef();
      QSharedPointer<GroupDef> gd = md->getGroupDef();

      if (((gd && gd->isLinkable()) || (fd && fd->isLinkable()) || md->isReference()) &&
         md->getNamespaceDef() == 0 && md->isLinkable() &&
         (! checkStatics || (!md->isStatic() && ! md->isDefine()) || currentFile == 0 || fd == currentFile) ) {

         // statics must appear in the same file
         bool match = true;
         ArgumentList *argList = 0;

         if (! md->isDefine() && ! args.isEmpty() && args != "()") {
            argList = new ArgumentList;
            ArgumentList *mdAl = md->argumentList();

            stringToArgumentList(args, argList);

            match = matchArguments2(md->getOuterScope(), fd, mdAl,
                       Doxy_Globals::globalScope, fd, argList,checkCV);

            delete argList;
            argList = 0;
         }

         if (match && (forceTagFile.isEmpty() || md->getReference() == forceTagFile)) {
            members.append(md);
         }
      }
   }
}

/*!
 * Searches for a member definition given its name `memberName' as a string.
 * memberName may also include a (partial) scope to indicate the scope
 * in which the member is located.
 *
 * The parameter `scName' is a string representing the name of the scope in
 * which the link was found.
 *
 * In case of a function args contains a string representation of the
 * argument list. Passing 0 means the member has no arguments.
 * Passing "()" means any argument list will do, but "()" is preferred.
 *
 * The function returns true if the member is known and documented or false if it is not.
 * If true is returned parameter `md' contains a pointer to the member definition.
 *
 * Furthermore exactly one of the parameter `cd', `nd', or `fd'
 * will be non-zero:
 *   - if `cd' is non zero, the member was found in a class pointed to by cd
 *   - if `nd' is non zero, the member was found in a namespace pointed to by nd
 *   - if `fd' is non zero, the member was found in the global namespace of file fd.
 */
bool getDefs(const QString &scName, const QString &mbName, const QString &args, QSharedPointer<MemberDef> &md,
             QSharedPointer<ClassDef> &cd, QSharedPointer<FileDef> &fd, QSharedPointer<NamespaceDef> &nd,
             QSharedPointer<GroupDef> &gd, bool forceEmptyScope, QSharedPointer<FileDef> currentFile,
             bool checkCV, const QString &forceTagFile)
{
   fd = QSharedPointer<FileDef>();
   md = QSharedPointer<MemberDef>();
   cd = QSharedPointer<ClassDef>();
   nd = QSharedPointer<NamespaceDef>();
   gd = QSharedPointer<GroupDef>();

   if (mbName.isEmpty()) {
      return false;
   }

   QString scopeName  = scName;
   QString memberName = mbName;

   scopeName  = substitute(scopeName, "\\", "::");  // for PHP
   memberName = substitute(memberName, "\\", "::"); // for PHP

   int is;
   int im = 0;
   int pm = 0;

   // strip common part of the scope from the scopeName
   while ((is = scopeName.lastIndexOf("::")) != -1 && (im = memberName.indexOf("::", pm)) != -1 &&
          (scopeName.right(scopeName.length() - is - 2) == memberName.mid(pm, im - pm))) {

      scopeName = scopeName.left(is);
      pm = im + 2;
   }

   QString mName = memberName;
   QString mScope;

   if (! memberName.startsWith("operator ") && (im = memberName.lastIndexOf("::")) != -1 &&
              im < (memberName.length() - 2) ) {

      // treat operator conversion methods as a special case, not A::

      mScope = memberName.left(im);
      mName  = memberName.right(memberName.length() - im - 2);
   }

   // handle special case where both scope name and member scope are equal
   if (mScope == scopeName) {
      scopeName = "";
   }

   QSharedPointer<MemberName> mn = Doxy_Globals::memberNameSDict->find(mName);

   if ((! forceEmptyScope || scopeName.isEmpty()) && mn && ! (scopeName.isEmpty() && mScope.isEmpty())) {
      // this was changed for bug638856, forceEmptyScope, empty scopeName

      int scopeOffset = scopeName.length();

      do {
         QString className = scopeName.left(scopeOffset);

         if (! className.isEmpty() && !mScope.isEmpty()) {
            className += "::" + mScope;

         } else if (!mScope.isEmpty()) {
            className = mScope;
         }

         QSharedPointer<MemberDef> tmd;
         QSharedPointer<ClassDef> fcd = getResolvedClass(Doxy_Globals::globalScope, QSharedPointer<FileDef>(), className, &tmd);

         if (fcd == nullptr && className.contains('<'))  {
            // try without template specifiers as well
            QString nameWithoutTemplates = stripTemplateSpecifiersFromScope(className, false);
            fcd = getResolvedClass(Doxy_Globals::globalScope, QSharedPointer<FileDef>(), nameWithoutTemplates, &tmd);
         }

         // todo: fill in correct fileScope
         if (fcd && fcd->isLinkable() ) {
            // is it a documented class

            int mdist = maxInheritanceDepth;
            ArgumentList *argList = 0;

            if (! args.isEmpty()) {
               argList = new ArgumentList;
               stringToArgumentList(args, argList);
            }

            for (auto mmd : *mn) {
               if (! mmd->isStrongEnumValue()) {
                  ArgumentList *mmdAl = mmd->argumentList();

                  bool match = (args.isEmpty() || matchArguments2(mmd->getOuterScope(), mmd->getFileDef(),
                                    mmdAl, fcd, fcd->getFileDef(), argList, checkCV));

                  if (match) {
                     QSharedPointer<ClassDef> mcd = mmd->getClassDef();

                     if (mcd) {
                        int m = minClassDistance(fcd, mcd);

                        if (m < mdist && mcd->isLinkable()) {
                           mdist = m;
                           cd    = mcd;
                           md    = mmd;
                        }
                     }
                  }
               }
            }

            if (argList) {
               delete argList;
               argList = 0;
            }

            if (mdist == maxInheritanceDepth && args == "()") {
               // no exact match found, but if args="()" an arbitrary member will do

               for (auto mmd : *mn) {
                  QSharedPointer<ClassDef> mcd = mmd->getClassDef();

                  if (mcd) {
                     int m = minClassDistance(fcd, mcd);

                     if (m < mdist) {
                        mdist = m;
                        cd = mcd;
                        md = mmd;
                     }
                  }

               }
            }

            if (mdist < maxInheritanceDepth) {

               if (! md->isLinkable() || md->isStrongEnumValue()) {
                  // avoid returning things we can not link to
                  md = QSharedPointer<MemberDef>();
                  cd = QSharedPointer<ClassDef>();

                  // found a match, not linkable
                  return false;

               } else {
                  gd = md->getGroupDef();

                  if (gd) {
                     cd = QSharedPointer<ClassDef>();
                  }

                  // found a match
                  return true;
               }
            }
         }

         if (tmd && tmd->isEnumerate() && tmd->isStrong()) {
            // scoped enum
            QSharedPointer<MemberList> tml = tmd->enumFieldList();

            if (tml) {
               for (auto emd : *tml) {
                  if (emd->localName() == mName) {
                     if (emd->isLinkable()) {
                        cd = tmd->getClassDef();
                        md = emd;

                        return true;

                     } else {
                        md = QSharedPointer<MemberDef>();
                        cd = QSharedPointer<ClassDef>();

                        return false;
                     }
                  }
               }
            }
         }

         /* go to the parent scope */
         if (scopeOffset == 0) {
            scopeOffset = -1;

         } else if ((scopeOffset = scopeName.lastIndexOf("::", scopeOffset - 1)) == -1) {
            scopeOffset = 0;
         }

      } while (scopeOffset >= 0);

   }

   if (mn && scopeName.isEmpty() && mScope.isEmpty()) {
      // Maybe a related function?

      QSharedPointer<MemberDef> temp;
      QSharedPointer<MemberDef> fuzzy_mmd;

      ArgumentList *argList = 0;
      bool hasEmptyArgs = (args == "()");

      if (! args.isEmpty()) {
         stringToArgumentList(args, argList = new ArgumentList);
      }

      bool isFound = false;

      for (auto item : *mn) {
         temp = item;

         if (! item->isLinkable() || (! item->isRelated() && ! item->isForeign()) || ! item->getClassDef()) {
            continue;
         }

         if (args.isEmpty()) {
            isFound = true;
            break;
         }

         ArgumentList *mmdAl = item->argumentList();

         if (matchArguments2(item->getOuterScope(), item->getFileDef(), mmdAl,
                             Doxy_Globals::globalScope, item->getFileDef(), argList, checkCV)) {

            isFound = true;
            break;
         }

         if (! fuzzy_mmd && hasEmptyArgs) {
            fuzzy_mmd = item;
         }
      }

      if (argList) {
         delete argList;
         argList = 0;
      }

      if (! isFound) {
         temp = fuzzy_mmd;
      }

      if (temp && ! temp->isStrongEnumValue()) {
         md = temp;
         cd = temp->getClassDef();

         return true;
      }
   }

   // maybe a namespace, file or group member ?
   mn = Doxy_Globals::functionNameSDict->find(mName);

   if (mn) {
      // name is known
      QSharedPointer<NamespaceDef> fnd;
      int scopeOffset = scopeName.length();


      do {
         QString namespaceName = scopeName.left(scopeOffset);

         if (! namespaceName.isEmpty() && ! mScope.isEmpty()) {
            namespaceName += "::" + mScope;

         } else if (! mScope.isEmpty()) {
            namespaceName = mScope;
         }

         if (! namespaceName.isEmpty() && (fnd = Doxy_Globals::namespaceSDict->find(namespaceName)) && fnd->isLinkable()) {
            bool found = false;

            for (auto mmd : *mn) {

               if (found) {
                  break;
               }

               QSharedPointer<MemberDef> emd = mmd->getEnumScope();

               if (emd && emd->isStrong()) {

                  if (emd->getNamespaceDef() == fnd && rightScopeMatch(mScope, emd->localName())) {
                     nd = fnd;
                     md = mmd;

                     found = true;

                  } else {
                     md = QSharedPointer<MemberDef>();
                     cd = QSharedPointer<ClassDef>();

                     return false;
                  }

               } else if (mmd->getNamespaceDef() == fnd) {
                  // namespace is found

                  bool match = true;
                  ArgumentList *argList = 0;

                  if (! args.isEmpty() && args != "()") {
                     argList = new ArgumentList;
                     ArgumentList *mmdAl = mmd->argumentList();

                     stringToArgumentList(args, argList);
                     match = matchArguments2(mmd->getOuterScope(), mmd->getFileDef(), mmdAl,
                                fnd, mmd->getFileDef(), argList, checkCV);
                  }

                  if (match) {
                     nd    = fnd;
                     md    = mmd;
                     found = true;
                  }

                  if (! args.isEmpty()) {
                     delete argList;
                     argList = 0;
                  }
               }
            }

            if (! found && ! args.isEmpty() && args == "()") {
               // no exact match found, but if args="()" an arbitrary member will do

               for (auto mmd : *mn) {

                  if (found) {
                     break;
                  }

                  if (mmd->getNamespaceDef() == fnd) {
                     nd = fnd;
                     md = mmd;
                     found = true;
                  }
               }
            }

            if (found) {
               if (! md->isLinkable()) {
                  // avoid returning things we cannot link to

                  md = QSharedPointer<MemberDef>();
                  nd = QSharedPointer<NamespaceDef>();

                  return false;    // match found but not linkable

               } else {
                  gd = md->getGroupDef();

                  if (gd && gd->isLinkable()) {
                     nd = QSharedPointer<NamespaceDef>();

                  } else {
                     gd = QSharedPointer<GroupDef>();

                  }

                  return true;
               }
            }

         } else {
            // not a namespace

            for (auto mmd : *mn) {
               QSharedPointer<MemberDef> tmd = mmd->getEnumScope();

               int ni = namespaceName.lastIndexOf("::");
               bool notInNS = tmd && ni == -1 && tmd->getNamespaceDef() == 0 && (mScope.isEmpty() || mScope == tmd->name());
               bool sameNS  = tmd && tmd->getNamespaceDef() && namespaceName.left(ni) == tmd->getNamespaceDef()->name();

               if (tmd && tmd->isStrong() && (notInNS || sameNS) && namespaceName.length() > 0) {
                  // enum is part of namespace so this should not be empty
                  md = mmd;
                  fd = mmd->getFileDef();
                  gd = mmd->getGroupDef();

                  if (gd && gd->isLinkable()) {
                     fd = QSharedPointer<FileDef>();

                  } else {
                     gd = QSharedPointer<GroupDef>();
                  }

                  return true;
               }
            }
         }

         if (scopeOffset == 0) {
            scopeOffset = -1;

         } else if ((scopeOffset = scopeName.lastIndexOf("::", scopeOffset - 1)) == -1) {
            scopeOffset = 0;
         }

      } while (scopeOffset >= 0);


      // no scope, global function
      {
         QList<QSharedPointer<MemberDef>> members;

         // search for matches with strict static checking
         findMembersWithSpecificName(mn, args, true, currentFile, checkCV, forceTagFile, members);

         if (members.count() == 0) {
            // nothing found
            // search again without strict static checking
            findMembersWithSpecificName(mn, args, false, currentFile, checkCV, forceTagFile, members);
         }

         if (members.count() != 1 && ! args.isEmpty() && args == "()") {
            // no exact match found, but if args="()" an arbitrary member will do

            for (auto md : *mn) {
               fd = md->getFileDef();
               gd = md->getGroupDef();

               QSharedPointer<MemberDef> tmd = md->getEnumScope();

               if ((gd && gd->isLinkable()) || (fd && fd->isLinkable()) || (tmd && tmd->isStrong())) {
                  members.append(md);
               }
            }
         }

         if (members.count() > 0) { // at least one match
            if (currentFile) {

               for (auto md : members) {
                  if (md->getFileDef() && md->getFileDef()->name() == currentFile->name()) {
                     break; // found match in the current file
                  }
               }

               if (! md) {
                  // member not in the current file
                  md = members.last();
               }

            } else {
               md = members.last();
            }
         }

         if (md && (md->getEnumScope() == 0 || ! md->getEnumScope()->isStrong())) {
            // found a matching global member, that is not a scoped enum value (or uniquely matches)

            fd = md->getFileDef();
            gd = md->getGroupDef();

            if (gd && gd->isLinkable()) {
               fd = QSharedPointer<FileDef>();
            } else {
               gd = QSharedPointer<GroupDef>();
            }

            return true;
         }
      }
   }

   // nothing found
   return false;
}

/*!
 * Searches for a scope definition given its name as a string via parameter
 * `scope`.
 *
 * The parameter `docScope` is a string representing the name of the scope in
 * which the `scope` string was found.
 *
 * The function returns true if the scope is known and documented or
 * false if it is not.
 * If true is returned exactly one of the parameter `cd`, `nd`
 * will be non-zero:
 *   - if `cd` is non zero, the scope was a class pointed to by cd.
 *   - if `nd` is non zero, the scope was a namespace pointed to by nd.
 */
static bool getScopeDefs(const QString &docScope, const QString &scope,
                  QSharedPointer<ClassDef> &cd, QSharedPointer<NamespaceDef> &nd)
{
   cd = QSharedPointer<ClassDef>();
   nd = QSharedPointer<NamespaceDef>();

   QString scopeName = scope;

   if (scopeName.isEmpty()) {
      return false;
   }

   bool explicitGlobalScope = false;

   if (scopeName.startsWith("::")) {
      scopeName = scopeName.right(scopeName.length() - 2);
      explicitGlobalScope = true;
   }

   QString docScopeName = docScope;
   int scopeOffset = explicitGlobalScope ? 0 : docScopeName.length();

   do {
      // for each possible docScope (from largest to and including empty)
      QString fullName = scopeName;

      if (scopeOffset > 0) {
         fullName.prepend(docScopeName.left(scopeOffset) + "::");
      }

      if (((cd = getClass(fullName)) || (cd = getClass(fullName + "-p")) ) && cd->isLinkable()) {
         return true;

      } else if ((nd = Doxy_Globals::namespaceSDict->find(fullName)) && nd->isLinkable()) {
         return true;

      }

      if (scopeOffset == 0) {
         scopeOffset = -1;

      } else if ((scopeOffset = docScopeName.lastIndexOf("::", scopeOffset - 1)) == -1) {
         scopeOffset = 0;
      }

   } while (scopeOffset >= 0);

   return false;
}

static bool isLowerCase(const QString &str)
{
   for (auto c : str) {

      if (! c.isLower() ) {
         return false;
      }
   }

   return true;
}

/*! Returns an object given its name and context
 *  @post return value true implies *resContext != 0 or *resMember != 0
 */
bool resolveRef(const QString &scName, const QString &tName, bool inSeeBlock, QSharedPointer<Definition> *resContext,
                QSharedPointer<MemberDef> *resMember, bool useBaseTemplateOnly, QSharedPointer<FileDef> currentFile,
                bool checkScope)
{
   QString fullName = substitute(tName, "#", "::");

   if (fullName.indexOf("anonymous_namespace{") == -1) {
      fullName = removeRedundantWhiteSpace(substitute(fullName, ".", "::"));
   } else {
      fullName = removeRedundantWhiteSpace(fullName);
   }

   int bracePos = findParameterList(fullName);
   int endNamePos;
   int scopePos;

   if (bracePos != -1) {
      endNamePos = bracePos;
      scopePos   = fullName.lastIndexOf("::", bracePos);

   } else   {
      endNamePos = fullName.length();
      scopePos = fullName.lastIndexOf("::", fullName.length() - 1);
   }

   bool explicitScope = fullName.left(2) == "::" && (scopePos > 2 || tName.left(2) == "::" || scName.isEmpty());

   // default result values
   *resContext = QSharedPointer<Definition>();
   *resMember  = QSharedPointer<MemberDef>();

   if (bracePos == -1) {
      // simple name
      QSharedPointer<ClassDef> cd;
      QSharedPointer<NamespaceDef> nd;

      if (! inSeeBlock && scopePos == -1 && isLowerCase(tName) ) {
         // link to lower case only name, do not try to autolink
         return false;
      }

      // check if fullName is a class or namespace reference
      if (scName != fullName && getScopeDefs(scName, fullName, cd, nd)) {

         if (cd) {
            // scope matches that of a class
            *resContext = cd;

         } else {
            // scope matches that of a namespace
            assert(nd != 0);
            *resContext = nd;
         }

         return true;

      } else if (scName == fullName || (! inSeeBlock && scopePos == -1)) {
         // nothing to link, output plain text
         return false;
      }

      // continue search...
   }

   // extract userscope + name
   QString nameStr = fullName.left(endNamePos);
   if (explicitScope) {
      nameStr = nameStr.mid(2);
   }

   // extract arguments
   QString argsStr;
   if (bracePos != -1) {
      argsStr = fullName.right(fullName.length() - bracePos);
   }

   QString scopeStr = scName;

   QSharedPointer<MemberDef>    md;
   QSharedPointer<ClassDef>     cd;
   QSharedPointer<FileDef>      fd;
   QSharedPointer<NamespaceDef> nd;
   QSharedPointer<GroupDef>     gd;

   // check if nameStr is a member or global
   if (getDefs(scopeStr, nameStr, argsStr, md, cd, fd, nd, gd, explicitScope, currentFile, true))  {

      if (checkScope && md && md->getOuterScope() == Doxy_Globals::globalScope &&
            ! md->isStrongEnumValue() && (! scopeStr.isEmpty() || nameStr.indexOf("::") > 0)) {

         // we found a member, but it is a global one while we were explicitly
         // looking for a scoped variable. See bug 616387 for an example why this check is needed.
         // note we do need to support autolinking to "::symbol" hence the > 0

         *resContext = QSharedPointer<Definition>();
         *resMember  = QSharedPointer<MemberDef>();

         return false;
      }

      if (md) {
         *resMember  = md;
         *resContext = md;

      } else if (cd) {
         *resContext = cd;

      } else if (nd) {
         *resContext = nd;

      } else if (fd) {
         *resContext = fd;

      } else if (gd) {
         *resContext = gd;

      } else {
         *resContext = QSharedPointer<Definition>();
         *resMember  = QSharedPointer<MemberDef>();

         return false;
      }

      return true;

   } else if (inSeeBlock && ! nameStr.isEmpty() && (gd = Doxy_Globals::groupSDict->find(nameStr))) {
      // group link
      *resContext = gd;
      return true;

   } else if (tName.indexOf('.') != -1) {
      // maybe a link to a file

      bool ambig;
      fd = findFileDef(Doxy_Globals::inputNameDict, tName, ambig);

      if (fd && !ambig) {
         *resContext = fd;
         return true;
      }
   }

   // strip template specifier, try again
   // broom - use libClang to match the correct template
   int posBegin = nameStr.indexOf('<');
   bool tryBaseTemplate = false;

   if (posBegin != -1 && nameStr.indexOf("operator") == -1) {
      int posEnd = nameStr.lastIndexOf('>');

      if (posEnd != -1) {

         if (useBaseTemplateOnly) {
            // do nothing

         } else {
            nameStr = nameStr.left(posBegin) + nameStr.right(nameStr.length() - posEnd - 1);
            return resolveRef(scName, nameStr, inSeeBlock, resContext, resMember, true, QSharedPointer<FileDef>(), checkScope);

         }
      }
   }

   if (bracePos != -1) {
      // try without parameters as well, could be a contructor invocation
      *resContext = getClass(fullName.left(bracePos));

      if (*resContext) {
         return true;
      }
   }

   return false;
}

QString linkToText(SrcLangExt lang, const QString &link, bool isFileName)
{
   // static bool optimizeOutputJava = Config::getBool("optimize-java");
   QString result = link;

   if (! result.isEmpty()) {
      // replace # by ::
      result = substitute(result, "#", "::");

      // replace . by ::
      if (! isFileName && result.indexOf('<') == -1) {
         result = substitute(result, ".", "::");
      }

      // strip leading :: prefix if present
      if (result.length() > 1 && result.at(0) == ':' && result.at(1) == ':') {
         result = result.right(result.length() - 2);
      }

      QString sep = getLanguageSpecificSeparator(lang);

      if (sep != "::") {
         result = substitute(result, "::", sep);
      }
   }

   return result;
}

bool resolveLink(const QString &scName, const QString &linkRef, bool xx, QSharedPointer<Definition> *resContext, QString &resAnchor)
{
   *resContext = QSharedPointer<Definition>();

   QString linkRefWithoutTemplates = stripTemplateSpecifiersFromScope(linkRef, false);

   QSharedPointer<FileDef>  fd;
   QSharedPointer<GroupDef> gd;
   QSharedPointer<PageDef>  pd;
   QSharedPointer<ClassDef> cd;
   QSharedPointer<DirDef>   dir;
   QSharedPointer<NamespaceDef> nd;

   QSharedPointer<SectionInfo> si;
   bool ambig;

   if (linkRef.isEmpty()) {
      // no reference name
      return false;

   } else if (pd = Doxy_Globals::pageSDict->find(linkRef)) {
      // link to a page
      QSharedPointer<GroupDef> gd = pd->getGroupDef();

      if (gd) {
         if (! pd->name().isEmpty()) {
            si = Doxy_Globals::sectionDict->find(pd->name());
         }

         *resContext = gd;
         if (si) {
            resAnchor = si->label;
         }

      } else {
         *resContext = pd;
      }

      return true;

   } else if (si = Doxy_Globals::sectionDict->find(linkRef)) {
      *resContext = si->definition;
      resAnchor = si->label;
      return true;

   } else if (pd = Doxy_Globals::exampleSDict->find(linkRef)) {
      // link to an example

      *resContext = pd;
      return true;

   } else if (gd = Doxy_Globals::groupSDict->find(linkRef)) {
      // link to a group

      *resContext = gd;
      return true;

   } else if ((fd = findFileDef(Doxy_Globals::inputNameDict, linkRef, ambig)) && fd->isLinkable()) {
      // file link
      *resContext = fd;
      return true;

   } else if (cd = getClass(linkRef)) {
      // class link

      *resContext = cd;
      resAnchor = cd->anchor();
      return true;

   } else if ((cd = getClass(linkRefWithoutTemplates))) {
      // C#/Java generic class link

      *resContext = cd;
      resAnchor = cd->anchor();
      return true;

   } else if (cd = getClass(linkRef + "-p")) {
      // Obj-C protocol link

      *resContext = cd;
      resAnchor = cd->anchor();
      return true;

   }

   else if ((nd = Doxy_Globals::namespaceSDict->find(linkRef))) {
      *resContext = nd;
      return true;

   } else if ((dir = Doxy_Globals::directories.find(QFileInfo(linkRef).absoluteFilePath() + "/")) && dir->isLinkable()) {
      // TODO: make this location independent like filedefs

      *resContext = dir;
      return true;

   } else {
      // probably a member reference
      QSharedPointer<MemberDef> md = QSharedPointer<MemberDef>();
      bool res = resolveRef(scName, linkRef, true, resContext, &md);

      if (md) {
         resAnchor = md->anchor();
      }

      return res;
   }
}

// General function that generates the HTML code for a reference to some
// file, class or member from text `lr' within the context of class `clName'.
// This link has the text 'lt' (if not 0), otherwise `lr' is used as a
// basis for the link's text returns true if a link could be generated.

bool generateLink(OutputDocInterface &od, const QString &clName, const QString &lr, bool inSeeBlock, const QString &lt)
{
   QSharedPointer<Definition> compound;

   QString anchor;
   QString linkText = linkToText(SrcLangExt_Unknown, lt, false);

   if (resolveLink(clName, lr, inSeeBlock, &compound, anchor)) {

      if (compound) {
         // link to compound
         QSharedPointer<GroupDef> temp = compound.dynamicCast<GroupDef>();

         if (lt.isEmpty() && anchor.isEmpty() && compound->definitionType() == Definition::TypeGroup) {
            linkText = temp->groupTitle(); // use group's title as link

         } else if (compound->definitionType() == Definition::TypeFile) {
            linkText = linkToText(compound->getLanguage(), lt, true);
         }

         od.writeObjectLink(compound->getReference(), compound->getOutputFileBase(), anchor, linkText);

         if (! compound->isReference()) {
            writePageRef(od, compound->getOutputFileBase(), anchor);
         }

      } else {
         err("%s:%d: resolveLink() successful but no compound was found", __FILE__, __LINE__);
      }

      return true;

   } else {
      // link could not be found
      od.docify(linkText);
      return false;
   }
}

void generateFileRef(OutputDocInterface &od, const QString &name, const QString &text)
{
   QString linkText = text;

   if (linkText.isEmpty()) {
      linkText = name;
   }

   QSharedPointer<FileDef> fd;
   bool ambig;

   if ((fd = findFileDef(Doxy_Globals::inputNameDict, name, ambig)) && fd->isLinkable()) {
      // link to documented input file

      od.writeObjectLink(fd->getReference(), fd->getOutputFileBase(), 0, linkText);

   } else {
      od.docify(linkText);
   }
}

QSharedPointer<FileDef> findFileDef(const FileNameDict *fnDict, const QString &name, bool &ambig)
{
   ambig = false;

   if (name.isEmpty()) {
      return QSharedPointer<FileDef>();
   }

   // set up the key
   QPair<const FileNameDict *, QString> key(fnDict, name);

   FindFileCacheElem *cachedResult = s_findFileDefCache[key];

   if (cachedResult) {
      ambig = cachedResult->isAmbig;
      return cachedResult->fileDef;
   }

   cachedResult = new FindFileCacheElem(QSharedPointer<FileDef>(), false);

   QFileInfo fi(name);

   QString fName = fi.fileName();
   QString path  = fi.absolutePath() + "/";

   if (fName.isEmpty()) {
      s_findFileDefCache.insert(key, cachedResult);
      return QSharedPointer<FileDef>();
   }

   // returns a FileName which inherits from FileList
   QSharedPointer<FileNameList> fn = (*fnDict)[fName];

   if (fn) {

      if (fn->count() == 1) {
         QSharedPointer<FileDef> fd = fn->first();

         QFileInfo fdFile(fd->getPath());
         QString fdPath = fi.absolutePath() + "/";

#if defined(_WIN32) || defined(__MACOSX__)
         // Windows or MacOSX
         bool isSamePath = fdPath.toLower() == path.toLower();
#else
         // Unix
         bool isSamePath = fdPath == path;
#endif

         if (path.isEmpty() || isSamePath) {
            cachedResult->fileDef = fd;

            s_findFileDefCache.insert(key, cachedResult);
            return fd;
         }

      } else {
         // file name alone is ambiguous
         int count = 0;

         QSharedPointer<FileDef> lastMatch;
         QString pathStripped = stripFromIncludePath(path);

         for (auto fd : *fn) {
            QString fdStripPath = stripFromIncludePath(fd->getPath());

            if (path.isEmpty() || fdStripPath.right(pathStripped.length()) == pathStripped) {
               count++;
               lastMatch = fd;
            }
         }

         ambig = (count > 1);

         cachedResult->isAmbig = ambig;
         cachedResult->fileDef = lastMatch;

         s_findFileDefCache.insert(key, cachedResult);
         return lastMatch;
      }
   }

   s_findFileDefCache.insert(key, cachedResult);

   return QSharedPointer<FileDef>();
}

QString showFileDefMatches(const FileNameDict *fnDict, const QString &xName)
{
   QString result;
   QString path;
   QString name = xName;

   int slashPos = qMax(name.lastIndexOf('/'), name.lastIndexOf('\\'));

   if (slashPos != -1) {
      path = name.left(slashPos + 1);
      name = name.right(name.length() - slashPos - 1);
   }

   QSharedPointer<FileNameList> fn;
   fn = (*fnDict)[name];

   if (fn) {
      for (auto fd : *fn) {
         if (path.isEmpty() || fd->getPath().right(path.length()) == path) {
            result += "   " + fd->getFilePath() + "\n";
         }
      }
   }

   return result;
}

/// substitute all occurrences of \a old in \a str by \a dest
QString substitute(const QString &origString, const QString &oldWord, const QString &newWord)
{
   QString retval = origString;
   retval.replace(oldWord, newWord);

   return retval;
}

/*! Returns the character index within \a name of the first prefix
 *  in Config::getList("ignore-prefix") that matches \a name at the left hand side,
 *  or zero if no match was found
 */
int getPrefixIndex(const QString &name)
{
   if (name.isEmpty()) {
      return 0;
   }

   static const QStringList slist = Config::getList("ignore-prefix");

   for (auto s : slist) {

      if (name.startsWith(s) && name != s)  {
         return s.length();
      }
   }

   return 0;
}

static void initBaseClassHierarchy(SortedList<BaseClassDef *> *bcl)
{
   if (bcl == 0) {
      return;
   }

   for (auto item : *bcl) {
      QSharedPointer<ClassDef> cd = item->classDef;

      if (cd->baseClasses() == 0) {
         // no base classes => new root
         initBaseClassHierarchy(cd->baseClasses());
      }

      cd->visited = false;
   }
}

bool classHasVisibleChildren(QSharedPointer<ClassDef> cd)
{
   SortedList<BaseClassDef *> *bcl;

   if (cd->subClasses() == 0) {
      return false;
   }

   bcl = cd->subClasses();

   for (auto item : *bcl) {
      if (item->classDef->isVisibleInHierarchy()) {
         return true;
      }
   }

   return false;
}

void initClassHierarchy(ClassSDict *cl)
{
   for (auto cd : *cl) {
      cd->visited = false;
      initBaseClassHierarchy(cd->baseClasses());
   }
}

bool hasVisibleRoot(SortedList<BaseClassDef *> *bcl)
{
   if (bcl) {

      for (auto item : *bcl) {
         QSharedPointer<ClassDef> cd = item->classDef;

         if (cd->isVisibleInHierarchy()) {
            return true;
         }

         hasVisibleRoot(cd->baseClasses());
      }
   }

   return false;
}

QString escapeCharsInString(const QString &name, bool allowDots, bool allowUnderscore)
{
   static bool caseSenseNames    = Config::getBool("case-sensitive-fname");
   static bool allowUnicodeNames = Config::getBool("allow-unicode-names");

   QString retval;

   const QChar *p = name.constData();
   QChar c;

   while ((c = *p++) != 0) {

      switch (c.unicode()) {
         case '_':

            if (allowUnderscore) {
               retval += '_';
            } else {
               retval += "__";
            }
            break;

         case '-':
            retval += '-';
            break;

         case ':':
            retval += "_1";
            break;

         case '/':
            retval += "_2";
            break;

         case '<':
            retval += "_3";
            break;

         case '>':
            retval += "_4";
            break;

         case '*':
            retval += "_5";
            break;

         case '&':
            retval += "_6";
            break;

         case '|':
            retval += "_7";
            break;

         case '.':
            if (allowDots) {
               retval += '.';
            } else {
               retval += "_8";
            }
            break;

         case '!':
            retval += "_9";
            break;

         case ',':
            retval += "_00";
            break;

         case ' ':
            retval += "_01";
            break;

         case '{':
            retval += "_02";
            break;

         case '}':
            retval += "_03";
            break;

         case '?':
            retval += "_04";
            break;

         case '^':
            retval += "_05";
            break;

         case '%':
            retval += "_06";
            break;

         case '(':
            retval += "_07";
            break;

         case ')':
            retval += "_08";
            break;

         case '+':
            retval += "_09";
            break;

         case '=':
            retval += "_0A";
            break;

         case '$':
            retval += "_0B";
            break;

         case '\\':
            retval += "_0C";
            break;

         case '@':
            retval += "_0D";
            break;

         default:

            if (c.unicode() > 127) {
               if (allowUnicodeNames) {
                  retval += c;

               } else {
                  QString tmp = QString("_x%1").arg(c.unicode(), 2, 16, QChar('0') );
                  retval += tmp;
               }

            } else if (caseSenseNames || ! c.isUpper()) {
               retval += c;

            } else {
               retval += '_';
               retval += c.toLower();
            }
            break;
      }
   }

   return retval;
}

/*! This function determines the file name on disk of an item
 *  given its name, which could be a class name with template
 *  arguments, so special characters need to be escaped.
 */
QString convertNameToFile(const QString &name, bool allowDots, bool allowUnderscore)
{
   static bool shortNames    = Config::getBool("short-names");
   static bool createSubdirs = Config::getBool("create-subdirs");

   QString result;

   if (name.isEmpty()){
      return result;
   }

   if (shortNames) {
      // use short names only
      static QHash<QString, int> usedNames;

      static int count = 1;
      int num;

      auto value = usedNames.find(name);

      if (value != usedNames.end()) {
         usedNames.insert(name, count);
         num = count++;

      } else {
         num = *value;
      }

      result = QString("a%1").arg(num, 5, 10, QChar('0'));

   } else {
      // long names
      result = escapeCharsInString(name, allowDots, allowUnderscore);
      int resultLen = result.length();

      if (resultLen >= 128) {
         // prevent names that cannot be created
         // third algorithm based on MD5 hash

         QString sigStr;
         sigStr = QCryptographicHash::hash(result.toUtf8(), QCryptographicHash::Md5).toHex();

         result = result.left(128 - 32) + sigStr;
      }
   }

   if (createSubdirs) {
      int l1Dir = 0;
      int l2Dir = 0;

#if MAP_ALGO == ALGO_COUNT
      // old algorithm, has the problem that after regeneration the
      // output can be located in a different dir.

      if (Doxy_Globals::htmlDirMap == 0) {
         Doxy_Globals::htmlDirMap = new QHash<QString, int>();
      }

      static int curDirNum = 0;
      int *dirNum = Doxy_Globals::htmlDirMap->find(result);

      if (dirNum == 0) {
         // new name
         Doxy_Globals::htmlDirMap->insert(result, new int(curDirNum));
         l1Dir = (curDirNum) & 0xf;        // bits 0-3
         l2Dir = (curDirNum >> 4) & 0xff;  // bits 4-11
         curDirNum++;

      } else {
         // existing name
         l1Dir = (*dirNum) & 0xf;          // bits 0-3
         l2Dir = ((*dirNum) >> 4) & 0xff;  // bits 4-11
      }

#elif MAP_ALGO == ALGO_CRC16
      // second algorithm based on CRC-16 checksum
      int dirNum = qChecksum(result, result.length());

      l1Dir = dirNum & 0xf;
      l2Dir = (dirNum >> 4) & 0xff;

#elif MAP_ALGO == ALGO_MD5
      // third algorithm based on MD5 hash

      QString sigStr;
      sigStr = QCryptographicHash::hash(result.toUtf8(), QCryptographicHash::Md5);

      l1Dir = sigStr[14].unicode() & 0xf;
      l2Dir = sigStr[15].unicode() & 0xff;
#endif

      result = QString("d%1/d%2/").arg(l1Dir, 0, 16).arg(l2Dir, 2, 16, QChar('0')) + result;
   }

   return result;
}

QByteArray relativePathToRoot(const QString &name)
{
   if (Config::getBool("create-subdirs")) {

      if (name.isEmpty() || name.contains("/") ) {
         return "../../";
      }
   }

   return QByteArray();
}

void createSubDirs(QDir &d)
{
   if (Config::getBool("create-subdirs")) {
      // create 4096 subdirectories

      int l1;
      int l2;

      for (l1 = 0; l1 < 16; l1++) {
         QString temp = QString("d%1").arg(l1, 0, 16);
         d.mkdir(temp);

         for (l2 = 0; l2 < 256; l2++) {
            QString temp = QString("d%1/d%2").arg(l1, 0, 16).arg(l2, 2, 16, QChar('0'));
            d.mkdir(temp);
         }
      }
   }
}

/*! Input is a scopeName, output is the scopename split into a
 *  namespace part (as large as possible) and a classname part.
 */
void extractNamespaceName(const QString &scopeName, QString &className, QString &namespaceName, bool allowEmptyClass)
{
   int i, p;
   QString clName = scopeName;

   QSharedPointer<NamespaceDef> nd;

   if (! clName.isEmpty() && (nd = getResolvedNamespace(clName)) && getClass(clName) == 0) {
      // the whole name is a namespace (and not a class)
      namespaceName = nd->name();

      className.resize(0);
      goto done;
   }

   p = clName.length() - 2;

   while (p >= 0 && (i = clName.lastIndexOf("::", p)) != -1) {
      // see if the first part is a namespace (and not a class)

      if (i > 0 && (nd = getResolvedNamespace(clName.left(i))) && getClass(clName.left(i)) == 0) {
         //printf("found!\n");
         namespaceName = nd->name();
         className = clName.right(clName.length() - i - 2);
         goto done;
      }
      p = i - 2; // try a smaller piece of the scope
   }

   // not found, so we just have to guess.
   className = scopeName;
   namespaceName.resize(0);

done:
   if (className.isEmpty() && !namespaceName.isEmpty() && !allowEmptyClass) {
      // class and namespace with the same name, correct to return the class.
      className = namespaceName;
      namespaceName.resize(0);
   }

   if (/*className.right(2)=="-g" ||*/ className.right(2) == "-p") {
      className = className.left(className.length() - 2);
   }

   return;
}

QString insertTemplateSpecifierInScope(const QString &scope, const QString &templ)
{
   QString result = scope;

   if (! templ.isEmpty() && scope.indexOf('<') == -1) {
      int si;
      int pi = 0;

      QSharedPointer<ClassDef> cd = QSharedPointer<ClassDef>();

      while ( (si = scope.indexOf("::", pi)) != -1 && ! getClass(scope.left(si) + templ) &&
         ((cd = getClass(scope.left(si))) == 0 || cd->templateArguments() == 0) ) {
         pi = si + 2;
      }

      if (si == -1) { // not nested => append template specifier
         result += templ;

      } else { // nested => insert template specifier before after first class name
         result = scope.left(si) + templ + scope.right(scope.length() - si);
      }
   }

   return result;
}

// new version by Davide Cesari which also works for Fortran
QString stripScope(const QString &name)
{
   QString  result = name;

   int l = result.length();
   int p;

   bool done = false;
   bool skipBracket = false; // if brackets do not match properly, ignore them altogether

   int count = 0;

   do {
      p = l - 1; // start at the end of the string

      while (p >= 0 && count >= 0) {
         QChar c = result.at(p);

         switch (c.unicode()) {
            case ':':
               // only exit in the case of ::

               if (p > 0 && result.at(p - 1) == ':') {
                  return result.right(l - p - 1);
               }
               p--;
               break;

            case '>':
               if (skipBracket) { // do not care about brackets
                  p--;

               } else { // count open/close brackets
                  if (p > 0 && result.at(p - 1) == '>') { // skip >> operator
                     p -= 2;
                     break;
                  }
                  count = 1;

                  p--;
                  bool foundMatch = false;

                  while (p >= 0 && ! foundMatch) {
                     c = result.at(p--);

                     switch (c.unicode()) {
                        case '>':
                           count++;
                           break;
                        case '<':
                           if (p > 0) {
                              if (result.at(p - 1) == '<') { // skip << operator
                                 p--;
                                 break;
                              }
                           }
                           count--;
                           foundMatch = count == 0;
                           break;

                        default:

                           break;
                     }
                  }
               }

               break;

            default:
               p--;
         }
      }

      done = count == 0 || skipBracket; // reparse if brackets do not match
      skipBracket = true;

   } while (! done); // if < > unbalanced repeat ignoring them

   return name;
}


/*! Converts a string to an XML-encoded string */
QString convertToXML(const QString &str)
{
   QString retval;

   if (str.isEmpty()) {
      return retval;
   }

   for (auto c : str) {

      switch (c.unicode()) {
         case '<':
            retval += "&lt;";
            break;
         case '>':
            retval += "&gt;";
            break;
         case '&':
            retval += "&amp;";
            break;
         case '\'':
            retval += "&apos;";
            break;
         case '"':
            retval += "&quot;";
            break;

         case  1:
         case  2:
         case  3:
         case  4:
         case  5:
         case  6:
         case  7:
         case  8:
         case 11:
         case 12:
         case 13:
         case 14:
         case 15:
         case 16:
         case 17:
         case 18:
         case 19:
         case 20:
         case 21:
         case 22:
         case 23:
         case 24:
         case 25:
         case 26:
         case 27:
         case 28:
         case 29:
         case 30:
         case 31:
            break; // skip invalid XML characters (see http://www.w3.org/TR/2000/REC-xml-20001006#NT-Char)

         default:
            retval += c;
            break;
      }
   }

   return retval;
}

/*! Converts a string to a HTML-encoded string */
QString convertToHtml(const QString &str, bool keepEntities)
{
   if (str.isEmpty()) {
      return "";
   }

   QString retval;

   const QChar *p = str.constData();
   QChar c;

   while ((c = *p++) != 0) {

      switch (c.unicode()) {
         case '<':
            retval += "&lt;";
            break;

         case '>':
            retval += "&gt;";
            break;

         case '&':
            if (keepEntities) {
               const QChar *e = p;
               QChar ce;

               while ((ce = *e++) != 0) {
                  if (ce == ';' || (! (isId(ce) || ce == '#'))) {
                     break;
                  }
               }

               if (ce == ';') {
                  // found end of an entity, copy entry verbatim
                  retval += c;

                  while (p < e) {
                     retval += *p++;
                  }

               } else {
                  retval += "&amp;";
               }

            } else {
               retval += "&amp;";
            }
            break;

         case '\'':
            retval += "&#39;";
            break;

         case '"':
            retval += "&quot;";
            break;

         default:
            retval += c;
            break;
      }
   }

   return retval;
}

QString convertToJSString(const QString &s)
{
   if (s.isEmpty()) {
      return "";
   }

   QString retval = s;

   retval.replace("\"", "\\\"");
   retval.replace("\'", "\\\'");

   return convertCharEntities(retval);
}

QString convertCharEntities(const QString &str)
{
   QString retval;

   if (str.isEmpty()) {
      return retval;
   }

   static QRegExp entityPat("&[a-zA-Z]+[0-9]*;");

   int i = 0;
   int p;
   int k;

   while ((p = entityPat.indexIn(str, i)) != -1) {
      k = entityPat.matchedLength();

      if (p > i) {
         retval += str.mid(i, p - i);
      }

      QString entity = str.mid(p, k);
      DocSymbol::SymType symType = HtmlEntityMapper::instance()->name2sym(entity);

      QString code;

      if (symType == DocSymbol::Sym_Unknown) {
         retval += str.mid(p, k);

      } else {
         code = HtmlEntityMapper::instance()->utf8(symType);

         if (! code.isEmpty()) {
            retval += code;

         } else {
            retval += str.mid(p, k);
         }
      }

      i = p + k;
   }

   retval += str.mid(i, str.length() - i);

   return retval;
}

void addMembersToMemberGroup(QSharedPointer<MemberList> ml, MemberGroupSDict **ppMemberGroupSDict, QSharedPointer<Definition> context)
{
   assert(context != 0);

   if (ml == nullptr) {
      return;
   }

   uint index = 0;

   for (auto md : *ml) {

      if (md->isEnumerate()) {
         // insert enum value of this enum into groups
         QSharedPointer<MemberList> fmdl = md->enumFieldList();

         if (fmdl != nullptr) {
            for (auto fmd : *fmdl) {
               int groupId = fmd->getMemberGroupId();

               if (groupId != -1) {
                  QSharedPointer<MemberGroupInfo> info = Doxy_Globals::memGrpInfoDict[groupId];

                  if (info) {
                     if (*ppMemberGroupSDict == nullptr) {
                        *ppMemberGroupSDict = new MemberGroupSDict;
                     }

                     QSharedPointer<MemberGroup> mg = (*ppMemberGroupSDict)->find(groupId);

                     if (mg == nullptr) {
                        mg = QMakeShared<MemberGroup>(context, groupId, info->header, info->doc, info->docFile, info->docLine);
                        (*ppMemberGroupSDict)->insert(groupId, mg);
                     }

                     mg->insertMember(fmd);          // insert in member group
                     fmd->setMemberGroup(mg);
                  }
               }
            }
         }
      }

      int groupId = md->getMemberGroupId();

      if (groupId != -1) {
         QSharedPointer<MemberGroupInfo> info = Doxy_Globals::memGrpInfoDict[groupId];

         if (info) {

            if (*ppMemberGroupSDict == nullptr) {
               *ppMemberGroupSDict = new MemberGroupSDict;
            }

            QSharedPointer<MemberGroup> mg = (*ppMemberGroupSDict)->find(groupId);

            if (mg == nullptr) {
               mg = QMakeShared<MemberGroup>(context, groupId, info->header, info->doc, info->docFile, info->docLine);
               (*ppMemberGroupSDict)->insert(groupId, mg);
            }

            md = ml->takeAt(index);           // remove from member list
            mg->insertMember(md);             // insert in member group
            mg->setRefItems(info->m_sli);
            md->setMemberGroup(mg);

            continue;
         }
      }

      ++index;
   }
}

/*! Extracts a (sub-)string from \a type starting at \a pos that
 *  could form a class. The index of the match is returned and the found
 *  class \a name and a template argument list \a templSpec. If -1 is returned
 *  there are no more matches.
 */
int extractClassNameFromType(const QString &type, int &pos, QString &name, QString &templSpec, SrcLangExt lang)
{
   static const QRegExp re_norm("[a-z_A-Z\\x80-\\xFF][a-z_A-Z0-9:\\x80-\\xFF]*");
   static const QRegExp re_ftn("[a-z_A-Z\\x80-\\xFF][()=_a-z_A-Z0-9:\\x80-\\xFF]*");
   QRegExp re;

   name.resize(0);
   templSpec.resize(0);

   int i;
   int l;
   int typeLen = type.length();

   if (typeLen > 0) {

      if (lang == SrcLangExt_Fortran) {

         if (typeLen <= pos) {
            return -1;

         } else if (type.at(pos) == ',') {
            return -1;

         }

         if (type.left(4).toLower() == "type") {
            re = re_norm;

         } else {
            re = re_ftn;

         }

      } else {
         re = re_norm;

      }

      if ((i = re.indexIn(type, pos)) != -1) {
         // for each class name in the type
         l = re.matchedLength();

         int ts = i + l;
         int te = ts;
         int tl = 0;

         while (ts < typeLen && type.at(ts) == ' ') {
            ts++, tl++;   // skip any whitespace
         }

         if (ts < typeLen && type.at(ts) == '<') { // assume template instance
            // locate end of template
            te = ts + 1;
            int brCount = 1;

            while (te < typeLen && brCount != 0) {

               if (type.at(te) == '<') {
                  if (te < typeLen - 1 && type.at(te + 1) == '<') {
                     te++;
                  } else {
                     brCount++;
                  }
               }

               if (type.at(te) == '>') {
                  if (te < typeLen - 1 && type.at(te + 1) == '>') {
                     te++;
                  } else {
                     brCount--;
                  }
               }
               te++;
            }
         }

         name = type.mid(i, l);

         if (te > ts) {
            templSpec = type.mid(ts, te - ts), tl += te - ts;
            pos = i + l + tl;

         } else { // no template part
            pos = i + l;
         }


         return i;
      }
   }

   pos = typeLen;

   return -1;
}

QString normalizeNonTemplateArgumentsInString(const QString &name, QSharedPointer<Definition> context,
                  const ArgumentList *formalArgs)
{
   // skip until <
   int p = name.indexOf('<');

   if (p == -1) {
      return name;
   }

   p++;
   QString result = name.left(p);

   static QRegExp re("[a-z_A-Z\\x80-\\xFF][a-z_A-Z0-9\\x80-\\xFF]*");
   int l, i;

   // for each identifier in the template part (e.g. B<T> -> T)
   while ((i = re.indexIn(name, p)) != -1) {
      l = re.matchedLength();

      result += name.mid(p, i - p);
      QString n = name.mid(i, l);
      bool found = false;

      if (formalArgs) {
         // check that n is not a formal template argument

         for (auto formArg : *formalArgs) {
            if (found) {
               break;
            }

            found = (formArg.name == n);
         }
      }

      if (! found) {
         // try to resolve the type
         QSharedPointer<ClassDef> cd = getResolvedClass(context, QSharedPointer<FileDef>(), n);

         if (cd) {
            result += cd->name();
         } else {
            result += n;
         }

      } else {
         result += n;
      }

      p = i + l;
   }

   result += name.right(name.length() - p);

   return removeRedundantWhiteSpace(result);
}


/*! Substitutes any occurrence of a formal argument from argument list
 *  \a formalArgs in \a name by the corresponding actual argument in
 *  argument list \a actualArgs. The result after substitution
 *  is returned as a string. The argument \a name is used to
 *  prevent recursive substitution.
 */
QString substituteTemplateArgumentsInString(const QString &name, ArgumentList *formalArgs, ArgumentList *actualArgs)
{
   if (formalArgs == 0) {
      return name;
   }

   QString result;

   static QRegExp re("[a-z_A-Z\\x80-\\xFF][a-z_A-Z0-9\\x80-\\xFF]*");
   int p = 0, l, i;

   // for each identifier in the base class name (e.g. B<T> -> B and T)

   while ((i = re.indexIn(name, p)) != -1) {
      l = re.matchedLength();

      result += name.mid(p, i - p);
      QString n = name.mid(i, l);

      // if n is a template argument, then we substitute it
      // for its template instance argument.
      bool found = false;

      auto act_iter = actualArgs->begin();

      for (auto formArg : *formalArgs) {

         if (found) {
            break;
         }

         Argument *actArg = nullptr;

         if (act_iter != actualArgs->end()) {
            actArg = &(*act_iter);
         }

         if (formArg.type.left(6) == "class " && formArg.name.isEmpty()) {
            formArg.name = formArg.type.mid(6);
            formArg.type = "class";
         }

         if (formArg.type.left(9) == "typename " && formArg.name.isEmpty()) {
            formArg.name = formArg.type.mid(9);
            formArg.type = "typename";
         }


         if (formArg.type == "class" || formArg.type == "typename" || formArg.type.startsWith("template")) {

            if (formArg.name == n && actArg != nullptr  && ! actArg->type.isEmpty()) {
               // base class is a template argument
               // replace formal argument with the actual argument of the instance

               if (! leftScopeMatch(actArg->type, n)) {
                  // the scope guard is to prevent recursive lockup for template<class A> class C : public<A::T>,
                  // where A::T would become A::T::T here, // since n==A and actArg->type==A::T

                  if (actArg->name.isEmpty()) {
                     result += actArg->type + " ";
                     found = true;

                  } else {
                     // for case where the actual arg is something like "unsigned int"
                     // the "int" part is in actArg->name

                     result += actArg->type + " " + actArg->name + " ";
                     found = true;
                  }
               }

            } else if (formArg.name == n && !formArg.defval.isEmpty() && formArg.defval != name) {
               /* to prevent recursion */

               result += substituteTemplateArgumentsInString(formArg.defval, formalArgs, actualArgs) + " ";
               found = true;
            }

         } else if (formArg.name == n && ! formArg.defval.isEmpty() && formArg.defval != name) {
            /* to prevent recursion */

            result += substituteTemplateArgumentsInString(formArg.defval, formalArgs, actualArgs) + " ";
            found = true;
         }

         if (act_iter != actualArgs->end()) {
            ++act_iter;
         }

      }

      if (! found) {
         result += n;
      }

      p = i + l;
   }

   result += name.right(name.length() - p);

   return result.trimmed();
}

/*! Makes a deep copy of the list of argument lists \a srcLists.
 *  Will allocate memory, that is owned by the caller.
 */
QList<ArgumentList> *copyArgumentLists(const QList<ArgumentList> *srcLists)
{
   assert(srcLists != 0);

   QList<ArgumentList> *dstLists = new QList<ArgumentList>;

   for (auto sl : *srcLists ) {
      dstLists->append(sl);
   }

   return dstLists;
}

/*! Strips template specifiers from scope \a fullName, except those
 *  that make up specialized classes. The switch \a parentOnly
 *  determines whether or not a template "at the end" of a scope
 *  should be considered, e.g. with \a parentOnly is \c true, A<T>::B<S> will
 *  try to strip \<T\> and not \<S\>, while \a parentOnly is \c false will
 *  strip both unless A<T> or B<S> are specialized template classes.
 */
QString stripTemplateSpecifiersFromScope(const QString &fullName, bool parentOnly, QString *pLastScopeStripped)
{
   QString result;

   int p = 0;
   int l = fullName.length();
   int i = fullName.indexOf('<');

   while (i != -1) {

      int e = i + 1;
      bool done = false;
      int count = 1;

      while (e < l && ! done) {
         QChar c = fullName.at(e++);

         if (c == '<') {
            count++;

         } else if (c == '>') {
            count--;
            done = count == 0;
         }
      }

      int si = fullName.indexOf("::", e);

      if (parentOnly && si == -1) {
         break;
      }
      // we only do the parent scope, so we stop here if needed

      result += fullName.mid(p, i - p);

      if (getClass(result + fullName.mid(i, e - i)) != 0) {
         result += fullName.mid(i, e - i);

      } else if (pLastScopeStripped) {
         *pLastScopeStripped = fullName.mid(i, e - i);
      }

      p = e;
      i = fullName.indexOf('<', p);
   }

   result += fullName.right(l - p);

   return result;
}

/*! Merges two scope parts together. The parts may (partially) overlap.
 *  Example1: \c A::B and \c B::C will result in \c A::B::C <br>
 *  Example2: \c A and \c B will be \c A::B <br>
 *  Example3: \c A::B and B will be \c A::B
 *
 *  @param leftScope the left hand part of the scope.
 *  @param rightScope the right hand part of the scope.
 *  @returns the merged scope.
 */
QString mergeScopes(const QString &leftScope, const QString &rightScope)
{
   // case leftScope=="A" rightScope=="A::B" => result = "A::B"

   if (leftScopeMatch(rightScope, leftScope)) {
      return rightScope;
   }

   QString result;
   int i = 0;
   int p = leftScope.length() - 1;

   // case leftScope=="A::B" rightScope=="B::C" => result = "A::B::C"
   // case leftScope=="A::B" rightScope=="B" => result = "A::B"

   bool found = false;
   while ((i = leftScope.lastIndexOf("::", p)) != -1) {
      if (leftScopeMatch(rightScope, leftScope.right(leftScope.length() - i - 2))) {
         result = leftScope.left(i + 2) + rightScope;
         found = true;
      }

      p = i - 1;
   }

   if (found) {
      return result;
   }

   // case leftScope=="A" rightScope=="B" => result = "A::B"
   result = leftScope;

   if (! result.isEmpty() && ! rightScope.isEmpty()) {
      result += "::";
   }

   result += rightScope;
   return result;
}

/*! Returns a fragment from scope \a s, starting at position \a p.
 *
 *  @param s the scope name as a string.
 *  @param p the start position (0 is the first).
 *  @param l the resulting length of the fragment.
 *  @returns the location of the fragment, or -1 if non is found.
 */
int getScopeFragment(const QString &str, int p, int *l)
{
   int sl = str.length();
   int sp = p;
   int count = 0;

   bool done;

   if (sp >= sl) {
      return -1;
   }

   while (sp < sl) {
      QChar c = str.at(sp);

      if (c == ':') {
         sp++;
         p++;

      } else {
         break;
      }
   }

   bool goAway = false;

   while (sp < sl) {
      QChar c = str.at(sp);

      switch (c.unicode()) {
         case ':':
            // found next part
            goAway = true;
            break;

         case '<':
            // skip template specifier
            count = 1;
            sp++;

            done = false;

            while (sp < sl && !done) {
               // TODO: deal with << and >> operators!
               QChar c = str.at(sp++);

               switch (c.unicode()) {
                  case '<':
                     count++;
                     break;

                  case '>':
                     count--;

                     if (count == 0) {
                        done = true;
                     }
                     break;

                  default:
                     break;
               }
            }
            break;

         default:
            sp++;
            break;
      }

      if (goAway) {
         break;
      }

   }

   *l = sp - p;

   return p;
}

QSharedPointer<PageDef> addRelatedPage(const QString &name, const QString &ptitle, const QString &doc, QList<SectionInfo> *,
                  const QString &fileName, int startLine, const QList<ListItemInfo> *sli, QSharedPointer<GroupDef> gd,
                  TagInfo *tagInfo, SrcLangExt lang)
{
   static int id = 1;

   QSharedPointer<PageDef> pd;

   if ((pd = Doxy_Globals::pageSDict->find(name)) && ! tagInfo) {
      // append documentation block to the page
      pd->setDocumentation(doc, fileName, startLine);

   } else {
      // new page
      QString baseName = name;

      if (baseName.right(4) == ".tex") {
         baseName = baseName.left(baseName.length() - 4);

      } else if (baseName.right(Doxy_Globals::htmlFileExtension.length()) == Doxy_Globals::htmlFileExtension) {
         baseName = baseName.left(baseName.length() - Doxy_Globals::htmlFileExtension.length());
      }

      QString title = ptitle.trimmed();
      pd = QMakeShared<PageDef>(fileName, startLine, baseName, doc, title);

      pd->setRefItems(sli);
      pd->setLanguage(lang);

      if (tagInfo) {
         pd->setReference(tagInfo->tagName);
         pd->setFileName(tagInfo->fileName, true);

      } else {
         pd->setFileName(qPrintable(convertNameToFile(pd->name(), false, true)), false);

      }

      pd->setInputOrderId(id);
      id++;

      Doxy_Globals::pageSDict->insert(baseName, pd);

      if (gd) {
         gd->addPage(pd);
      }

      if (! pd->title().isEmpty()) {
         // a page name is a label as well
         QString file;

         if (gd) {
            file = gd->getOutputFileBase();

         } else {
            file = pd->getOutputFileBase();

         }

         QSharedPointer<SectionInfo> si = Doxy_Globals::sectionDict->find(pd->name());

         if (si) {
            if (si->lineNr != -1) {
               warn(file, -1, "multiple use of section label '%s', (first occurrence: %s, line %d)",
                    qPrintable(pd->name()), qPrintable(si->fileName), si->lineNr);

            } else {
               warn(file, -1, "multiple use of section label '%s', (first occurrence: %s)",
                    qPrintable(pd->name()), qPrintable(si->fileName));
            }

         } else {
            si = QSharedPointer<SectionInfo>(new SectionInfo(file, -1, pd->name(), pd->title(), SectionInfo::Page, 0, pd->getReference()));
            Doxy_Globals::sectionDict->insert(pd->name(), si);
         }
      }
   }

   return pd;
}

void addRefItem(const QList<ListItemInfo> *sli, const QString &key, const QString &prefix,
                  const QString &name, const QString &title, const QString &args, QSharedPointer<Definition> scope)
{
   if (sli && ! key.isEmpty() && ! key.startsWith("@")) {
      // check for @ to skip anonymous stuff

      for (auto lii : *sli) {
         auto refList = Doxy_Globals::xrefLists->find(lii.type);

         if (refList != Doxy_Globals::xrefLists->end() && ( (lii.type != "todo" || Config::getBool("generate-todo-list")) &&
                          (lii.type != "test"       || Config::getBool("generate-test-list")) &&
                          (lii.type != "bug"        || Config::getBool("generate-bug-list"))  &&
                          (lii.type != "deprecated" || Config::getBool("generate-deprecate-list")) ) ) {

            // either not a built-in list or the list is enabled
            RefItem *item = refList->getRefItem(lii.itemId);
            assert(item != 0);

            item->prefix = prefix;
            item->scope  = scope;
            item->name   = name;
            item->title  = title;
            item->args   = args;

            refList->insertIntoList(key, item);
         }
      }
   }
}

bool recursivelyAddGroupListToTitle(OutputList &ol, QSharedPointer<Definition> d, bool root)
{
   SortedList<QSharedPointer<GroupDef>> *groups = d->partOfGroups();

   if (groups) {
      // write list of group to which this definition belongs

      if (root) {
         ol.pushGeneratorState();
         ol.disableAllBut(OutputGenerator::Html);
         ol.writeString("<div class=\"ingroups\">");
      }

      bool first = true;

      for (auto gd : *groups) {

         if (recursivelyAddGroupListToTitle(ol, gd, false)) {
            ol.writeString(" &raquo; ");
         }

         if (! first) {
            ol.writeString(" &#124; ");
         } else {
            first = false;
         }

         ol.writeObjectLink(gd->getReference(), gd->getOutputFileBase(), 0, gd->groupTitle());
      }

      if (root) {
         ol.writeString("</div>");
         ol.popGeneratorState();
      }

      return true;
   }
   return false;
}

void addGroupListToTitle(OutputList &ol, QSharedPointer<Definition> d)
{
   recursivelyAddGroupListToTitle(ol, d, true);
}

void filterLatexString(QTextStream &t, const QString &text, bool insideTabbing, bool insidePre, bool insideItem)
{
   static bool latexHyperPdf = Config::getBool("latex-hyper-pdf");

   if (text.isEmpty()) {
      return;
   }

   const QChar *p  = text.constData();

   int cnt;

   QChar c;
   QChar pc = '\0';

   while (*p != 0) {

      c = *p++;

      if (insidePre) {
         switch (c.unicode()) {
            case '\\':
               t << "\\(\\backslash\\)";
               break;

            case '{':
               t << "\\{";
               break;

            case '}':
               t << "\\}";
               break;

            case '_':
               t << "\\_";
               break;

            default:
               t << c;
         }

      } else {

         switch (c.unicode()) {
            case '#':
               t << "\\#";
               break;
            case '$':
               t << "\\$";
               break;

            case '%':
               t << "\\%";
               break;

            case '^':
               t << "$^\\wedge$";
               break;

            case '&':
               // might be a special symbol

               const QChar *ptr2;

               ptr2 = p;
               cnt  = 2;

               // we have to count & and ; as well
               while ((*ptr2 >= 'a' && *ptr2 <= 'z') || (*ptr2 >= 'A' && *ptr2 <= 'Z') || (*ptr2 >= '0' && *ptr2 <= '9')) {
                  cnt++;
                  ptr2++;
               }

               if (*ptr2 == ';')  {
                  // we need & as well
                  --p;

                  QString tmp = QString(p, cnt);
                  DocSymbol::SymType res = HtmlEntityMapper::instance()->name2sym(tmp);

                  if (res == DocSymbol::Sym_Unknown) {
                     p++;
                     t << "\\&";

                  } else {
                     t << HtmlEntityMapper::instance()->latex(res);
                     ptr2++;
                     p = ptr2;
                  }

               } else {
                  t << "\\&";

               }
               break;

            case '*':
               t << "$\\ast$";
               break;

            case '_':
               if (! insideTabbing) {
                  t << "\\+";
               }
               t << "\\_";

               if (! insideTabbing) {
                  t << "\\+";
               }
               break;

            case '{':
               t << "\\{";
               break;

            case '}':
               t << "\\}";
               break;

            case '<':
               t << "$<$";
               break;

            case '>':
               t << "$>$";
               break;

            case '|':
               t << "$\\vert$";
               break;

            case '~':
               t << "$\\sim$";
               break;

            case '[':
               if (latexHyperPdf || insideItem) {
                  t << "\\mbox{[}";
               } else {
                  t << "[";
               }
               break;

            case ']':
               if (pc == '[') {
                  t << "$\\,$";
               }

               if (latexHyperPdf || insideItem) {
                  t << "\\mbox{]}";
               } else {
                  t << "]";
               }
               break;

            case '-':
               t << "-\\/";
               break;

            case '\\':
               t << "\\textbackslash{}";
               break;

            case '"':
               t << "\\char`\\\"{}";
               break;

            case '\'':
               t << "\\textquotesingle{}";
               break;

            default:
               if (! insideTabbing) {
                  if ( (c >= 'A' && c <= 'Z' && pc != ' ' && pc != '\0' && *p != 0) || (c == ':' && pc != ':') || (pc == '.' && isId(c)) ) {
                     t << "\\+";
                  }
               }
               t << c;
         }
      }

      pc = c;
   }
}

QString rtfFormatBmkStr(const QString &key)
{
   // To overcome the 40-character tag limitation, we substitute a short arbitrary string for the name
   // supplied, and keep track of the correspondence between names and strings.

   static uint64_t nextTag = 0;
   static QHash<QString, uint64_t> tagDict;

   auto item = tagDict.find(key);

   if (item == tagDict.end()) {
      // this particular name has not yet been added to the list. Add it now and
      // associate it with the next tag value. then increment next tag

      tagDict.insert(key, nextTag);
      item = tagDict.find(key);

      // increment part
      nextTag++;
   }

   return QString::number(item.value(), 16);
}


bool checkExtension(const QString &fName, const QString &ext)
{
   return (fName.endsWith(ext));
}

QString stripExtensionGeneral(const QString &fName, const QString &ext)
{
   QString result = fName;

   if (result.endsWith(ext)) {
      result = result.left(result.length() - ext.length());
   }

   return result;
 }

QString stripExtension(const QString &fName)
{
   return stripExtensionGeneral(fName, Doxy_Globals::htmlFileExtension);
}

QString renameNS_Aliases(const QString &scope, bool fromTo)
{
   if (scope.isEmpty() || Doxy_Globals::nsRenameOrig.isEmpty() )  {
      return scope;
   }

   QString retval = scope;

   for (auto item = Doxy_Globals::nsRenameOrig.begin(); item != Doxy_Globals::nsRenameOrig.end(); item++) {

      QString from = item.key();
      QString to   = item.value();

      if (fromTo) {

         if (to.isEmpty()) {
            // do the same for now - this may be incorrect

            QStringList list = retval.split(" ");
            retval = "";

            for (auto str : list) {
               if (str.startsWith(from + "::") || str == from) {
                  str.replace(0, from.length(), to);
               }

               if (retval.isEmpty()) {
                  retval = str;

               } else {
                  retval = retval + " " + str;

               }
            }

         } else {
            QStringList list = retval.split(" ");
            retval = "";

            for (auto str : list) {
               if (str.startsWith(from + "::") || str == from) {
                  str.replace(0, from.length(), to);
               }

               if (retval.isEmpty()) {
                  retval = str;

               } else {
                  retval = retval + " " + str;

               }
            }
         }

      } else {
         // reverse used in docparser

         if (to.isEmpty()) {
            // hold on this for now

         } else {
            QStringList list = retval.split(" ");
            retval = "";

            for (auto str : list) {
               if (str.startsWith(to + "::") || str == to) {
                  str.replace(0, to.length(), from);
               }

               if (retval.isEmpty()) {
                  retval = str;

               } else {
                  retval = retval + " " + str;

               }
            }
         }
      }
   }

   return retval;
}

void replaceNamespaceAliases(QString &scope, int i)
{
   while (i > 0) {
      QString ns = scope.left(i);
      QString s  = Doxy_Globals::namespaceAliasDict[ns];

      if (! s.isEmpty()) {
         scope = s + scope.right(scope.length() - i);
         i = s.length();
      }

      if (i > 0 && ns == scope.left(i)) {
         break;
      }
   }
}

QString stripPath(const QString &s)
{
   QString result = s;

   int i = result.lastIndexOf('/');

   if (i != -1) {
      result = result.mid(i + 1);
   }

   i = result.lastIndexOf('\\');

   if (i != -1) {
      result = result.mid(i + 1);
   }

   return result;
}

/** returns \c true iff string \a s contains word \a w */
bool containsWord(const QString &s, const QString &word)
{
   static QRegExp wordExp("[a-z_A-Z\\x80-\\xFF]+");
   int p = 0, i, l;

   while ((i = wordExp.indexIn(s, p)) != -1) {
      l = wordExp.matchedLength();

      if (s.mid(i, l) == word) {
         return true;
      }

      p = i + l;
   }

   return false;
}

bool findAndRemoveWord(QString &str, const QString &word)
{
   static QRegExp wordExp("[a-z_A-Z\\x80-\\xFF]+");
   int p = 0;
   int i;
   int l;

   while ((i = wordExp.indexIn(str, p)) != -1) {
      l = wordExp.matchedLength();

      if (str.mid(i, l) == word) {

         if (i > 0 && str.at(i - 1).isSpace() ) {
            i--;
            l++;

         } else if (i + l < str.length() && str.at(i + l).isSpace() ) {
            l++;

         }

         str = str.left(i) + str.mid(i + l); // remove word + spacing
         return true;
      }

      p = i + l;

   }
   return false;
}

/** strips blank lines from the start and end of a string
 *  @param str the string to be stripped
 *  @param docLine the line number corresponding to the start of the string
 *         This will be adjusted based on the number of lines stripped from the start.
 *  @returns The stripped string.
 */
QString trimEmptyLines(const QString &str, int &docLine)
{
   if (str.isEmpty()) {
      return "";
   }

   const QChar *p = str.constData();
   const QChar *ptr = p;

   // search for leading empty lines
   int start = -1;
   int len   = str.length();

   QChar c;

   while ((c = *p++) != 0) {

      if (c == ' ' || c == '\t' || c == '\r') {
         // do nothing

      } else if (c == '\n') {
         start = (p - ptr);
         docLine++;

      } else {
         break;

      }
   }

   // search for trailing empty lines
   int end = -1;

   p = ptr + len - 1;

   while (p >= ptr) {
      c = *p;
      p--;

      if (c == ' ' || c == '\t' || c == '\r') {
         // do nothing

      } else if (c == '\n') {
         end = (p - ptr);

      } else {
         break;
      }
   }

   // return whole string if no leading or trailing lines where found
   if (start == -1 && end == -1) {
      return str;
   }

   // return substring
   if (end == -1) {
      end = len;
   }

   if (start == -1) {
      start = 0;
   }

   if (end <= start) {
      // only empty lines
      return "";
   }

   return str.mid(start, end - start + 1);
}

QSharedPointer<MemberDef> getMemberFromSymbol(QSharedPointer<Definition> scope, QSharedPointer<FileDef> fileScope, const QString &xName)
{
   QSharedPointer<MemberDef> bestMatch = QSharedPointer<MemberDef> ();
   QString name = xName;

   if (name.isEmpty()) {
      return bestMatch;
   }

   auto iter = Doxy_Globals::glossary().find(name);

   if (iter == Doxy_Globals::glossary().end()) {
      return bestMatch;
   }

   if (scope == nullptr || (scope->definitionType() != Definition::TypeClass &&
          scope->definitionType() != Definition::TypeNamespace) ) {

      scope = Doxy_Globals::globalScope;
   }

   QString explicitScopePart;
   int qualifierIndex = computeQualifiedIndex(name);

   if (qualifierIndex != -1) {
      explicitScopePart = name.left(qualifierIndex);
      replaceNamespaceAliases(explicitScopePart, explicitScopePart.length());
      name = name.mid(qualifierIndex + 2);
   }

   int minDistance = 10000;

   // find the closest matching definition
   while (iter != Doxy_Globals::glossary().end() && iter.key() == name)  {
      // search for the best match, only look at members

      if (iter.value()->definitionType() == Definition::TypeMember) {
         s_visitedNamespaces.clear();

         QSharedPointer<Definition> def = sharedFrom(iter.value());
         int distance = isAccessibleFromWithExpScope(scope, fileScope, def, explicitScopePart);

         if (distance != -1 && distance < minDistance) {
            minDistance = distance;

            QSharedPointer<Definition> def = sharedFrom(iter.value());
            QSharedPointer<MemberDef> md   = def.dynamicCast<MemberDef>();
            bestMatch = md;
         }
      }

      ++iter;
   }

   return bestMatch;
}

/*! Returns true iff the given name string appears to be a typedef in scope. */
bool checkIfTypedef(QSharedPointer<Definition> scope, QSharedPointer<FileDef> fileScope, const QString &name)
{
   QSharedPointer<MemberDef> bestMatch = getMemberFromSymbol(scope, fileScope, name);

   if (bestMatch && bestMatch->isTypedef()) {
      return true;   // closest matching definition is a typedef
   } else {
      return false;
   }
}

QString parseCommentAsText(QSharedPointer<const Definition> scope, QSharedPointer<const MemberDef> md,
                  const QString &doc, const QString &fileName, int lineNr)
{
   QString s;

   if (doc.isEmpty()) {
      return s;
   }

   {
      // need to remove the const, this should be reworked
      QSharedPointer<Definition> scope_unconst = scope.constCast<Definition>();


      // need to remove the const, this should be reworked
      QSharedPointer<MemberDef> md_unconst = md.constCast<MemberDef>();

      QTextStream t_stream(&s);
      DocNode *root = validatingParseDoc(fileName, lineNr, scope_unconst, md_unconst, doc, false, false);

      TextDocVisitor *visitor = new TextDocVisitor(t_stream);
      root->accept(visitor);

      delete visitor;
      delete root;
   }

   QString result = convertCharEntities(s);

   int len   = result.length();
   int count = len;

   if (count >= 80) {
      // try to truncate the string

      for (count = 80; count < 100; ++count)  {

         if (count == len) {
            break;
         }

         if (result.at(count) == ',' || result.at(count) == '.' || result.at(count) == '?') {
            break;
         }
      }
   }

   if (count < len) {
      result = result.left(count) + "...";
   }

   return result;
}

static QString expandAliasRec(const QString &s, bool allowRecursion = false);

struct Marker {
   Marker(int p, int n, int s) : pos(p), number(n), size(s) {}
   int pos;        // position in the string
   int number;     // argument number
   int size;       // size of the marker
};

/** For a string \a s that starts with a command name, returns the character
 *  offset within that string representing the first character after the
 *  command. For an alias with argument, this is the offset to the
 *  character just after the argument list.
 *
 *  Examples:
 *  - s=="a b"      returns 1
 *  - s=="a{2,3} b" returns 6
 *  = s=="#"        returns 0
 */
static int findEndOfCommand(const QString &str)
{
   int retval = 0;

   const QChar *data = str.constData();
   const QChar *ptr  = data;

   QChar c = *ptr;

   while (isId(c)) {
      ptr++;

      // next char
      c = *ptr;
   }

   if (c == '{') {
      QString args = extractAliasArgs(str, ptr - data);
      retval += args.length();
   }

   retval += ptr - data;

   return retval;
}

/** Replaces the markers in an alias definition \a aliasValue
 *  with the corresponding values found in the comma separated argument
 *  list \a argList and the returns the result after recursive alias expansion.
 */
static QString replaceAliasArguments(const QString &aliasValue, const QString &argList)
{
   // first make a list of arguments from the comma separated argument list
   QList<QString> args;

   int i;
   int l = argList.length();
   int s = 0;

   for (i = 0; i < l; i++) {
      QChar c = argList.at(i);

      if (c == ',' && (i == 0 || argList.at(i - 1) != '\\')) {
         args.append(argList.mid(s, i - s));
         s = i + 1; // start of next argument

      } else if (c == '@' || c == '\\') {
         // check if this is the start of another aliased command (see bug704172)
         i += findEndOfCommand(argList.mid(i + 1));
      }
   }

   if (l > s) {
      args.append(argList.right(l - s));
   }

   // next we look for the positions of the markers and add them to a list
   QList<Marker> markerList;

   l = aliasValue.length();
   int markerStart = 0;
   int markerEnd = 0;

   for (i = 0; i < l; i++) {
      if (markerStart == 0 && aliasValue.at(i) == '\\') { // start of a \xx marker
         markerStart = i + 1;

      } else if (markerStart > 0 && aliasValue.at(i) >= '0' && aliasValue.at(i) <= '9') {
         // read digit that make up the marker number
         markerEnd = i + 1;

      } else {
         if (markerStart > 0 && markerEnd > markerStart) { // end of marker
            int markerLen = markerEnd - markerStart;

            // include backslash
            markerList.append(Marker(markerStart - 1, aliasValue.mid(markerStart, markerLen).toInt(), markerLen + 1));
         }

         markerStart = 0; // outside marker
         markerEnd = 0;
      }
   }

   if (markerStart > 0) {
      markerEnd = l;
   }

   if (markerStart > 0 && markerEnd > markerStart) {
      int markerLen = markerEnd - markerStart;

      // include backslash
      markerList.append(Marker(markerStart - 1, aliasValue.mid(markerStart, markerLen).toInt(), markerLen + 1));

   }

   // then we replace the markers with the corresponding arguments in one pass
   QString result;
   int p = 0;

   for (auto m : markerList) {
      result += aliasValue.mid(p, m.pos - p);

      if (m.number > 0 && m.number <= args.count()) {
         // valid number
         result += expandAliasRec(args.at(m.number - 1), true);
      }

      p = m.pos + m.size; // continue after the marker
   }

   result += aliasValue.right(l - p); // append remainder

   // expand the result again
   result = substitute(result, "\\{", "{");
   result = substitute(result, "\\}", "}");
   result = expandAliasRec(substitute(result, "\\,", ","));

   return result;
}

static QString escapeCommas(const QString &s)
{
   QString retval = s;
   retval.replace(",", "\\,");

   return retval;
}

static QString expandAliasRec(const QString &s, bool allowRecursion)
{
   QString result;
   static QRegExp cmdPat("[\\\\@][a-z_A-Z][a-z_A-Z0-9]*");

   QString value = s;

   int i;
   int len;
   int p = 0;

   while ((i = cmdPat.indexIn(value, p)) != -1) {
      len = cmdPat.matchedLength();
      result += value.mid(p, i - p);

      QString args = extractAliasArgs(value, i + len);

      bool hasArgs = ! args.isEmpty();            // found directly after command
      int argsLen  =   args.length();

      QString cmd = value.mid(i + 1, len - 1);
      QString cmdNoArgs = cmd;

      int numArgs = 0;

      if (hasArgs) {
         numArgs = countAliasArguments(args);

         cmd = cmd + QString("{%1}").arg(numArgs);    // alias name + {n}
      }

      QString aliasText = Doxy_Globals::cmdAliasDict.value(cmd);

      if (numArgs > 1 &&  aliasText.isEmpty()) {
         // in case there is no command with numArgs parameters, but there is a command with 1 parameter
         // we also accept all text as the argument of that command (so you do not have to escape commas)

         aliasText = Doxy_Globals::cmdAliasDict.value(cmdNoArgs + "{1}");

         if (! aliasText.isEmpty()) {
            cmd  = cmdNoArgs + "{1}";
            args = escapeCommas(args);    // everything is seen as one argument
         }
      }

      if ((allowRecursion || ! s_aliasesProcessed.contains(cmd)) && ! aliasText.isEmpty()) {
         // expand the alias

         if (! allowRecursion) {
            s_aliasesProcessed.insert(cmd);
         }

         QString val = aliasText;
         if (hasArgs) {
            val = replaceAliasArguments(val, args);
         }

         result += expandAliasRec(val);
         if (! allowRecursion) {
            s_aliasesProcessed.remove(cmd);
         }

         p = i + len;
         if (hasArgs) {
            p += argsLen + 2;
         }

      } else {
         // command is not an alias
         result += value.mid(i, len);
         p = i + len;
      }
   }

   result += value.right(value.length() - p);

   return result;
}

int countAliasArguments(const QString &argList)
{
   int count = 1;
   int len   = argList.length();

   for (int k = 0; k < len; k++) {
      QChar c = argList.at(k);

      if (c == ',' && (k == 0 || argList.at(k - 1) != '\\')) {
         count++;

      } else if (c == '@' || c == '\\') {
         // check if this is the start of another aliased command (see bug704172)
         k += findEndOfCommand(argList.mid(k + 1));
      }
   }

   return count;
}

QString extractAliasArgs(const QString &args, int pos)
{
   int i;
   int bc = 0;

   QChar prevChar = 0;

   if (args.length() > pos && args.at(pos) == '{') {

      // alias has argument
      for (i = pos; i < args.length(); i++) {

         if (prevChar != '\\') {
            if (args.at(i) == '{') {
               bc++;
            }

            if (args.at(i) == '}') {
               bc--;
            }

            prevChar = args.at(i);

         } else {
            prevChar = 0;

         }

         if (bc == 0) {
            return args.mid(pos + 1, i - pos - 1);
         }
      }
   }

   return "";
}

QString resolveAliasCmd(const QString &aliasCmd)
{
   QString result;

   s_aliasesProcessed.clear();
   result = expandAliasRec(aliasCmd);

   return result;
}

QString expandAlias(const QString &aliasName, const QString &aliasValue)
{
   QString result;
   s_aliasesProcessed.clear();

   // avoid expanding this command recursively
   s_aliasesProcessed.insert(aliasName);

   // expand embedded commands
   result = expandAliasRec(aliasValue);

   return result;
}

void writeTypeConstraints(OutputList &ol, QSharedPointer<Definition> d, ArgumentList *al)
{
   if (al == nullptr || al->isEmpty() ) {
      return;
   }

   ol.startConstraintList(theTranslator->trTypeConstraints());

   for (auto a : *al) {
      ol.startConstraintParam();
      ol.parseText(a.name);
      ol.endConstraintParam();
      ol.startConstraintType();

      linkifyText(TextGeneratorOLImpl(ol), d, QSharedPointer<FileDef>(), QSharedPointer<Definition>(), a.type);

      ol.endConstraintType();
      ol.startConstraintDocs();
      ol.generateDoc(d->docFile(), d->docLine(), d, QSharedPointer<MemberDef>(), a.docs, true, false);
      ol.endConstraintDocs();
   }

   ol.endConstraintList();
}

void stackTrace()
{

#ifdef TRACINGSUPPORT
   void *backtraceFrames[128];
   int frameCount = backtrace(backtraceFrames, 128);
   static char cmd[40960];

   char *p = cmd;
   p += sprintf(p, "/usr/bin/atos -p %d ", (int)getpid());

   for (int x = 0; x < frameCount; x++) {
      p += sprintf(p, "%p ", backtraceFrames[x]);
   }

   fprintf(stderr, "========== STACKTRACE START ==============\n");
   if (FILE *fp = popen(cmd, "r")) {
      char resBuf[512];

      while (size_t len = fread(resBuf, 1, sizeof(resBuf), fp)) {
         fwrite(resBuf, 1, len, stderr);
      }

      pclose(fp);
   }

   fprintf(stderr, "============ STACKTRACE END ==============\n");
#endif
}

//! read a file name \a fileName and optionally filter and transcode it
QString readInputFile(const QString &fileName)
{
   QString retval;
   readInputFile(fileName, retval);

   return retval;
}

//! read a file name \a fileName and optionally filter and transcode it
bool readInputFile(const QString &fileName, QString &fileContents, bool filter, bool isSourceCode)
{
   int size = 0;

   QFileInfo fi(fileName);
   if (! fi.exists()) {
      return false;
   }

   QString filterName = getFileFilter(fileName, isSourceCode);
   QByteArray buffer;

   if (filterName.isEmpty() || ! filter) {
      QFile f(fileName);

      if (! f.open(QIODevice::ReadOnly)) {
         err("Unable to open file %s, error: %d\n", qPrintable(fileName), f.error());
         return false;
      }

      size = fi.size();
      buffer.resize(size);

      if (f.read(buffer.data(), size) != size) {
         err("Unable to read file %s, error: %d\n", qPrintable(fileName), f.error());
         return false;
      }

   } else {
      QString cmd = filterName + " \"" + fileName + "\"";
      Debug::print(Debug::ExtCmd, 0, "Executing popen(`%s`)\n", csPrintable(cmd));

      FILE *f = popen(qPrintable(cmd), "r");

      if (! f) {
         err("Unable to execute filer %s, error: %d\n", csPrintable(filterName));
         return false;
      }

      const int bufSize = 1024;
      char buf[bufSize];
      int numRead;

      while ((numRead = fread(buf, 1, bufSize, f)) > 0) {
         buffer.append(buf, numRead);
         size += numRead;
      }

      pclose(f);

      Debug::print(Debug::FilterOutput, 0, "Filter output\n");
      Debug::print(Debug::FilterOutput, 0, "-------------\n%s\n-------------\n", buffer.constData() );
   }

   if (size >= 2 && ((buffer.at(0) == -1 && buffer.at(1) == -2) || (buffer.at(0) == -2 && buffer.at(1) == -1) )) {
      // UCS-2 encoded file
      fileContents = QTextCodec::codecForMib(1015)->toUnicode(buffer);

   } else if (size >= 3 && (uchar)buffer.at(0) == 0xEF && (uchar)buffer.at(1) == 0xBB && (uchar)buffer.at(2) == 0xBF) {

      // UTF-8 encoded file, remove UTF-8 BOM: no translation needed
      buffer = buffer.mid(3);
      fileContents  = QString::fromUtf8(buffer);

   } else {
      // transcode according to the INPUT_ENCODING setting
      // do character transcoding if needed.

      fileContents = transcodeToQString(buffer);
   }

   // translate CR's
   fileContents = filterCRLF(fileContents);

   return true;
}

// Replace %word by word in title
QString filterTitle(const QString &title)
{
   QString tf;

   static QRegExp re("%[A-Z_a-z]");
   int p = 0, i, l;

   while ((i = re.indexIn(title, p)) != -1) {
      l = re.matchedLength();

      tf += title.mid(p, i - p);
      tf += title.mid(i + 1, l - 1); // skip %

      p = i + l;
   }

   tf += title.right(title.length() - p);

   return tf;
}

// returns true if the name of the file represented by `fi' matches
// one of the file patterns in the `patList' list

bool patternMatch(const QFileInfo &fi, const QStringList &patList)
{
   bool found = false;

   QString fn  = fi.fileName();
   QString fp  = fi.filePath();
   QString afp = fi.absoluteFilePath();

   for (auto pattern : patList) {

      if (! pattern.isEmpty()) {
         int i = pattern.indexOf('=');

         if (i != -1) {
            pattern = pattern.left(i);   // strip off the extension
         }

#if defined(_WIN32) || defined(__MACOSX__)
         // Windows or MacOSX
         QRegExp re(pattern, Qt::CaseInsensitive, QRegExp::Wildcard);
#else
         // unix
         QRegExp re(pattern, Qt::CaseSensitive, QRegExp::Wildcard);
#endif

         // input-patterns
         // possilbe issue if the pattern has something other than a wildcard for the name
         // found = re.indexIn(fn) != -1 || re.indexIn(fp) != -1 || re.indexIn(afp) != -1;

         found = re.exactMatch(fn) || re.exactMatch(fp) || re.exactMatch(afp);

         if (found) {
            break;
         }
      }

   }

   return found;
}

QString externalLinkTarget()
{
   static bool extLinksInWindow = Config::getBool("external-links-in-window");

   if (extLinksInWindow) {
      return "target=\"_blank\" ";

   } else {
      return "";
   }
}

QString externalRef(const QString &relPath, const QString &ref, bool href)
{
   QString result;

   if (! ref.isEmpty()) {
      QString dest = Doxy_Globals::tagDestinationDict[ref];

      if (! dest.isEmpty()) {
         result  = dest;
         int len = result.length();

         if (! relPath.isEmpty() && len > 0 && result.at(0) == '.') {
            // relative path -> prepend relPath.
            result.prepend(relPath);
         }

         if (! href) {
            result.prepend("doxypress=\"" + ref + ":");
         }

         if (len > 0 && result.at(len - 1) != '/') {
            result += '/';
         }

         if (! href) {
            result.append("\" ");
         }
      }


   } else {
      result = relPath;

   }

   return result;
}

/** Writes the intensity only bitmap representated by \a data as an image to
 *  directory \a dir using the colors defined by html_colorstyle.
 */
void writeColoredImgData(ColoredImgDataItem data)
{
   static int hue   = Config::getInt("html-colorstyle-hue");
   static int sat   = Config::getInt("html-colorstyle-sat");
   static int gamma = Config::getInt("html-colorstyle-gamma");

   QString fileName = data.path + "/" + data.name;
   QFile f(fileName);

   if (f.open(QIODevice::WriteOnly)) {

      ColoredImage image(data.width, data.height, data.content, data.alpha, sat, hue, gamma);
      QByteArray buffer = image.convert();

      if (f.write(buffer) == -1) {
         err("Unable to write file %s, error: %d\n", qPrintable(fileName), f.error());
      }

      f.close();

   } else {
      err("Unable to save image file %s, error: %d\n", qPrintable(fileName), f.error());

   }

   Doxy_Globals::indexList->addImageFile(data.name);

}

/** Replaces any markers of the form \#\#AA in input string \a str
 *  by new markers of the form \#AABBCC, where \#AABBCC represents a
 *  valid color, based on the intensity represented by hex number AA
 *  and the current html_colorstyle  settings.
 */
QString replaceColorMarkers(const QString &str)
{
   QString result = str;

   if (result.isEmpty()) {
      return result;
   }

   static QRegExp re("##([0-9A-Fa-f][0-9A-Fa-f])");

   static int hue   = Config::getInt("html-colorstyle-hue");
   static int sat   = Config::getInt("html-colorstyle-sat");
   static int gamma = Config::getInt("html-colorstyle-gamma");

   int startPos = 0;
   int len = result.length();

   while (re.indexIn(result, startPos) != -1) {

      QString tempColor = re.cap(1);
      int level = tempColor.toInt(nullptr, 16);

      double r, g, b;
      int red, green, blue;

      ColoredImage::hsl2rgb(hue / 360.0, sat / 255.0, pow(level / 255.0, gamma / 100.0), &r, &g, &b);

      red   = (int)(r * 255.0);
      green = (int)(g * 255.0);
      blue  = (int)(b * 255.0);

      QString colorStr = "#%1%2%3";
      colorStr = colorStr.arg(red, 2, 16, QChar('0')).arg(green, 2, 16, QChar('0')).arg(blue, 2, 16, QChar('0'));
      result.replace(re.pos(0), re.matchedLength(), colorStr);

      //
      startPos = re.pos(0) + colorStr.length();
   }

   return result;
}

/** Copies the contents of file with name \a src to the newly created
 *  file with name \a dest. Returns true if successful.
 */
bool copyFile(const QString &src, const QString &dest)
{
   QFile sf(src);

   if (sf.open(QIODevice::ReadOnly)) {
      QFileInfo fi(src);
      QFile df(dest);

      if (df.open(QIODevice::WriteOnly)) {
         char *buffer = new char[fi.size()];
         sf.read(buffer, fi.size());

         df.write(buffer, fi.size());
         df.flush();

         delete[] buffer;

      } else {
         err("Unable to open file for writing %s, error: %d\n", qPrintable(dest), df.error());
         return false;
      }

   } else {
      err("Unable to open file for reading %s, error: %d\n", qPrintable(src), sf.error());
      return false;
   }

   return true;
}

/** Returns the section of text, in between a pair of markers.
 *  Full lines are returned, excluding the lines on which the markers appear.
 */
QString extractBlock(const QString &text, const QString &marker)
{
   QString result;

   int p = 0;
   int i;
   bool found = false;

   // find the character positions of the markers
   int m1 = text.indexOf(marker);
   if (m1 == -1) {
      return result;
   }

   int m2 = text.indexOf(marker, m1 + marker.length());
   if (m2 == -1) {
      return result;
   }

   // find start and end line positions for the markers
   int l1 = -1;
   int l2 = -1;

   while (! found && (i = text.indexOf('\n', p)) != -1) {
      found = (p <= m1 && m1 < i); // found the line with the start marker
      p = i + 1;
   }

   l1 = p;
   int lp = i;

   if (found) {
      while ((i = text.indexOf('\n', p)) != -1) {
         if (p <= m2 && m2 < i) { // found the line with the end marker
            l2 = p;
            break;
         }

         p = i + 1;
         lp = i;
      }
   }

   if (l2 == -1) { // marker at last line without newline (see bug706874)
      l2 = lp;
   }

   return l2 > l1 ? text.mid(l1, l2 - l1) : QString();
}

/** Returns a string representation of \a lang. */
QString langToString(SrcLangExt lang)
{
   switch (lang) {
      case SrcLangExt_Unknown:
         return "Unknown";

      case SrcLangExt_IDL:
         return "IDL";

      case SrcLangExt_Java:
         return "Java";

      case SrcLangExt_CSharp:
         return "C#";

      case SrcLangExt_D:
         return "D";

      case SrcLangExt_PHP:
         return "PHP";

      case SrcLangExt_ObjC:
         return "Objective-C";

      case SrcLangExt_Cpp:
         return "C++";

      case SrcLangExt_JS:
         return "Javascript";

      case SrcLangExt_Python:
         return "Python";

      case SrcLangExt_Fortran:
         return "Fortran";

      case SrcLangExt_XML:
         return "XML";

      case SrcLangExt_Tcl:
         return "Tcl";

      case SrcLangExt_Markdown:
         return "Markdown";
   }

   return "Unknown";
}

/** Returns the scope separator to use given the programming language \a lang */
QString getLanguageSpecificSeparator(SrcLangExt lang, bool classScope)
{
   if (lang == SrcLangExt_Java || lang == SrcLangExt_CSharp || lang == SrcLangExt_Python) {
      return ".";

   } else if (lang == SrcLangExt_PHP && !classScope) {
      return "\\";

   } else {
      return "::";

   }
}

/** Corrects URL \a url according to the relative path \a relPath.
 *  Returns the corrected URL. For absolute URLs no correction will be done.
 */
QString correctURL(const QString &url, const QString &relPath)
{
   QString result = url;

   if (! relPath.isEmpty() && ! url.startsWith("http:") && ! url.startsWith("https:") &&
            ! url.startsWith("ftp:") && ! url.startsWith("file:")) {

      result.prepend(relPath);
   }

   return result;
}

bool protectionLevelVisible(Protection prot)
{
   static bool extractPrivate = Config::getBool("extract-private");
   static bool extractPackage = Config::getBool("extract-package");

   return (prot != Private && prot != Package)  || (prot == Private && extractPrivate) || (prot == Package && extractPackage);
}

QString stripIndentation(const QString &s)
{
   if (s.isEmpty()) {
      return s;
   }

   // compute minimum indentation over all lines

   int indent    = 0;
   int minIndent = 1000000;       // "infinite"
   bool searchIndent = true;

   static int tabSize = Config::getInt("tab-size");

   for (auto c : s) {
      if (c == '\t') {
         indent += tabSize - (indent % tabSize);

      } else if (c == '\n') {
         indent = 0, searchIndent = true;

      } else if (c == ' ') {
         indent++;

      } else if (searchIndent) {
         searchIndent = false;

         if (indent < minIndent) {
            minIndent = indent;
         }
      }
   }

   // no indent to remove -> we are done
   if (minIndent == 0) {
      return s;
   }

   // remove minimum indentation for each line
   QString result;
   indent = 0;

   for (auto c : s) {
      if (c == '\n') { // start of new line
         indent = 0;
         result += c;

      } else if (indent < minIndent) { // skip until we reach minIndent
         if (c == '\t') {
            int newIndent = indent + tabSize - (indent % tabSize);
            int i = newIndent;

            while (i > minIndent) { // if a tab crosses the minIndent boundary fill the rest with spaces
               result += ' ';
               i--;
            }
            indent = newIndent;

         } else { // space
            indent++;
         }

      } else { // copy anything until the end of the line
         result += c;
      }
   }

   return result;
}

bool srcFileVisibleInIndex(QSharedPointer<FileDef> fd)
{
   return fd->isDocumentationFile() && fd->generateSourceFile();
}

bool docFileVisibleInIndex(QSharedPointer<FileDef> fd)
{
   static bool allExternals = Config::getBool("all-externals");

   bool retval = ( (allExternals && fd->isLinkable()) || fd->isLinkableInProject() ) && fd->isDocumentationFile();

   return retval;
}

void addDocCrossReference(QSharedPointer<MemberDef> src, QSharedPointer<MemberDef> dst)
{
   static bool referencedByRelation = Config::getBool("ref-by-relation");
   static bool referencesRelation   = Config::getBool("ref-relation");

   if (dst->isTypedef() || dst->isEnumerate()) {
      return;   // do not add types
   }

   if ((referencedByRelation || dst->hasCallerGraph()) && src->showInCallGraph() ) {
      dst->addSourceReferencedBy(src);
      QSharedPointer<MemberDef> mdDef = dst->memberDefinition();

      if (mdDef) {
         mdDef->addSourceReferencedBy(src);
      }

      QSharedPointer<MemberDef> mdDecl = dst->memberDeclaration();
      if (mdDecl) {
         mdDecl->addSourceReferencedBy(src);
      }
   }

   if ((referencesRelation || src->hasCallGraph()) && src->showInCallGraph()) {
      src->addSourceReferences(dst);
      QSharedPointer<MemberDef> mdDef = src->memberDefinition();

      if (mdDef) {
         mdDef->addSourceReferences(dst);
      }

      QSharedPointer<MemberDef> mdDecl = src->memberDeclaration();

      if (mdDecl) {
         mdDecl->addSourceReferences(dst);
      }
   }
}


uint getUtf8Code( const QString &s, int idx )
{
   return s[idx].unicode();
}

uint getUtf8CodeToLower( const QString &s, int idx )
{
   return s[idx].toLower().unicode();
}

uint getUtf8CodeToUpper( const QString &s, int idx )
{
   return s[idx].toUpper().unicode();
}

bool namespaceHasVisibleChild(QSharedPointer<NamespaceDef> nd, bool includeClasses)
{
   if (nd->getNamespaceSDict()) {

      for (auto cnd : *nd->getNamespaceSDict()) {
         if (cnd->isLinkableInProject() && cnd->localName().indexOf('@') == -1) {
            return true;

         } else if (namespaceHasVisibleChild(cnd, includeClasses)) {
            return true;
         }
      }
   }

   if (includeClasses && nd->getClassSDict()) {
      for (auto cd : *nd->getClassSDict()) {
         if (cd->isLinkableInProject() && cd->templateMaster() == 0) {
            return true;
         }
      }
   }

   return false;
}

bool classVisibleInIndex(QSharedPointer<ClassDef> cd)
{
   static bool allExternals = Config::getBool("all-externals");
   return (allExternals && cd->isLinkable()) || cd->isLinkableInProject();
}

QByteArray extractDirection(QString docs)
{
   QRegExp re("\\[[^\\]]+\\]");
   int len = 0;

   if (re.indexIn(docs, 0) == 0) {
      len = re.matchedLength();

      int  inPos  = docs.indexOf("in", 1, Qt::CaseInsensitive);
      int outPos  = docs.indexOf("out", 1, Qt::CaseInsensitive);

      bool input  = inPos != -1 &&  inPos < len;
      bool output = outPos != -1 && outPos < len;

      if (input || output) {
         // in,out attributes
         docs = docs.mid(len); // strip attributes

         if (input && output) {
            return "[in,out]";

         } else if (input) {
            return "[in]";

         } else if (output) {
            return "[out]";
         }
      }
   }
   return QByteArray();
}

/** Computes for a given list type \a inListType, which are the
 *  the corresponding list type(s) in the base class that are to be
 *  added to this list.
 *
 *  So for public inheritance, the mapping is 1-1, so outListType1=inListType
 *  Private members are to be hidden completely.
 *
 *  For protected inheritance, both protected and public members of the
 *  base class should be joined in the protected member section.
 *
 *  For private inheritance, both protected and public members of the
 *  base class should be joined in the private member section.
 */
void convertProtectionLevel(MemberListType inListType, Protection inProt, int *outListType1, int *outListType2)
{
   static bool extractPrivate = Config::getBool("extract-private");

   // default representing 1-1 mapping
   *outListType1 = inListType;
   *outListType2 = -1;

   if (inProt == Public) {
      switch (inListType) // in the private section of the derived class,
         // the private section of the base class should not be visible
      {
         case MemberListType_priMethods:
         case MemberListType_priStaticMethods:
         case MemberListType_priSlots:
         case MemberListType_priAttribs:
         case MemberListType_priStaticAttribs:
         case MemberListType_priTypes:
            *outListType1 = -1;
            *outListType2 = -1;
            break;
         default:
            break;
      }

   } else if (inProt == Protected) { // Protected inheritance
      switch (inListType)
         // in the protected section of the derived class,
         // both the public and protected members are shown as protected
      {
         case MemberListType_pubMethods:
         case MemberListType_pubStaticMethods:
         case MemberListType_pubSlots:
         case MemberListType_pubAttribs:
         case MemberListType_pubStaticAttribs:
         case MemberListType_pubTypes:
         case MemberListType_priMethods:
         case MemberListType_priStaticMethods:
         case MemberListType_priSlots:
         case MemberListType_priAttribs:
         case MemberListType_priStaticAttribs:
         case MemberListType_priTypes:
            *outListType1 = -1;
            *outListType2 = -1;
            break;

         case MemberListType_proMethods:
            *outListType2 = MemberListType_pubMethods;
            break;
         case MemberListType_proStaticMethods:
            *outListType2 = MemberListType_pubStaticMethods;
            break;
         case MemberListType_proSlots:
            *outListType2 = MemberListType_pubSlots;
            break;
         case MemberListType_proAttribs:
            *outListType2 = MemberListType_pubAttribs;
            break;
         case MemberListType_proStaticAttribs:
            *outListType2 = MemberListType_pubStaticAttribs;
            break;
         case MemberListType_proTypes:
            *outListType2 = MemberListType_pubTypes;
            break;
         default:
            break;
      }

   } else if (inProt == Private) {
      switch (inListType) // in the private section of the derived class,
         // both the public and protected members are shown
         // as private
      {
         case MemberListType_pubMethods:
         case MemberListType_pubStaticMethods:
         case MemberListType_pubSlots:
         case MemberListType_pubAttribs:
         case MemberListType_pubStaticAttribs:
         case MemberListType_pubTypes:
         case MemberListType_proMethods:
         case MemberListType_proStaticMethods:
         case MemberListType_proSlots:
         case MemberListType_proAttribs:
         case MemberListType_proStaticAttribs:
         case MemberListType_proTypes:
            *outListType1 = -1;
            *outListType2 = -1;
            break;

         case MemberListType_priMethods:
            if (extractPrivate) {
               *outListType1 = MemberListType_pubMethods;
               *outListType2 = MemberListType_proMethods;
            } else {
               *outListType1 = -1;
               *outListType2 = -1;
            }
            break;
         case MemberListType_priStaticMethods:
            if (extractPrivate) {
               *outListType1 = MemberListType_pubStaticMethods;
               *outListType2 = MemberListType_proStaticMethods;
            } else {
               *outListType1 = -1;
               *outListType2 = -1;
            }
            break;
         case MemberListType_priSlots:
            if (extractPrivate) {
               *outListType1 = MemberListType_pubSlots;
               *outListType2 = MemberListType_proSlots;
            } else {
               *outListType1 = -1;
               *outListType2 = -1;
            }
            break;
         case MemberListType_priAttribs:
            if (extractPrivate) {
               *outListType1 = MemberListType_pubAttribs;
               *outListType2 = MemberListType_proAttribs;
            } else {
               *outListType1 = -1;
               *outListType2 = -1;
            }
            break;
         case MemberListType_priStaticAttribs:
            if (extractPrivate) {
               *outListType1 = MemberListType_pubStaticAttribs;
               *outListType2 = MemberListType_proStaticAttribs;
            } else {
               *outListType1 = -1;
               *outListType2 = -1;
            }
            break;
         case MemberListType_priTypes:
            if (extractPrivate) {
               *outListType1 = MemberListType_pubTypes;
               *outListType2 = MemberListType_proTypes;
            } else {
               *outListType1 = -1;
               *outListType2 = -1;
            }
            break;
         default:
            break;
      }
   }
}

bool mainPageHasTitle()
{
   if (Doxy_Globals::mainPage == nullptr) {
      return false;
   }

   if (Doxy_Globals::mainPage->title().isEmpty()) {
      return false;
   }

   if (Doxy_Globals::mainPage->title().toLower() == "notitle") {
      return false;
   }

   return true;
}

QString stripPrefix(QString input, const QByteArray &prefix)
{
   QString retval = input;

   if (input.startsWith(prefix)) {
      retval = retval.remove(0, strlen(prefix));
   }

   return retval;
}

QByteArray stripPrefix(QByteArray input, const QByteArray &prefix)
{
   QByteArray retval = input;

   if (input.startsWith(prefix)) {
      retval = retval.remove(0, strlen(prefix));
   }

   return retval;
}

Protection getProtection(const QString &data)
{
   Protection retval;

   QString visibility = data.toLower();

   if (visibility == "public") {
      retval = Public;

   } else if (visibility == "protected") {
      retval = Protected;

   } else if (visibility == "private") {
      retval = Private;

   }

   return retval;
}
