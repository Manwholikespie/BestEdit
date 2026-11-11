/*
** 2026 November 11
**
** This file implements the document class and the editor window.
**
** BestEdit is built on the Cocoa document architecture.  A subclass of
** NSDocument named BeDocument is created here at run time.  Cocoa makes
** one instance for each open document and takes care of the open and
** save panels, autosaving, versions, the recent documents list, the
** "edited" marker in the title bar, and restoring windows at launch.
** All that is left for BeDocument to do is:
**
**    *  Convert the text to and from the bytes of a file.
**    *  Build the window that displays the text.
**    *  Keep the word count in that window up to date, and report
**       changes in it to the writing statistics.
**
** Two kinds of documents are understood.  Rich text is stored as RTF
** and is the default.  Plain text is stored as UTF-8 and is edited in a
** window that has no formatting controls.
**
** The state of each document is held in an Editor structure.  The only
** instance variable of BeDocument is a pointer to that structure.
*/
#include "cocoa.h"
#include "bestedit.h"
#include <stdio.h>
#include <stdlib.h>

/*
** The initial size of the text area of a new window, and the height of
** the status bar that runs along the bottom of each window.
*/
#define DOC_WIDTH       700.0
#define DOC_HEIGHT      780.0
#define DOC_BAR_HEIGHT  24.0

/*
** UTF-8 for the dot that separates items in the status bar.
*/
#define MIDDOT "\xc2\xb7"

/*
** Each open document is represented by an instance of the following
** structure.
*/
typedef struct Editor Editor;
struct Editor {
  id pText;               /* The NSTextView.  NULL until the window exists */
  id pCount;              /* Label showing the word count */
  id pClock;              /* Label showing the session timer */
  id pButton;             /* Button that starts and pauses the timer */
  id pLoaded;             /* Text read from a file but not yet displayed */
  id pDocAttr;            /* Document-wide attributes read from the file */
  int bRich;              /* True for rich text.  False for plain text */
  int bLoading;           /* True while text from a file is being installed */
  i64 nWord;              /* Number of words in the document */
  i64 nChar;              /* Number of characters in the document */
  Editor *pNext;          /* Next in the list of all editors */
};

/*
** Module state: the instance variable of BeDocument that holds the
** Editor pointer, a list of all editors, and the images for the timer
** button.
*/
static Ivar docIvar;
static Editor *pEditorList = 0;
static id pPlayImage = 0;
static id pPauseImage = 0;

/*
** Access the Editor of a BeDocument.
*/
#define docEditor(pDoc) \
  (*(Editor**)((char*)(pDoc) + ivar_getOffset(docIvar)))

/*
** Return true if zType, a uniform type identifier, is rich text.
** Every other type that BestEdit can open is some kind of plain text.
*/
static int docTypeIsRich(id zType){
  return zType==0 || msg1(BOOL, zType, "isEqualToString:", id, S("public.rtf"));
}

/*
** If ppErr is not NULL, store in *ppErr a new error object having the
** given code in the Cocoa error domain.
*/
static void docSetError(id *ppErr, long iCode){
  if( ppErr ){
    *ppErr = msg3(id, cls("NSError"), "errorWithDomain:code:userInfo:",
                  id, NSCocoaErrorDomain, long, iCode, id, (id)0);
  }
}

/*
** Feed the part of string zStr that lies in range r to word counter pWc.
*/
static void edCount(id zStr, NSRange r, WordCounter *pWc){
  u16 aBuf[1024];                 /* A piece of the string */
  unsigned long i = r.location;   /* Index of the next piece */
  unsigned long iEnd = r.location + r.length;
  while( i<iEnd ){
    unsigned long n = iEnd - i;
    if( n>(unsigned long)ArraySize(aBuf) ) n = ArraySize(aBuf);
    CFStringGetCharacters((CFStringRef)zStr, CFRangeMake(i, n), aBuf);
    beWordFeed(pWc, aBuf, (int)n);
    i += n;
  }
}

/*
** Recompute the word and character counts of the whole document.
*/
static void edRecount(Editor *p){
  id zStr = msg0(id, p->pText, "string");
  NSRange r;                      /* The whole string */
  WordCounter wc;                 /* The counter */
  r.location = 0;
  r.length = msg0(unsigned long, zStr, "length");
  beWordInit(&wc);
  edCount(zStr, r, &wc);
  p->nWord = wc.nWord;
  p->nChar = wc.nChar;
}

