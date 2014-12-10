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

#include <QDir>
#include <QHash>

#include <stdio.h>

#include <htags.h>
#include <util.h>
#include <message.h>
#include <config.h>
#include <portable.h>

bool Htags::useHtags = false;

static QDir g_inputDir;
static QHash<QString, QByteArray> g_symbolDict;

/*! constructs command line of htags(1) and executes it.
 *  \retval true success
 *  \retval false an error has occurred.
 */
bool Htags::execute(const QByteArray &htmldir)
{
   static QStringList &inputSource = Config_getList("INPUT");

   static bool quiet    = Config_getBool("QUIET");
   static bool warnings = Config_getBool("WARNINGS");

   static QByteArray htagsOptions  = ""; //Config_getString("HTAGS_OPTIONS");
   static QByteArray projectName   = Config_getString("PROJECT_NAME");
   static QByteArray projectNumber = Config_getString("PROJECT_NUMBER");

   QByteArray cwd = QDir::currentPath().toUtf8();
   if (inputSource.isEmpty()) {
      g_inputDir.setPath(cwd);

   } else if (inputSource.count() == 1) {
      g_inputDir.setPath(inputSource.first());

      if (! g_inputDir.exists())
         err("Cannot find directory %s. Check the value of the INPUT tag in the configuration file.\n", qPrintable(inputSource.first()) );

   } else {
      err("If you use USE_HTAGS then INPUT should specific a single directory. \n");
      return false;
   }

   /*
    * Construct command line for htags(1).
    */
   QByteArray commandLine = " -g -s -a -n ";
   if (!quiet) {
      commandLine += "-v ";
   }

   if (warnings) {
      commandLine += "-w ";
   }

   if (!htagsOptions.isEmpty()) {
      commandLine += ' ';
      commandLine += htagsOptions;
   }

   if (!projectName.isEmpty()) {
      commandLine += "-t \"";
      commandLine += projectName;

      if (!projectNumber.isEmpty()) {
         commandLine += '-';
         commandLine += projectNumber;
      }
      commandLine += "\" ";
   }

   commandLine += " \"" + htmldir + "\"";

   QByteArray oldDir = QDir::currentPath().toUtf8();
   QDir::setCurrent(g_inputDir.absolutePath());

   //printf("CommandLine=[%s]\n",commandLine.data());
   portable_sysTimerStart();

   bool result = portable_system("htags", commandLine, false) == 0;
   portable_sysTimerStop();

   QDir::setCurrent(oldDir);

   return result;
}


/*! load filemap and make index.
 *  \param htmlDir of HTML directory generated by htags(1).
 *  \retval true success
 *  \retval false error
 */
bool Htags::loadFilemap(const QByteArray &htmlDir)
{
   QByteArray fileMapName = htmlDir + "/HTML/FILEMAP";
   QByteArray fileMap;

   QFileInfo fi(fileMapName);

   /*
    * Construct FILEMAP dictionary using QHash.
    *
    * In FILEMAP, URL includes 'html' suffix but we cut it off according
    * to the method of FileDef class.
    *
    * FILEMAP format:
    * <NAME>\t<HREF>.html\n
    * QDICT:
    * dict[<NAME>] = <HREF>
    */

   if (fi.exists() && fi.isReadable()) {
      QFile f(fileMapName);
     
      QByteArray line;     

      if (f.open(QIODevice::ReadOnly)) {

         while (! (line = f.readLine()).isEmpty()) {
           
            int sep = line.indexOf('\t');

            if (sep != -1) {
               QByteArray key   = line.left(sep).trimmed();
               QByteArray value = line.mid(sep + 1).trimmed();

               int ext = value.lastIndexOf('.');

               if (ext != -1) {
                  value = value.left(ext);   // strip extension
               }
              
               g_symbolDict.insert(key, value);               
            }
         }
         return true;

      } else {
         err("file %s cannot be opened\n", fileMapName.data());
      }
   }
   return false;
}

/*! convert path name into the url in the hypertext generated by htags.
 *  \param path path name
 *  \returns URL NULL: not found.
 */
QByteArray Htags::path2URL(const QByteArray &path)
{
   QByteArray url;
   QByteArray symName = path;
   QByteArray dir = g_inputDir.absolutePath().toUtf8();

   int dl = dir.length();

   if (symName.length() > dl + 1) {
      symName = symName.mid(dl + 1);
   }

   if (! symName.isEmpty()) {
      QByteArray result = g_symbolDict[symName];

      if (! result.isEmpty()) {
         url = "HTML/" + result;
      }
   }
   return url;
}

