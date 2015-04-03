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

#include <QTextCodec>
#include <QTextStream>

#include <errno.h>
#include <locale.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <unistd.h>

#include <cite.h>
#include <classlist.h>
#include <cmdmapper.h>
#include <code.h>
#include <config.h>
#include <doxy_setup.h>
#include <doxy_build_info.h>
#include <entry.h>
#include <filename.h>
#include <filestorage.h>
#include <formula.h>
#include <fortranscanner.h>
#include <groupdef.h>
#include <htmlgen.h>
#include <index.h>
#include <language.h>
#include <latexgen.h>
#include <layout.h>
#include <markdown.h>
#include <membername.h>
#include <namespacedef.h>
#include <outputlist.h>
#include <parser_file.h>
#include <parser_make.h>
#include <parser_py.h>
#include <pagedef.h>
#include <perlmodgen.h>
#include <portable.h>
#include <pre.h>
#include <reflist.h>
#include <rtfgen.h>
#include <scanner.h>
#include <tclscanner.h>
#include <util.h>

// globals
#include <doxy_globals.h>

namespace Doxy_Setup {
   QString getValue(QStringList::iterator &iter, QStringList::iterator end);   
   bool openOutputFile(const QString &outFile, QFile &f);   
   void usage();
}

using namespace Doxy_Setup;

enum Options {
     INVALID, 
     BLANK_LAYOUT,
     BLANK_STYLE,
     DEBUG_DUMP, 
     DEBUG_SYMBOLS,
     HELP, 
     OUTPUT_APP,
     RTF_EXTENSIONS,            
     DOXY_VERSION,      
};

void initDoxyPress()
{
   printf("Initialization\n"); 

   const char *lang = portable_getenv("LC_ALL");

   if (lang) {
      portable_setenv("LANG", lang);
   }

   setlocale(LC_ALL, "");
   setlocale(LC_CTYPE, "C");       // to get isspace(0xA0)==0, needed for UTF-8
   setlocale(LC_NUMERIC, "C");

   QTextCodec::setCodecForLocale(QTextCodec::codecForName("UTF-8"));

   Doxygen::runningTime.start();
   initPreprocessor();

   Doxygen::parserManager = new ParserManager;
   Doxygen::parserManager->registerDefaultParser(new FileParser);
   Doxygen::parserManager->registerParser("c", new CLanguageScanner);

   Doxygen::parserManager->registerParser("python",       new PythonLanguageScanner);

   Doxygen::parserManager->registerParser("fortran",      new FortranLanguageScanner);
   Doxygen::parserManager->registerParser("fortranfree",  new FortranLanguageScannerFree);
   Doxygen::parserManager->registerParser("fortranfixed", new FortranLanguageScannerFixed);

// unsupported feature (broom, hold)
// Doxygen::parserManager->registerParser("dbusxml",      new DBusXMLScanner);

   Doxygen::parserManager->registerParser("tcl",          new TclLanguageScanner);

   Doxygen::parserManager->registerParser("md",           new MarkdownFileParser);
   Doxygen::parserManager->registerParser("make",         new MakeFileParser);

   // register any additional parsers here
   initDefaultExtensionMapping();

   initClassMemberIndices();
   initNamespaceMemberIndices();
   initFileMemberIndices();
 
   Doxygen::inputNameList     = new SortedList<QSharedPointer<FileName>>;

   Doxygen::memberNameSDict   = new MemberNameSDict();
   Doxygen::functionNameSDict = new MemberNameSDict();
   Doxygen::groupSDict        = new GroupSDict();

   Doxygen::globalScope       = QMakeShared<NamespaceDef>("<globalScope>", 1, 1, "<globalScope>");

   Doxygen::namespaceSDict    = new NamespaceSDict();
   Doxygen::classSDict        = new ClassSDict();
   Doxygen::hiddenClasses     = new ClassSDict();  
   Doxygen::pageSDict         = new PageSDict();          // all doc pages
   Doxygen::exampleSDict      = new PageSDict();          // all examples

   Doxygen::inputNameDict     = new FileNameDict();
   Doxygen::includeNameDict   = new FileNameDict();
   Doxygen::exampleNameDict   = new FileNameDict();
   Doxygen::imageNameDict     = new FileNameDict();
   Doxygen::dotFileNameDict   = new FileNameDict();
   Doxygen::mscFileNameDict   = new FileNameDict();
   Doxygen::diaFileNameDict   = new FileNameDict();
   Doxygen::citeDict          = new CiteDict();
   Doxygen::genericsDict      = new GenericsSDict;
   Doxygen::indexList         = new IndexList;
   Doxygen::formulaList       = new FormulaList;
   Doxygen::formulaDict       = new FormulaDict();
   Doxygen::formulaNameDict   = new FormulaDict();
   Doxygen::sectionDict       = new SectionDict();

   // Initialize global constants
   Doxy_Globals::g_compoundKeywordDict.insert("template class");
   Doxy_Globals::g_compoundKeywordDict.insert("template struct");
   Doxy_Globals::g_compoundKeywordDict.insert("class");
   Doxy_Globals::g_compoundKeywordDict.insert("struct");
   Doxy_Globals::g_compoundKeywordDict.insert("union");
   Doxy_Globals::g_compoundKeywordDict.insert("interface");
   Doxy_Globals::g_compoundKeywordDict.insert("exception");
}