/*
** Update the word count shown in the status bar.  If bSelection is true
** and some text is selected, the count for the selection is shown as
** well.  Ex:
**
**     1,234 words . 7,001 characters
**     12 of 1,234 words . 64 of 7,001 characters
**
** The selection is clipped to the text, because in the midst of an
** edit the selection can briefly extend past the end of the text.
*/
static void edShowCount(Editor *p, int bSelection){
  id zStr = msg0(id, p->pText, "string");
  unsigned long nStr = msg0(unsigned long, zStr, "length");
  id aSel = bSelection ? msg0(id, p->pText, "selectedRanges") : 0;
  unsigned long nSel = msg0(unsigned long, aSel, "count");
  unsigned long i;                /* Loop counter */
  i64 nSelWord = 0;               /* Words in the selection */
  i64 nSelChar = 0;               /* Characters in the selection */
  int bSel = 0;                   /* True if the selection is not empty */
  char zW[32], zC[32];            /* Totals rendered as text */
  char zSW[32], zSC[32];          /* Selection counts rendered as text */
  char zMsg[200];                 /* The complete message */

  for(i=0; i<nSel; i++){
    id pVal = msg1(id, aSel, "objectAtIndex:", unsigned long, i);
    NSRange r = msg0(NSRange, pVal, "rangeValue");
    WordCounter wc;
    if( r.location>=nStr ) continue;
    if( r.length>nStr-r.location ) r.length = nStr - r.location;
    if( r.length==0 ) continue;
    bSel = 1;
    beWordInit(&wc);
    edCount(zStr, r, &wc);
    nSelWord += wc.nWord;
    nSelChar += wc.nChar;
  }
  beFmtInt(p->nWord, zW);
  beFmtInt(p->nChar, zC);
  if( bSel ){
    snprintf(zMsg, sizeof(zMsg), "%s of %s %s  " MIDDOT "  %s of %s %s",
             beFmtInt(nSelWord, zSW), zW, p->nWord==1 ? "word" : "words",
             beFmtInt(nSelChar, zSC), zC,
             p->nChar==1 ? "character" : "characters");
  }else{
    snprintf(zMsg, sizeof(zMsg), "%s %s  " MIDDOT "  %s %s",
             zW, p->nWord==1 ? "word" : "words",
             zC, p->nChar==1 ? "character" : "characters");
  }
  beSetText(p->pCount, zMsg);
}

/*
** Return the font used for text that has no font of its own: all of
** the text of a plain text document, and whatever is typed first into
** an empty rich text document.
*/
static id edDefaultFont(Editor *p){
  id pFont;
  if( p->bRich ){
    pFont = msg2(id, cls("NSFont"), "fontWithName:size:",
                 id, S("Helvetica"), double, 12.0);
    if( pFont==0 ){
      pFont = msg1(id, cls("NSFont"), "userFontOfSize:", double, 12.0);
    }
  }else{
    pFont = msg1(id, cls("NSFont"), "userFixedPitchFontOfSize:", double, 0.0);
  }
  return pFont;
}

/*
** Display the text that was read from the file, if any, and bring the
** word count up to date.
**
** Changes made here are not edits by the user, so they are neither
** undoable nor counted in the writing statistics.
*/
static void edInstall(Editor *p){
  id pStore = msg0(id, p->pText, "textStorage");
  NSRange r;                      /* Where to put the insertion point */
  p->bLoading = 1;
  if( p->pLoaded ){
    msg1(void, pStore, "setAttributedString:", id, p->pLoaded);
    msg0(void, p->pLoaded, "release");
    p->pLoaded = 0;
  }
  if( !p->bRich || msg0(unsigned long, pStore, "length")==0 ){
    msg1(void, p->pText, "setFont:", id, edDefaultFont(p));
  }
  r.location = 0;
  r.length = 0;
  msg1(void, p->pText, "setSelectedRange:", NSRange, r);
  p->bLoading = 0;
  edRecount(p);
  edShowCount(p, 1);
}

