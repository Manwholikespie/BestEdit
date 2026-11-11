/*
** 2026 November 11
**
** A test of the user interface of BestEdit.
**
** This file is compiled only into the test build of the application
** ("make uitest").  If the environment variable BESTEDIT_UITEST names a
** directory, then shortly after launch the application starts to drive
** itself: it types into the untitled document, checks the word count
** and the statistics, saves and reopens the document, opens the
** statistics window, and so forth.  A picture of each window is written
** into the directory as a PNG file, and one line is printed for each
** check.  The exit code is zero if every check passes.
**
** The steps are run from a timer, a fraction of a second apart, so
** that the event loop gets to run between one step and the next just
** as it would if a person were at the keyboard.
*/
#include "cocoa.h"
#include "bestedit.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char *zOutDir = 0;   /* Directory that receives the output */
static int iStep = 0;             /* The next step to run */
static int nErr = 0;              /* Number of checks that failed */
static id pDoc = 0;               /* The untitled document */
static i64 nBaseWord = 0;         /* Words this week before the test */

/*
** Record the result of one check.
*/
static void check(int bOk, const char *zWhat){
  printf("%s: %s\n", bOk ? "ok" : "FAILED", zWhat);
  if( !bOk ) nErr++;
  fflush(stdout);
}

/*
** Check that label pLabel shows the text zExpect.
*/
static void checkLabel(id pLabel, const char *zExpect, const char *zWhat){
  const char *z = msg0(const char*, msg0(id, pLabel, "stringValue"),
                       "UTF8String");
  if( strcmp(z, zExpect)!=0 ){
    printf("    got \"%s\", expected \"%s\"\n", z, zExpect);
  }
  check(strcmp(z, zExpect)==0, zWhat);
}

/*
** Return an NSURL for file zName in the output directory.
*/
static id outUrl(const char *zName){
  char zPath[1200];
  snprintf(zPath, sizeof(zPath), "%s/%s", zOutDir, zName);
  return msg1(id, cls("NSURL"), "fileURLWithPath:", id, beStr(zPath));
}

/*
** Write a picture of window pWin, title bar included, to file zName in
** the output directory.
*/
static void snapshot(id pWin, const char *zName){
  id pView = msg0(id, msg0(id, pWin, "contentView"), "superview");
  NSRect b = msgRect(pView, "bounds");
  id pRep = msg1(id, pView, "bitmapImageRepForCachingDisplayInRect:",
                 NSRect, b);
  id pData;
  msg2(void, pView, "cacheDisplayInRect:toBitmapImageRep:",
       NSRect, b, id, pRep);
  pData = msg2(id, pRep, "representationUsingType:properties:",
               unsigned long, NSBitmapImageFileTypePNG,
               id, msg0(id, cls("NSDictionary"), "dictionary"));
  check(msg2(BOOL, pData, "writeToURL:atomically:", id, outUrl(zName),
             BOOL, YES), zName);
}

/*
** Return the window of document pD.
*/
static id docWindow(id pD){
  id aCtrl = msg0(id, pD, "windowControllers");
  return msg0(id, msg0(id, aCtrl, "firstObject"), "window");
}

/*
** Type string z into document pD, replacing the range (iStart,nLen) or,
** if iStart is negative, the current selection.
*/
static void typeText(id pD, const char *z, long iStart, long nLen){
  NSRange r;
  if( iStart<0 ){
    r.location = (unsigned long)0x7fffffffffffffffLL;    /* NSNotFound */
    r.length = 0;
  }else{
    r.location = (unsigned long)iStart;
    r.length = (unsigned long)nLen;
  }
  msg2(void, beTestDocText(pD), "insertText:replacementRange:",
       id, beStr(z), NSRange, r);
}

/*
** Return the net number of words recorded for the past week.
*/
static i64 weekWords(void){
  StatsReport r;
  beStatsReport(BE_WEEK, &r);
  return r.nWord;
}

/*
** Open the file zName in the output directory and return the document.
*/
static id openFile(const char *zName){
  id pCtrl = msg0(id, cls("NSDocumentController"), "sharedDocumentController");
  id pErr = 0;
  return msg3(id, pCtrl, "openDocumentWithContentsOfURL:display:error:",
              id, outUrl(zName), BOOL, YES, id*, &pErr);
}