void shutDownDoxypress()
{
   finializeSearchIndexer();
   Doxygen::symbolStorage->close();

   QDir thisDir;
   thisDir.remove(Doxygen::objDBFileName);

   //
   delete Doxygen::sectionDict;
   delete Doxygen::formulaNameDict;
   delete Doxygen::formulaDict;
   delete Doxygen::formulaList;
   delete Doxygen::indexList;
   delete Doxygen::genericsDict;
   delete Doxygen::inputNameDict;
   delete Doxygen::includeNameDict;
   delete Doxygen::exampleNameDict;
   delete Doxygen::imageNameDict;
   delete Doxygen::dotFileNameDict;
   delete Doxygen::mscFileNameDict;
   delete Doxygen::diaFileNameDict;  
   delete Doxygen::pageSDict;
   delete Doxygen::exampleSDict;  
   delete Doxygen::xrefLists;
   delete Doxygen::parserManager;
   
   removePreProcessor();

   delete theTranslator;
   delete Doxy_Globals::g_outputList;

   Mappers::freeMappers();
   codeFreeScanner();
   
   delete Doxygen::inputNameList;
   delete Doxygen::memberNameSDict;
   delete Doxygen::functionNameSDict;
   delete Doxygen::groupSDict;
   delete Doxygen::classSDict;
   delete Doxygen::hiddenClasses;
   delete Doxygen::namespaceSDict;   

   delete Doxygen::symbolStorage;
}

// **
struct CommandLine parseCommandLine(QStringList argList)
{   
   CommandLine cmdArgs;

   QMap<QString, Options> argMap;
   argMap.insert( "--b", OUTPUT_APP      );
   argMap.insert( "--d", DEBUG_DUMP      );   
   argMap.insert( "--h", HELP            );  
   argMap.insert( "--l", BLANK_LAYOUT    );
   argMap.insert( "--m", DEBUG_SYMBOLS   );
   argMap.insert( "--w", BLANK_STYLE     );
   argMap.insert( "--v", DOXY_VERSION    );

   argMap.insert( "--help",    HELP         );
   argMap.insert( "--version", DOXY_VERSION );

   QStringList dashList;

   for (auto item : argList)   {      
      if (item.startsWith("--")) {
         dashList.append(item);
      }
   }
   
   auto iter = argList.begin();
 