/*
** Configure the text view of editor p for the kind of document that it
** holds.  The view must already be inside of its scroll view.
*/
static void edConfigure(Editor *p){
  id pT = p->pText;               /* The text view */
  id pSpell = cls("NSSpellChecker");
  BOOL bRich = p->bRich!=0;
  NSSize inset;                   /* Margin around the text */

  msg1(void, pT, "setRichText:", BOOL, bRich);
  msg1(void, pT, "setImportsGraphics:", BOOL, NO);
  msg1(void, pT, "setAllowsUndo:", BOOL, YES);
  msg1(void, pT, "setUsesFindBar:", BOOL, YES);
  msg1(void, pT, "setIncrementalSearchingEnabled:", BOOL, YES);
  msg1(void, pT, "setUsesFontPanel:", BOOL, bRich);
  msg1(void, pT, "setUsesRuler:", BOOL, bRich);
  msg1(void, pT, "setUsesInspectorBar:", BOOL, bRich);
  msg1(void, pT, "setRulerVisible:", BOOL, bRich);
  msg1(void, pT, "setUsesAdaptiveColorMappingForDarkAppearance:", BOOL, YES);
  msg1(void, pT, "setContinuousSpellCheckingEnabled:", BOOL, YES);
  msg1(void, pT, "setAutomaticSpellingCorrectionEnabled:", BOOL,
       msg0(BOOL, pSpell, "isAutomaticSpellingCorrectionEnabled"));
  msg1(void, pT, "setAutomaticTextReplacementEnabled:", BOOL,
       msg0(BOOL, pSpell, "isAutomaticTextReplacementEnabled"));
  msg1(void, pT, "setAutomaticQuoteSubstitutionEnabled:", BOOL,
       bRich && msg0(BOOL, pSpell, "isAutomaticQuoteSubstitutionEnabled"));
  msg1(void, pT, "setAutomaticDashSubstitutionEnabled:", BOOL,
       bRich && msg0(BOOL, pSpell, "isAutomaticDashSubstitutionEnabled"));
  inset.width = 8.0;
  inset.height = 10.0;
  msg1(void, pT, "setTextContainerInset:", NSSize, inset);
}

/*
** Create a small borderless button that shows only an image.
*/
static id edIconButton(NSRect r, id pImage, const char *zAction,
                       const char *zTip){
  id pBtn = msg3(id, cls("NSButton"), "buttonWithImage:target:action:",
                 id, pImage, id, beDelegate, SEL, sel_registerName(zAction));
  msg1(void, pBtn, "setBordered:", BOOL, NO);
  msg1(void, pBtn, "setImagePosition:", unsigned long, NSImageOnly);
  msg1(void, pBtn, "setContentTintColor:", id, beColor("secondaryLabelColor"));
  msg1(void, pBtn, "setToolTip:", id, beStr(zTip));
  msg1(void, pBtn, "setFrame:", NSRect, r);
  msg1(void, pBtn, "setAutoresizingMask:", unsigned long, NSViewMinXMargin);
  return pBtn;
}

/*
** Build the status bar that runs along the bottom of the window of
** editor p.  The bar is rWidth points wide.  From left to right it
** holds the word count, the session timer, the button that starts and
** pauses the timer, and a button that opens the statistics window.
*/
static id edBuildStatusBar(Editor *p, double rWidth){
  id pBar;                        /* The status bar */
  id pLine;                       /* Hairline along the top of the bar */
  id pStats;                      /* The statistics button */
  double x = rWidth - 8.0;        /* Right edge of the next item */

  pBar = msg0(id, cls("NSView"), "alloc");
  pBar = msg1(id, pBar, "initWithFrame:", NSRect,
              CGRectMake(0.0, 0.0, rWidth, DOC_BAR_HEIGHT));
  msg1(void, pBar, "setAutoresizingMask:", unsigned long,
       NSViewWidthSizable|NSViewMaxYMargin);

  pLine = msg0(id, cls("NSBox"), "alloc");
  pLine = msg1(id, pLine, "initWithFrame:", NSRect,
               CGRectMake(0.0, DOC_BAR_HEIGHT-1.0, rWidth, 1.0));
  msg1(void, pLine, "setBoxType:", unsigned long, NSBoxSeparator);
  msg1(void, pLine, "setAutoresizingMask:", unsigned long,
       NSViewWidthSizable);
  msg1(void, pBar, "addSubview:", id, pLine);
  msg0(void, pLine, "release");

  x -= 22.0;
  pStats = edIconButton(CGRectMake(x, 2.0, 22.0, 20.0),
                        beSymbol("chart.bar.xaxis", "Statistics"),
                        "showStats:", "Show writing statistics");
  msg1(void, pBar, "addSubview:", id, pStats);

  x -= 24.0;
  p->pButton = edIconButton(CGRectMake(x, 2.0, 22.0, 20.0), pPlayImage,
                            "toggleTimer:", "Start the timer");
  msg0(id, p->pButton, "retain");
  msg1(void, pBar, "addSubview:", id, p->pButton);

  x -= 66.0;
  p->pClock = beLabel(CGRectMake(x, 4.0, 64.0, 16.0), 11.0,
                      NSFontWeightRegular, "secondaryLabelColor", 1);
  msg0(id, p->pClock, "retain");
  msg1(void, p->pClock, "setAlignment:", unsigned long, NSTextAlignmentRight);
  msg1(void, p->pClock, "setAutoresizingMask:", unsigned long,
       NSViewMinXMargin);
  msg1(void, p->pClock, "setToolTip:", id,
       S("Time spent writing this session"));
  msg1(void, pBar, "addSubview:", id, p->pClock);

  p->pCount = beLabel(CGRectMake(10.0, 4.0, x-14.0, 16.0), 11.0,
                      NSFontWeightRegular, "secondaryLabelColor", 1);
  msg0(id, p->pCount, "retain");
  msg1(void, p->pCount, "setAutoresizingMask:", unsigned long,
       NSViewWidthSizable);
  msg1(void, pBar, "addSubview:", id, p->pCount);

  return msg0(id, pBar, "autorelease");
}