/*
** Read file zName from the test data directory into a new document,
** as if it had been opened, then save it, in memory, and read what was
** saved into a second new document.  Check that the text that comes
** back is zExpect, and that its second paragraph belongs to nList
** lists.
*/
static void checkRoundTrip(const char *zName, const char *zExpect, int nList){
  const char *zDir = getenv("BESTEDIT_TESTDATA");
  char zPath[1200];
  id pData, pDoc1, pDoc2, pSaved, pBack, pStyle;
  id pErr = 0;
  char zWhat[200];
  NSRange r;

  snprintf(zPath, sizeof(zPath), "%s/%s", zDir ? zDir : "test/data", zName);
  pData = msg1(id, cls("NSData"), "dataWithContentsOfFile:", id,
               beStr(zPath));
  snprintf(zWhat, sizeof(zWhat), "%s: read", zName);
  check(pData!=0, zWhat);
  if( pData==0 ) return;
  pDoc1 = beNew("BeDocument");
  pDoc2 = beNew("BeDocument");
  msg1(void, pDoc1, "setFileType:", id, S("public.rtf"));
  msg1(void, pDoc2, "setFileType:", id, S("public.rtf"));
  msg3(BOOL, pDoc1, "readFromData:ofType:error:", id, pData,
       id, S("public.rtf"), id*, &pErr);
  msg0(void, pDoc1, "makeWindowControllers");
  snprintf(zWhat, sizeof(zWhat), "%s: text as displayed", zName);
  check(msg1(BOOL, msg0(id, beTestDocText(pDoc1), "string"),
             "isEqualToString:", id, beStr(zExpect)), zWhat);
  pSaved = msg2(id, pDoc1, "dataOfType:error:", id, S("public.rtf"),
                id*, &pErr);
  msg3(BOOL, pDoc2, "readFromData:ofType:error:", id, pSaved,
       id, S("public.rtf"), id*, &pErr);
  pBack = beTestDocLoaded(pDoc2);
  snprintf(zWhat, sizeof(zWhat), "%s: text after saving", zName);
  check(pBack && msg1(BOOL, msg0(id, pBack, "string"), "isEqualToString:",
                      id, beStr(zExpect)), zWhat);
  r.location = strchr(zExpect, '\n') - zExpect + 1;
  r.length = 0;
  pStyle = msg3(id, pBack, "attribute:atIndex:effectiveRange:",
                id, NSParagraphStyleAttributeName,
                unsigned long, r.location, NSRange*, (NSRange*)0);
  snprintf(zWhat, sizeof(zWhat), "%s: lists after saving", zName);
  check((int)msg0(unsigned long, msg0(id, pStyle, "textLists"), "count")
          ==nList, zWhat);
  msg0(void, pDoc1, "close");
  msg0(void, pDoc1, "release");
  msg0(void, pDoc2, "release");
}