   for (auto item : dashList)   {    

      Options value = INVALID;
      
      if (argMap.contains(item)) {
         value = argMap.value(item);
         ++iter;
      }
                
      switch (value) {

         case INVALID:           
            err("Option %s is invalid\n", qPrintable(item));             
            exit(1);                  
       
         case BLANK_LAYOUT:
            cmdArgs.genLayout = true;
            cmdArgs.layoutName = getValue(iter, argList.end());
           
            if (cmdArgs.layoutName.isEmpty()) {
               cmdArgs.layoutName = "doxy_layout.xml";
            }

            cmdArgs.generateDoxy = false; 
            break;

         case BLANK_STYLE:
            cmdArgs.formatName = getValue(iter, argList.end());

            if (cmdArgs.formatName.isEmpty()) {
               err("Option \"-w\" is missing format specifier rtf, html or latex\n");               
               exit(1);
            }

            cmdArgs.formatName = cmdArgs.formatName.toLower();

            if (cmdArgs.formatName == "rtf-ext") {
               cmdArgs.rtfExt = getValue(iter, argList.end());

               if (cmdArgs.rtfExt.isEmpty()) {
                  cmdArgs.rtfExt = "doxy_ext.rtf";
               }

               QFile f;
               if (openOutputFile(cmdArgs.rtfExt, f)) {
                   RTFGenerator::writeExtensionsFile(f);
               }
               f.close();                

            } else if (cmdArgs.formatName == "rtf-style") {
               cmdArgs.rtfStyle = getValue(iter, argList.end());

               if (cmdArgs.rtfStyle.isEmpty()) {
                  cmdArgs.rtfStyle = "doxy_style.rtf";
               }

               QFile f;
               if (openOutputFile(cmdArgs.rtfStyle, f)) {
                  RTFGenerator::writeStyleSheetFile(f);
               }
               f.close();


            } else  if (cmdArgs.formatName == "html-head") {
               cmdArgs.htmlHead = getValue(iter, argList.end());

               if (cmdArgs.htmlHead.isEmpty()) {
                  cmdArgs.htmlHead = "doxy_header.html";
               }

               QFile f;
               if (openOutputFile(cmdArgs.htmlHead, f)) {
                  HtmlGenerator::writeHeaderFile(f);
               }
               f.close();


            } else  if (cmdArgs.formatName == "html-foot") {
               cmdArgs.htmlFoot = getValue(iter, argList.end());

               if (cmdArgs.htmlFoot.isEmpty()) {
                  cmdArgs.htmlFoot = "doxy_footer.html";
               }

               QFile f;
               if (openOutputFile(cmdArgs.htmlFoot, f)) {
                  HtmlGenerator::writeFooterFile(f);
               }
               f.close();


            } else  if (cmdArgs.formatName == "html-style") {
              cmdArgs.htmlStyle = getValue(iter, argList.end());

              if (cmdArgs.htmlStyle.isEmpty()) {
                 cmdArgs.htmlStyle = "doxy_style.css";
              }

               QFile f;
               if (openOutputFile(cmdArgs.htmlStyle, f)) {
                  HtmlGenerator::writeStyleSheetFile(f);
               }
               f.close();


            } else if (cmdArgs.formatName == "latex-head") {
              cmdArgs.latexHead = getValue(iter, argList.end());

               if (cmdArgs.latexHead.isEmpty()) {
                  cmdArgs.latexHead = "doxy_header.latex";
               }

               QFile f;
               if (openOutputFile(cmdArgs.latexHead, f)) {
                 LatexGenerator::writeHeaderFile(f);
               }
               f.close();


            } else if (cmdArgs.formatName == "latex-foot") {
              cmdArgs.latexFoot = getValue(iter, argList.end());
   
               if (cmdArgs.latexFoot.isEmpty()) {
                  cmdArgs.latexFoot = "doxy_footer.latex";
               }

               QFile f;
               if (openOutputFile(cmdArgs.latexFoot, f)) {
                  LatexGenerator::writeFooterFile(f);
               }
               f.close();
 

            } else if (cmdArgs.formatName == "latex-style") {
              cmdArgs.latexStyle = getValue(iter, argList.end());

               if (cmdArgs.latexStyle.isEmpty()) {
                  cmdArgs.latexStyle= "doxy_style.latex";
               }

              QFile f;
              if (openOutputFile(cmdArgs.latexStyle, f)) {
                 LatexGenerator::writeStyleSheetFile(f);
               }
               f.close();


            } else  {
               err("Option \"-w %s\" is invalid\n", qPrintable(cmdArgs.formatName));               
               exit(1);

            }

            cmdArgs.generateDoxy = false; 
            break;

         case DEBUG_DUMP:
            cmdArgs.debugLabel = getValue(iter, argList.end());

            if (cmdArgs.debugLabel.isEmpty() ) {
               err("Option \"-d\" is missing a debug specifier\n");                       
               exit(1);
            }
           
            if (! Debug::setFlag(cmdArgs.debugLabel)) {
               err("Option \"-d\" has an unknown debug specifier: \"%s\".\n", qPrintable(cmdArgs.debugLabel));
               exit(1);
            }

            break;

         case DEBUG_SYMBOLS:
            Doxy_Globals::g_dumpSymbolMap = true;
            break;
          
         case DOXY_VERSION:
            printf("\nDoxyPress Version: %s\n", versionString);   
            printf("email: doxypress@copperspice.com\n");         
            exit(0);
                  
         case OUTPUT_APP:
            setvbuf(stdout, NULL, _IONBF, 0);
            setvbuf(stderr, NULL, _IONBF, 0);
   
            Doxygen::outputToApp = true;
            break;

         case HELP:       
            usage();
            exit(0);                    
      }      
   }

   // is there anything left in the arguemnent list
   cmdArgs.configName = getValue(iter, argList.end());
 
   return cmdArgs;
}