/*
** Implementation of -[BeDocument init].
*/
static id docInit(id self, SEL cmd){
  struct objc_super sSuper;       /* For sending a message to super */
  Editor *p;                      /* The new editor */

  sSuper.receiver = self;
  sSuper.super_class = objc_getClass("NSDocument");
  self = ((id(*)(struct objc_super*,SEL))objc_msgSendSuper)(&sSuper, cmd);
  if( self==0 ) return 0;
  p = calloc(1, sizeof(*p));
  if( p==0 ){
    msg0(void, self, "release");
    return 0;
  }
  p->bRich = 1;
  p->pNext = pEditorList;
  pEditorList = p;
  docEditor(self) = p;
  return self;
}

/*
** Implementation of -[BeDocument dealloc].
*/
static void docDealloc(id self, SEL cmd){
  struct objc_super sSuper;       /* For sending a message to super */
  Editor *p = docEditor(self);    /* The editor to be destroyed */
  Editor **pp;                    /* For unlinking p from the list */

  if( p ){
    for(pp=&pEditorList; *pp; pp=&(*pp)->pNext){
      if( *pp==p ){
        *pp = p->pNext;
        break;
      }
    }
    if( p->pText ){
      msg1(void, msg0(id, p->pText, "textStorage"), "setDelegate:",
           id, (id)0);
      msg1(void, p->pText, "setDelegate:", id, (id)0);
    }
    msg0(void, p->pText, "release");
    msg0(void, p->pCount, "release");
    msg0(void, p->pClock, "release");
    msg0(void, p->pButton, "release");
    msg0(void, p->pLoaded, "release");
    msg0(void, p->pDocAttr, "release");
    free(p);
    docEditor(self) = 0;
  }
  sSuper.receiver = self;
  sSuper.super_class = objc_getClass("NSDocument");
  ((void(*)(struct objc_super*,SEL))objc_msgSendSuper)(&sSuper, cmd);
}

/*
** Implementation of +[BeDocument autosavesInPlace].  Returning YES asks
** Cocoa to save changes continuously as the user works, the way
** TextEdit does, rather than waiting to be told to save.
*/
static BOOL docAutosavesInPlace(id self, SEL NotUsed){
  UNUSED_PARAMETER(self);
  UNUSED_PARAMETER(NotUsed);
  return YES;
}

/*
** Decode the bytes of a plain text file.  UTF-8 is tried first.  If
** the bytes are not valid UTF-8, Cocoa is asked to guess the encoding.
** Return nil if the bytes cannot be decoded.
*/
static id docDecodeText(id pData){
  id zStr;                        /* The decoded text */
  zStr = msg0(id, cls("NSString"), "alloc");
  zStr = msg2(id, zStr, "initWithData:encoding:",
              id, pData, unsigned long, NSUTF8StringEncoding);
  if( zStr ) return msg0(id, zStr, "autorelease");
  msg4(unsigned long, cls("NSString"),
       "stringEncodingForData:encodingOptions:convertedString:"
       "usedLossyConversion:",
       id, pData, id, (id)0, id*, &zStr, BOOL*, (BOOL*)0);
  return zStr;
}