/*
** Run the next step of the test.
*/
static void testStep(CFRunLoopTimerRef pTimer, void *NotUsed){
  id pApp = msg0(id, cls("NSApplication"), "sharedApplication");
  id pPool = beNew("NSAutoreleasePool");
  NSRange r;
  UNUSED_PARAMETER(NotUsed);

  switch( iStep++ ){
    case 0: {
      /* An untitled rich text document opens at launch */
      id aDoc = msg0(id, msg0(id, cls("NSDocumentController"),
                              "sharedDocumentController"), "documents");
      pDoc = msg0(id, aDoc, "firstObject");
      check(pDoc!=0, "untitled document exists");
      if( pDoc==0 ) exit(1);
      nBaseWord = weekWords();
      checkLabel(beTestDocCount(pDoc),
                 "0 words  \xc2\xb7  0 characters", "empty count");
      check(!beTimerIsRunning(), "timer idle at launch");

      /* Typing updates the count, the statistics, and starts the timer */
      typeText(pDoc, "The quick brown fox jumps over the lazy dog. ", -1, 0);
      checkLabel(beTestDocCount(pDoc),
                 "9 words  \xc2\xb7  45 characters", "count after typing");
      check(weekWords()==nBaseWord+9, "nine words recorded");
      check(beTimerIsRunning(), "typing starts the timer");
      typeText(pDoc, "\nSecond paragraph, in which nothing much happens "
                     "\xe2\x80\x94 at least not yet.\n", -1, 0);
      checkLabel(beTestDocCount(pDoc),
                 "20 words  \xc2\xb7  112 characters", "count, two paragraphs");

      /* Make it rich: a bold word and a large first line */
      r.location = 4;
      r.length = 5;
      msg2(void, msg0(id, beTestDocText(pDoc), "textStorage"),
           "applyFontTraits:range:", unsigned long, NSBoldFontMask,
           NSRange, r);
      r.length = 11;
      msg1(void, beTestDocText(pDoc), "setSelectedRange:", NSRange, r);
      checkLabel(beTestDocCount(pDoc),
                 "2 of 20 words  \xc2\xb7  11 of 112 characters",
                 "count of selection");
      break;
    }
    case 1: {
      snapshot(docWindow(pDoc), "editor.png");
      break;
    }
    case 2: {
      id pErr = 0;
      i64 nWord, nChar;             /* Counts after the undo */
      char zW[32], zC[32];          /* The same, as text */
      char zExpect[100];            /* What the status bar should say */
      /* Deleting a word is recorded as a deletion */
      typeText(pDoc, "", 0, 4);
      r.location = 0;
      r.length = 0;
      msg1(void, beTestDocText(pDoc), "setSelectedRange:", NSRange, r);
      checkLabel(beTestDocCount(pDoc),
                 "19 words  \xc2\xb7  108 characters", "count after delete");

      /* Undo changes the text without the text view announcing it.
      ** The count and the statistics must follow anyway.  How much is
      ** undone depends on how the typing was grouped, which depends on
      ** timing: either just the deletion, or all of the typing. */
      msg0(void, msg0(id, pDoc, "undoManager"), "undo");
      nWord = beTestDocWords(pDoc, &nChar);
      check(nWord==20 || nWord==0, "undo changes the text");
      snprintf(zExpect, sizeof(zExpect), "%s words  \xc2\xb7  %s characters",
               beFmtInt(nWord, zW), beFmtInt(nChar, zC));
      checkLabel(beTestDocCount(pDoc), zExpect, "count after undo");
      check(weekWords()==nBaseWord+nWord, "net words after undo");

      /* Redo puts things back as they were before the undo */
      msg0(void, msg0(id, pDoc, "undoManager"), "redo");
      checkLabel(beTestDocCount(pDoc),
                 "19 words  \xc2\xb7  108 characters", "count after redo");
      check(weekWords()==nBaseWord+19, "net words after redo");

      /* Save as RTF, and write a plain text file to open */
      check(msg3(BOOL, pDoc, "writeToURL:ofType:error:", id,
                 outUrl("saved.rtf"), id, S("public.rtf"), id*, &pErr),
            "write RTF");
      check(msg4(BOOL, S("plain text\nna\xc3\xafve caf\xc3\xa9\n"),
                 "writeToURL:atomically:encoding:error:", id,
                 outUrl("plain.txt"), BOOL, YES, unsigned long,
                 NSUTF8StringEncoding, id*, &pErr),
            "write text file");
      break;
    }
    case 3: {
      /* Reopening the RTF gives back the same text, and opening a file
      ** does not count as writing */
      id pRtf = openFile("saved.rtf");
      id pTxt;
      id pFont;
      check(pRtf!=0, "open RTF");
      if( pRtf ){
        NSRange rEff;
        id pStore = msg0(id, beTestDocText(pRtf), "textStorage");
        checkLabel(beTestDocCount(pRtf),
                   "19 words  \xc2\xb7  108 characters", "count of RTF");
        if( msg0(unsigned long, pStore, "length")<10 ) exit(1);
        pFont = msg3(id, pStore, "attribute:atIndex:effectiveRange:",
                     id, NSFontAttributeName, unsigned long, 2,
                     NSRange*, &rEff);
        check(strstr(msg0(const char*, msg0(id, pFont, "fontName"),
                          "UTF8String"), "Bold")!=0
              && rEff.location==0 && rEff.length==5,
              "bold survives the round trip");
        check(msg0(BOOL, beTestDocText(pRtf), "isRichText"), "RTF is rich");
      }
      pTxt = openFile("plain.txt");
      check(pTxt!=0, "open text file");
      if( pTxt ){
        checkLabel(beTestDocCount(pTxt),
                   "4 words  \xc2\xb7  20 characters", "count of text file");
        check(!msg0(BOOL, beTestDocText(pTxt), "isRichText"),
              "text file is plain");
        typeText(pTxt, "more ", 0, 0);
        check(weekWords()==nBaseWord+20, "typing in text file is recorded");
      }
      check(weekWords()==nBaseWord+20, "opening files is not recorded");
      break;
    }
    case 4: {
      id aDoc = msg0(id, msg0(id, cls("NSDocumentController"),
                              "sharedDocumentController"), "documents");
      id pTxt = msg0(id, aDoc, "lastObject");
      id pErr = 0;
      id zBack;
      snapshot(docWindow(pTxt), "plain.png");
      check(msg3(BOOL, pTxt, "writeToURL:ofType:error:", id,
                 outUrl("plain2.txt"), id, S("public.plain-text"),
                 id*, &pErr), "write text");
      zBack = msg3(id, cls("NSString"),
                   "stringWithContentsOfURL:encoding:error:",
                   id, outUrl("plain2.txt"), unsigned long,
                   NSUTF8StringEncoding, id*, &pErr);
      check(zBack && msg1(BOOL, zBack, "isEqualToString:", id,
                          S("more plain text\nna\xc3\xafve caf\xc3\xa9\n")),
            "text file round trip");
      break;
    }
    case 5: {
      /* A list written by Microsoft Word sets off its markers in a
      ** way that Cocoa garbles when it saves.  Such paragraphs are
      ** taken out of their list, and keep their text.  A list in the
      ** form that Cocoa expects is left alone. */
      checkRoundTrip("wordlist.rtf",
                     "Steps:\n1.\tFirst\n2.\tSecond\nDone.\n", 0);
      checkRoundTrip("cocoalist.rtf",
                     "Shopping:\n\t\xe2\x80\xa2\tApples\n"
                     "\t\xe2\x80\xa2\tPears\n", 1);
      beStatsWinShow();
      beStatsWinSetPeriod(BE_WEEK);
      break;
    }
    case 6: {
      snapshot(beTestStatsWindow(), "stats-week.png");
      beStatsWinSetPeriod(BE_MONTH);
      break;
    }
    case 7: {
      snapshot(beTestStatsWindow(), "stats-month.png");
      beStatsWinSetPeriod(BE_YEAR);
      break;
    }
    case 8: {
      StatsReport rep;
      snapshot(beTestStatsWindow(), "stats-year.png");

      /* By now the timer has run for a few seconds.  Pausing it writes
      ** the time to the database. */
      check(beTimerIsRunning(), "timer still running");
      check(beTimerSession()>=2, "session time accumulates");
      beTimerToggle();
      check(!beTimerIsRunning(), "timer pauses");
      beStatsReport(BE_WEEK, &rep);
      check(rep.a[6].nSec>=2 && rep.a[6].nSec<=30, "time recorded");
      beTimerReset();
      check(beTimerSession()==0, "timer resets");
      break;
    }
    default: {
      /* Close everything without saving and quit */
      id aDoc = msg0(id, msg0(id, msg0(id, cls("NSDocumentController"),
                                       "sharedDocumentController"),
                              "documents"), "copy");
      unsigned long i, n = msg0(unsigned long, aDoc, "count");
      for(i=0; i<n; i++){
        msg0(void, msg1(id, aDoc, "objectAtIndex:", unsigned long, i),
             "close");
      }
      msg0(void, aDoc, "release");
      CFRunLoopTimerInvalidate(pTimer);
      printf("%d errors\n", nErr);
      fflush(stdout);
      beTimerShutdown();
      beStatsClose();
      exit(nErr!=0);
    }
  }
  UNUSED_PARAMETER(pApp);
  msg0(void, pPool, "drain");
}

/*
** Start the test, if BESTEDIT_UITEST is set.
*/
void beUiTestStart(void){
  CFRunLoopTimerRef pTimer;
  zOutDir = getenv("BESTEDIT_UITEST");
  if( zOutDir==0 || zOutDir[0]==0 ) return;
  pTimer = CFRunLoopTimerCreate(0, CFAbsoluteTimeGetCurrent()+1.5, 0.7, 0, 0,
                                testStep, 0);
  CFRunLoopAddTimer(CFRunLoopGetMain(), pTimer, kCFRunLoopCommonModes);
}