void readConfiguration(struct CommandLine cmdArgs)
{      
   // Parse project file
   printf("\nDoxyPress Version: %s\n\n", versionString);   
      
   if (cmdArgs.genLayout) {
      writeDefaultLayoutFile(cmdArgs.layoutName);      
   }

   if (! cmdArgs.generateDoxy) {            
      exit(0);      
   }

   if (cmdArgs.configName.isEmpty()) {
   
      for (auto item : QDir::current().entryList()) {          

         if (item.compare("doxypress.json", Qt::CaseInsensitive) == 0) {
            cmdArgs.configName = item;
            break;
         } 
      }
     
      if (cmdArgs.configName.isEmpty()) {
      
         printf("Usage: doxypess [OPTIONS] [project file name]\n"); 
         printf("doxypress --help for more information\n\n");

         fprintf(stderr, "No project file was specified, default file 'doxypress.json' was not found\n");          
         exit(1);
      }        
   }  

   QFileInfo fi(cmdArgs.configName);

   if (! fi.exists()) {  
      err("Project file %s was not found\n", qPrintable(cmdArgs.configName));      
      exit(1);
   }
  
   // step 1 
   if (! Config::parseConfig(cmdArgs.configName) ) {
      fprintf(stderr, "\n\nIssue parsing the project file.\nPlease submit a bug report to " 
                  " the developers at doxypress@copperspice.com\n");
      exit(1);
   }
       
   // Perlmod wants to know the path to the config file
   QFileInfo configFileInfo(cmdArgs.configName);
   setPerlModDoxyfile(qPrintable(configFileInfo.absoluteFilePath()));  
}

QString Doxy_Setup::getValue(QStringList::iterator &iter, QStringList::iterator end)
{
   QString retval;

   if (iter == end) {
      // nothing
      
   } else {     

      if (! iter->startsWith("--")) {
         retval = *iter;
         ++iter;
      }
   }

   return retval;
}

bool Doxy_Setup::openOutputFile(const QString &outFile, QFile &f)
{
   bool fileOpened = false;
   bool writeToStdout = (outFile[0] == '-' && outFile[1] == '\0');

   if (writeToStdout) { 
      // write to stdout
      fileOpened = f.open(stdout, QIODevice::WriteOnly);

   } else { 
      // write to file
      QFileInfo fi(outFile);

      if (fi.exists()) { 
         // create a backup

         QDir dir = fi.dir();
         QFileInfo backup(fi.fileName() + ".bak");

         if (backup.exists()) { 
            // remove existing backup
            dir.remove(backup.fileName());
         }

         dir.rename(fi.fileName(), fi.fileName() + ".bak");
      }

      f.setFileName(outFile);

      fileOpened = f.open(QIODevice::WriteOnly | QIODevice::Text);
   }

   return fileOpened;
}

void Doxy_Setup::usage()
{
   printf("\n");
   printf("DoxyPress: Version %s\n", versionString);
   printf("email: doxypress@copperspice.com\n");  

   printf("\n"); 
   printf("Usage: doxypess [OPTIONS] [project file name]\n"); 
      
   printf("\n\n");
   printf("Generate default layout file to configure documentation:\n");
   printf("   --l  [layout file name]   Default is `doxy_layout.xml'\n");
     
   printf("\n");
   printf("Generate a style sheet file for RTF, HTML or Latex:\n");
   printf("   RTF:    --w  rtf-ext     [extensions file name]   Default is 'doxy_ext.rtf'\n");
   printf("   RTF:    --w  rtf-style   [stylesheet file name]   Default is 'doxy_style.rtf'\n");

   printf("\n");
   printf("   HTML:   --w  html-head   [header file name]       Default is 'doxy_header.html'\n");
   printf("   HTML:   --w  html-foot   [footer file name]       Default is 'doxy_footer.html'\n");
   printf("   HTML:   --w  html-style  [styleSheet file name]   Default is 'doxy_style.css'\n");
  
   printf("\n");
   printf("   LaTeX:  --w  latex-head  [header file name]       Default is 'doxy_header.latex'\n");
   printf("   LaTeX:  --w  latex-foot  [footer file name]       Default is 'doxy_footer.latex'\n");
   printf("   LaTeX:  --w  latex-style [styleSheet file name]   Default is 'doxy_style.latex'\n");
   
   printf("\n");
   printf("Other Options:\n");
   printf("   --h  display usage\n");
   printf("   --v  display version\n");
    
   printf("\n");
   printf("   --b  output for DoxyPressApp\n"); 
   printf("   --m  dump symbol map\n");  
   printf("   --d  <level> enable one or more debug levels\n");

   Debug::printFlags();  
}