/*
** Remove list formatting from paragraphs of pText that Cocoa would
** corrupt when saving them as RTF.  Return the number of paragraphs
** that have this problem.  If bFix is false, just count them.  If bFix
** is true, pText must be mutable.
**
** In the Cocoa text system, a paragraph that belongs to a list begins
** with its marker (the bullet or the number) set off by tabs, as
** ordinary characters in the text.  When Cocoa writes RTF it puts the
** marker of each list paragraph into the file separately and omits the
** marker characters from the text.  The trouble comes when the text of
** a list paragraph does not begin with the marker that Cocoa expects.
** That is common: RTF written by Microsoft Word sets its markers off
** differently, and files in which a marker has been edited or deleted
** turn up even among those that Apple ships.  For such a paragraph
** Cocoa still writes the marker it expects, but omits nothing from the
** text, and so the paragraph gains a spurious marker each time the
** document is saved and reopened:
**
**       1. First            (tab) 1. (tab) 1. First
**       2. Second     =>    (tab) 2. (tab) 2. Second
**
** The cure is to take such paragraphs out of their lists.  They keep
** their text, marker included, and their indentation, so they look the
** same.  They merely stop being renumbered automatically.
*/
static int docFixLists(id pText, int bFix){
  id zStr = msg0(id, pText, "string");
  unsigned long n = msg0(unsigned long, zStr, "length");
  unsigned long i = 0;            /* Start of the current paragraph */
  int nBad = 0;                   /* Number of paragraphs affected */

  while( i<n ){
    NSRange r;                    /* The current paragraph */
    id pStyle;                    /* Its paragraph style */
    id aList;                     /* The lists it belongs to */

    r.location = i;
    r.length = 0;
    r = msg1(NSRange, zStr, "paragraphRangeForRange:", NSRange, r);
    pStyle = msg3(id, pText, "attribute:atIndex:effectiveRange:",
                  id, NSParagraphStyleAttributeName, unsigned long, i,
                  NSRange*, (NSRange*)0);
    aList = msg0(id, pStyle, "textLists");
    if( msg0(unsigned long, aList, "count")>0 ){
      id pList = msg0(id, aList, "lastObject");
      long iItem = msg2(long, pText, "itemNumberInTextList:atIndex:",
                        id, pList, unsigned long, i);
      id zMark = msg1(id, pList, "markerForItemNumber:", long, iItem);
      id zWant = msg1(id, S("\t"), "stringByAppendingString:", id, zMark);
      NSRange rHead;              /* Where the marker ought to be */
      zWant = msg1(id, zWant, "stringByAppendingString:", id, S("\t"));
      rHead.location = r.location;
      rHead.length = msg0(unsigned long, zWant, "length");
      if( rHead.length>r.length
       || !msg1(BOOL, msg1(id, zStr, "substringWithRange:", NSRange, rHead),
                "isEqualToString:", id, zWant)
      ){
        nBad++;
        if( bFix ){
          id pNew = msg0(id, pStyle, "mutableCopy");
          msg1(void, pNew, "setTextLists:", id,
               msg0(id, cls("NSArray"), "array"));
          msg3(void, pText, "addAttribute:value:range:",
               id, NSParagraphStyleAttributeName, id, pNew, NSRange, r);
          msg0(void, pNew, "release");
        }
      }
    }
    if( r.location+r.length<=i ) break;
    i = r.location + r.length;
  }
  return nBad;
}

/*
** Implementation of -[BeDocument readFromData:ofType:error:].
**
** Convert the content of a file into text.  This is called when a
** document is opened, in which case the window does not exist yet and
** the text is saved for later.  It is also called when a document that
** is already open reverts to an earlier version, in which case the new
** text is displayed immediately.
**
** An RTF file holds more than text.  It also records properties of the
** document as a whole, such as the paper size, the margins, and the
** title and author.  BestEdit has no way to edit these, but it holds
** on to them so that they can be written back out when the document is
** saved.
*/
static BOOL docRead(id self, SEL NotUsed, id pData, id zType, id *ppErr){
  Editor *p = docEditor(self);
  id pAttr = 0;                   /* The text of the document */
  id pDocAttr = 0;                /* Attributes of the document as a whole */

  UNUSED_PARAMETER(NotUsed);
  p->bRich = docTypeIsRich(zType);
  if( msg0(unsigned long, pData, "length")==0 ){
    pAttr = beNew("NSAttributedString");
  }else if( p->bRich ){
    id pOpt = beDict(NSDocumentTypeDocumentAttribute, NSRTFTextDocumentType,
                     0, 0);
    i64 nFixed = 0;               /* Size of the repaired RTF */
    char *zFixed;                 /* The RTF with raw UTF-8 escaped */
    zFixed = beRtfRepair(msg0(const char*, pData, "bytes"),
                         (i64)msg0(unsigned long, pData, "length"), &nFixed);
    if( zFixed ){
      pData = msg3(id, cls("NSData"),
                   "dataWithBytesNoCopy:length:freeWhenDone:",
                   void*, zFixed, unsigned long, (unsigned long)nFixed,
                   BOOL, YES);
    }
    pAttr = msg0(id, cls("NSMutableAttributedString"), "alloc");
    pAttr = msg4(id, pAttr,
                 "initWithData:options:documentAttributes:error:",
                 id, pData, id, pOpt, id*, &pDocAttr, id*, ppErr);
    if( pAttr ) docFixLists(pAttr, 1);
  }else{
    id zStr = docDecodeText(pData);
    if( zStr ){
      pAttr = msg0(id, cls("NSAttributedString"), "alloc");
      pAttr = msg1(id, pAttr, "initWithString:", id, zStr);
    }else{
      docSetError(ppErr, NSFileReadCorruptFileError);
    }
  }
  if( pAttr==0 ) return NO;
  msg0(void, p->pLoaded, "release");
  p->pLoaded = pAttr;
  msg0(id, pDocAttr, "retain");
  msg0(void, p->pDocAttr, "release");
  p->pDocAttr = pDocAttr;
  if( p->pText ) edInstall(p);
  return YES;
}

/*
** Implementation of -[BeDocument dataOfType:error:].
**
** Convert the text of the document into the content of a file.
*/
static id docData(id self, SEL NotUsed, id zType, id *ppErr){
  Editor *p = docEditor(self);
  id pStore;                      /* The text to be saved */
  id pData;                       /* The text as bytes */

  UNUSED_PARAMETER(NotUsed);
  if( p->pText ){
    msg0(void, p->pText, "breakUndoCoalescing");
    pStore = msg0(id, p->pText, "textStorage");
  }else{
    pStore = p->pLoaded;
  }
  if( pStore==0 ){
    return msg0(id, cls("NSData"), "data");
  }
  if( docTypeIsRich(zType) ){
    id pAttr;                     /* Attributes of the document as a whole */
    NSRange r;                    /* The whole text */
    pAttr = msg0(id, cls("NSMutableDictionary"), "dictionary");
    if( p->pDocAttr ){
      msg1(void, pAttr, "addEntriesFromDictionary:", id, p->pDocAttr);
    }
    msg2(void, pAttr, "setObject:forKey:", id, NSRTFTextDocumentType,
         id, NSDocumentTypeDocumentAttribute);
    if( docFixLists(pStore, 0) ){
      /* Editing has left a list paragraph in a state that would not
      ** survive being saved.  Save a corrected copy of the text. */
      pStore = msg0(id, cls("NSMutableAttributedString"), "alloc");
      pStore = msg1(id, pStore, "initWithAttributedString:", id,
                    p->pText ? msg0(id, p->pText, "textStorage") : p->pLoaded);
      msg0(id, pStore, "autorelease");
      docFixLists(pStore, 1);
    }
    r.location = 0;
    r.length = msg0(unsigned long, pStore, "length");
    pData = msg2(id, pStore, "RTFFromRange:documentAttributes:",
                 NSRange, r, id, pAttr);
  }else{
    pData = msg1(id, msg0(id, pStore, "string"), "dataUsingEncoding:",
                 unsigned long, NSUTF8StringEncoding);
  }
  if( pData==0 ) docSetError(ppErr, NSFileWriteUnknownError);
  return pData;
}

/*
** Implementation of -[BeDocument writableTypesForSaveOperation:].
**
** A document is always saved as the type it already has.  Otherwise the
** save panel would offer to save rich text as plain text, which would
** silently discard the formatting.
*/
static id docWritableTypes(id self, SEL NotUsed, unsigned long NotUsed2){
  id zType = msg0(id, self, "fileType");
  UNUSED_PARAMETER(NotUsed);
  UNUSED_PARAMETER(NotUsed2);
  if( zType==0 ) zType = S("public.rtf");
  return msg1(id, cls("NSArray"), "arrayWithObject:", id, zType);
}

/*
** Implementation of -[BeDocument makeWindowControllers].
**
** Build the window for a document.  The content of the window is a
** scrolling text view with a status bar underneath.
*/
static void docMakeWindow(id self, SEL NotUsed){
  Editor *p = docEditor(self);
  id pWin;                        /* The new window */
  id pCtrl;                       /* The window controller */
  id pRoot;                       /* The content view of the window */
  id pScroll;                     /* The scroll view */
  NSRect r;                       /* Assorted rectangles */

  UNUSED_PARAMETER(NotUsed);
  if( p->pText ) return;
  if( pPlayImage==0 ){
    pPlayImage = msg0(id, beSymbol("play.fill", "Start"), "retain");
    pPauseImage = msg0(id, beSymbol("pause.fill", "Pause"), "retain");
  }
  if( p->pLoaded==0 ){
    p->bRich = docTypeIsRich(msg0(id, self, "fileType"));
  }

  r = CGRectMake(0.0, 0.0, DOC_WIDTH, DOC_HEIGHT + DOC_BAR_HEIGHT);
  pWin = msg0(id, cls("NSWindow"), "alloc");
  pWin = msg4(id, pWin, "initWithContentRect:styleMask:backing:defer:",
              NSRect, r,
              unsigned long, NSWindowStyleMaskTitled
                             | NSWindowStyleMaskClosable
                             | NSWindowStyleMaskMiniaturizable
                             | NSWindowStyleMaskResizable,
              unsigned long, NSBackingStoreBuffered,
              BOOL, YES);
  msg1(void, pWin, "setReleasedWhenClosed:", BOOL, NO);
  msg1(void, pWin, "setContentMinSize:", NSSize, CGSizeMake(380.0, 220.0));
  msg0(void, pWin, "center");
  pRoot = msg0(id, pWin, "contentView");

  r = CGRectMake(0.0, DOC_BAR_HEIGHT, DOC_WIDTH, DOC_HEIGHT);
  pScroll = msg0(id, cls("NSTextView"), "scrollableTextView");
  msg1(void, pScroll, "setFrame:", NSRect, r);
  msg1(void, pScroll, "setAutoresizingMask:", unsigned long,
       NSViewWidthSizable|NSViewHeightSizable);
  msg1(void, pScroll, "setBorderType:", unsigned long, 0);
  msg1(void, pRoot, "addSubview:", id, pScroll);
  msg1(void, pRoot, "addSubview:", id, edBuildStatusBar(p, DOC_WIDTH));

  p->pText = msg0(id, msg0(id, pScroll, "documentView"), "retain");
  edConfigure(p);
  edInstall(p);
  msg1(void, p->pText, "setDelegate:", id, self);
  msg1(void, msg0(id, p->pText, "textStorage"), "setDelegate:", id, self);

  pCtrl = msg0(id, cls("NSWindowController"), "alloc");
  pCtrl = msg1(id, pCtrl, "initWithWindow:", id, pWin);
  msg1(void, pCtrl, "setWindowFrameAutosaveName:", id, S("BeDocument"));
  msg1(void, self, "addWindowController:", id, pCtrl);
  msg1(BOOL, pWin, "makeFirstResponder:", id, p->pText);
  msg0(void, pCtrl, "release");
  msg0(void, pWin, "release");
  beDocTimerChanged();
}

/*
** Implementation of
** -[BeDocument textStorage:didProcessEditing:range:changeInLength:].
**
** The text storage calls this after every change to the text, whatever
** the cause: typing, pasting, deleting, undo and redo, find and
** replace, or spelling correction.  Any change in the number of words
** is added to the writing statistics.
**
** This is used in preference to the textDidChange: notification of the
** text view, which is not sent for every kind of change.  Undo, for
** one, changes the text without sending it.
*/
static void docDidProcessEditing(
  id self,                        /* The BeDocument */
  SEL NotUsed,                    /* The selector */
  id pStore,                      /* The text storage that changed */
  unsigned long mEdited,          /* What changed: 1 attributes, 2 text */
  NSRange rEdited,                /* Where the change was made */
  long nDelta                     /* Change in the length of the text */
){
  Editor *p = docEditor(self);
  i64 nPrior = p->nWord;          /* Word count before the change */

  UNUSED_PARAMETER(NotUsed);
  UNUSED_PARAMETER(pStore);
  UNUSED_PARAMETER(rEdited);
  UNUSED_PARAMETER(nDelta);
  if( p->bLoading || p->pCount==0 || (mEdited & 2)==0 ) return;
  edRecount(p);
  if( p->nWord>nPrior ){
    beStatsAddWords(p->nWord - nPrior, 0);
  }else if( p->nWord<nPrior ){
    beStatsAddWords(0, nPrior - p->nWord);
  }
  edShowCount(p, 0);
  beTimerActivity();
}

/*
** Implementation of -[BeDocument textDidChange:] and of
** -[BeDocument textViewDidChangeSelection:].
**
** The word count itself is maintained by docDidProcessEditing().  All
** that needs doing here is to redisplay it, because the display also
** depends on what is selected.
*/
static void docSelectionDidChange(id self, SEL NotUsed, id pNote){
  Editor *p = docEditor(self);
  UNUSED_PARAMETER(NotUsed);
  UNUSED_PARAMETER(pNote);
  if( p->pCount ) edShowCount(p, 1);
}

/*
** Implementation of -[BeDocument printOperationWithSettings:error:].
**
** The document is printed from a separate text view that is as wide as
** the printable area of the page, so that the lines of the printout
** break to suit the paper rather than to suit the window.
*/
static id docPrint(id self, SEL NotUsed, id pSettings, id *ppErr){
  Editor *p = docEditor(self);
  id pInfo;                       /* Paper size, margins, and so forth */
  id pView;                       /* The text view that gets printed */
  id pOp;                         /* The print operation */
  NSSize sz;                      /* Size of the paper */
  NSRect r;                       /* Printable area of one page */

  UNUSED_PARAMETER(NotUsed);
  if( p->pText==0 ){
    docSetError(ppErr, NSFileWriteUnknownError);
    return 0;
  }
  pInfo = msg0(id, msg0(id, self, "printInfo"), "copy");
  if( pSettings ){
    msg1(void, msg0(id, pInfo, "dictionary"), "addEntriesFromDictionary:",
         id, pSettings);
  }
  msg1(void, pInfo, "setVerticallyCentered:", BOOL, NO);
  sz = msg0(NSSize, pInfo, "paperSize");
  r.origin.x = 0.0;
  r.origin.y = 0.0;
  r.size.width = sz.width - msg0(double, pInfo, "leftMargin")
                          - msg0(double, pInfo, "rightMargin");
  r.size.height = sz.height - msg0(double, pInfo, "topMargin")
                            - msg0(double, pInfo, "bottomMargin");

  pView = msg0(id, cls("NSTextView"), "alloc");
  pView = msg1(id, pView, "initWithFrame:", NSRect, r);
  msg1(void, pView, "setAppearance:", id,
       msg1(id, cls("NSAppearance"), "appearanceNamed:",
            id, NSAppearanceNameAqua));
  msg1(void, msg0(id, pView, "textStorage"), "setAttributedString:",
       id, msg0(id, p->pText, "textStorage"));
  pOp = msg2(id, cls("NSPrintOperation"), "printOperationWithView:printInfo:",
             id, pView, id, pInfo);
  msg0(void, pView, "release");
  msg0(void, pInfo, "release");
  return pOp;
}

/*
** Create the BeDocument class.  This must be done before the
** application starts to run, because Cocoa looks the class up by the
** name given in Info.plist as soon as a document is opened.
*/
void beDocRegister(void){
  Class pClass;                   /* The new class */
  pClass = objc_allocateClassPair(objc_getClass("NSDocument"),
                                  "BeDocument", 0);
  class_addIvar(pClass, "pEditor", sizeof(Editor*), 3, "^v");
  class_addMethod(pClass, sel_registerName("init"),
                  (IMP)docInit, "@@:");
  class_addMethod(pClass, sel_registerName("dealloc"),
                  (IMP)docDealloc, "v@:");
  class_addMethod(pClass, sel_registerName("readFromData:ofType:error:"),
                  (IMP)docRead, BE_BOOL "@:@@^@");
  class_addMethod(pClass, sel_registerName("dataOfType:error:"),
                  (IMP)docData, "@@:@^@");
  class_addMethod(pClass, sel_registerName("writableTypesForSaveOperation:"),
                  (IMP)docWritableTypes, "@@:Q");
  class_addMethod(pClass, sel_registerName("makeWindowControllers"),
                  (IMP)docMakeWindow, "v@:");
  class_addMethod(pClass,
      sel_registerName("textStorage:didProcessEditing:range:changeInLength:"),
      (IMP)docDidProcessEditing, "v@:@Q{_NSRange=QQ}q");
  class_addMethod(pClass, sel_registerName("textDidChange:"),
                  (IMP)docSelectionDidChange, "v@:@");
  class_addMethod(pClass, sel_registerName("textViewDidChangeSelection:"),
                  (IMP)docSelectionDidChange, "v@:@");
  class_addMethod(pClass, sel_registerName("printOperationWithSettings:error:"),
                  (IMP)docPrint, "@@:@^@");
  class_addMethod(object_getClass((id)pClass),
                  sel_registerName("autosavesInPlace"),
                  (IMP)docAutosavesInPlace, BE_BOOL "@:");
  objc_registerClassPair(pClass);
  docIvar = class_getInstanceVariable(pClass, "pEditor");
}

/*
** Bring the timer display in the status bar of every window up to date.
*/
void beDocTimerChanged(void){
  Editor *p;                      /* For looping over all editors */
  char zClock[32];                /* The session time as text */
  id zText;                       /* The same, as an NSString */
  int bRun = beTimerIsRunning();

  if( pEditorList==0 ) return;
  zText = beStr(beFmtClock(beTimerSession(), zClock));
  for(p=pEditorList; p; p=p->pNext){
    if( p->pClock==0 ) continue;
    msg1(void, p->pClock, "setStringValue:", id, zText);
    msg1(void, p->pClock, "setTextColor:", id,
         beColor(bRun ? "labelColor" : "secondaryLabelColor"));
    msg1(void, p->pButton, "setImage:", id, bRun ? pPauseImage : pPlayImage);
    msg1(void, p->pButton, "setToolTip:", id,
         bRun ? S("Pause the timer") : S("Start the timer"));
  }
}

/*
** Test interfaces: return the text view, the word count label, the
** text that is waiting to be displayed, the document attributes, and
** the word count of a document.
*/
#ifdef BE_TEST
void *beTestDocText(void *pDoc){
  return docEditor(pDoc)->pText;
}
void *beTestDocCount(void *pDoc){
  return docEditor(pDoc)->pCount;
}
void *beTestDocLoaded(void *pDoc){
  return docEditor(pDoc)->pLoaded;
}
void *beTestDocAttr(void *pDoc){
  return docEditor(pDoc)->pDocAttr;
}
i64 beTestDocWords(void *pDoc, i64 *pnChar){
  *pnChar = docEditor(pDoc)->nChar;
  return docEditor(pDoc)->nWord;
}
#endif
