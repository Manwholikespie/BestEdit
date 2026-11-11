/*
** 2026 November 11
**
** A test that runs a collection of RTF files through the editor.
**
** This file is compiled only into the test build of the application.
** If the environment variable BESTEDIT_RTFTEST names a file holding a
** list of RTF files, one per line, then at startup each file on the
** list is put through everything that happens to a document when it is
** opened in BestEdit, edited, and saved:
**
**    1.  The file is read by a new document.
**    2.  The document builds its window and displays the text.  (The
**        window is never put on screen.)
**    3.  The document is asked for the bytes it would save.
**    4.  Those bytes are read by a second document.
**    5.  The second document is asked for the bytes it would save.
**
** The files themselves are only ever read.  Nothing is written back.
**
** One line is written to standard output for each file, as
** tab-separated fields:
**
**     status     "ok", or the step that failed
**     nUnit      Length of the text in UTF-16 code units
**     nWord      Word count
**     nChar      Character count
**     nAttach    Number of attachments (pictures and the like)
**     shown      1 if the text displayed matches the text read
**     text       1 if the text survives saving and reading back
**     fixed      Attributes that changed when the text was displayed
**     lost       Attributes that changed when saved and read back
**     doc        Document attributes that changed likewise
**     stable     1 if saving a second time gives the same bytes
**     path       Name of the file
**
** Attributes are compared by meaning rather than bit for bit: colors
** are compared after conversion to a common color space, and lists
** and tables by their properties.  A change to an attribute is
** reported under the name of the attribute.  The name ends in "~" if
** the change is one that cannot be seen, which comes about in these
** ways:
**
**    *  Text was aligned "naturally", meaning left for scripts written
**       left to right, and is now aligned left, and the paragraph
**       holds no right-to-left text.  Likewise for tab stops.
**    *  An underline had no color of its own and now has a color that
**       is the same as the color of the text, or the text is a link.
**       Links are always drawn underlined in the link color.
**    *  Text that is not underlined acquired an underline color.
**    *  An outline color was dropped from text that is not outlined.
**    *  A setting that only matters when a line is truncated changed.
**       The editor wraps lines and never truncates them.
**
** If BESTEDIT_RTFDUMP names a directory, the text of the N-th file as
** displayed is also written there as N.txt, for comparison against
** other RTF readers.
**
** If BESTEDIT_RTFVERBOSE is set, each line of output is followed by
** lines that show the first place where the text changed and, for each
** attribute that changed, the first old and new values.  If both are
** set, the bytes that would be saved for the N-th file are written to
** the dump directory as N.rtf.
*/
#include "cocoa.h"
#include "bestedit.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int bVerbose = 0;          /* True to explain each difference */

/*
** A small set of strings.
*/
typedef struct StrSet StrSet;
struct StrSet {
  int n;                          /* Number of strings in the set */
  char az[24][64];                /* The strings */
};

/*
** Return the description of object p as a C string, with newlines
** turned into spaces.
*/
static const char *describe(id p){
  id z;
  if( p==0 ) return "(none)";
  z = msg0(id, p, "description");
  z = msg2(id, z, "stringByReplacingOccurrencesOfString:withString:",
           id, S("\n"), id, S(" "));
  return msg0(const char*, z, "UTF8String");
}

/*
** Add the name of object pKey to set p.  Return true if it was not
** already there.
*/
static int setAdd(StrSet *p, id pKey){
  const char *z = describe(pKey);
  int i;
  for(i=0; i<p->n; i++){
    if( strcmp(p->az[i], z)==0 ) return 0;
  }
  if( p->n<ArraySize(p->az) ){
    snprintf(p->az[p->n++], sizeof(p->az[0]), "%s", z);
  }
  return 1;
}

/*
** Print the content of set p, or "-" if it is empty.
*/
static void setPrint(StrSet *p){
  int i;
  if( p->n==0 ) printf("-");
  for(i=0; i<p->n; i++) printf("%s%s", i ? "," : "", p->az[i]);
}

/*
** Return true if pA and pB are colors that look the same.  The same
** color can be recorded in more than one color space, so the colors
** are compared after converting both to sRGB.
*/
static int sameColor(id pA, id pB){
  id pSpace = msg0(id, cls("NSColorSpace"), "sRGBColorSpace");
  double a[4], b[4];
  int i;
  if( !msg1(BOOL, pA, "isKindOfClass:", id, cls("NSColor")) ) return 0;
  if( !msg1(BOOL, pB, "isKindOfClass:", id, cls("NSColor")) ) return 0;
  pA = msg1(id, pA, "colorUsingColorSpace:", id, pSpace);
  pB = msg1(id, pB, "colorUsingColorSpace:", id, pSpace);
  if( pA==0 || pB==0 ) return 0;
  msg4(void, pA, "getRed:green:blue:alpha:", double*, &a[0], double*, &a[1],
       double*, &a[2], double*, &a[3]);
  msg4(void, pB, "getRed:green:blue:alpha:", double*, &b[0], double*, &b[1],
       double*, &b[2], double*, &b[3]);
  for(i=0; i<4; i++){
    double d = a[i] - b[i];
    if( d>0.004 || d<-0.004 ) return 0;
  }
  return 1;
}

/*
** Return true if the paragraph of attributed string pText that
** contains index i holds any Hebrew or Arabic text.
*/
static int isRtl(id pText, unsigned long i){
  id zStr = msg0(id, pText, "string");
  NSRange r;
  unsigned long k;
  r.location = i;
  r.length = 0;
  r = msg1(NSRange, zStr, "paragraphRangeForRange:", NSRange, r);
  for(k=r.location; k<r.location+r.length; k++){
    unsigned int c = msg1(u16, zStr, "characterAtIndex:", unsigned long, k);
    if( (c>=0x0590 && c<=0x08ff) || (c>=0xfb1d && c<=0xfdff)
     || (c>=0xfe70 && c<=0xfeff) ){
      return 1;
    }
  }
  return 0;
}

/*
** Return true if arrays aA and aB of NSTextList or NSTextBlock objects
** describe the same lists or the same table cells.
*/
static int sameBlocks(id aA, id aB){
  unsigned long n = msg0(unsigned long, aA, "count");
  unsigned long i;
  if( n!=msg0(unsigned long, aB, "count") ) return 0;
  for(i=0; i<n; i++){
    id x = msg1(id, aA, "objectAtIndex:", unsigned long, i);
    id y = msg1(id, aB, "objectAtIndex:", unsigned long, i);
    if( msg0(id, x, "class")!=msg0(id, y, "class") ) return 0;
    if( msg1(BOOL, x, "isKindOfClass:", id, cls("NSTextList")) ){
      if( !msg1(BOOL, msg0(id, x, "markerFormat"), "isEqual:",
                id, msg0(id, y, "markerFormat")) ) return 0;
      if( msg0(long, x, "startingItemNumber")
            !=msg0(long, y, "startingItemNumber") ) return 0;
    }else if( msg1(BOOL, x, "isKindOfClass:", id, cls("NSTextTableBlock")) ){
      static const char *azProp[] = {
        "startingRow", "rowSpan", "startingColumn", "columnSpan"
      };
      int k;
      for(k=0; k<ArraySize(azProp); k++){
        if( msg0(long, x, azProp[k])!=msg0(long, y, azProp[k]) ) return 0;
      }
      if( msg0(unsigned long, msg0(id, x, "table"), "numberOfColumns")
            !=msg0(unsigned long, msg0(id, y, "table"), "numberOfColumns") ){
        return 0;
      }
    }
  }
  return 1;
}

/*
** Return true if arrays aA and aB of tab stops are the same except
** that natural alignment (4) has become left alignment (0).
*/
static int sameTabsButNatural(id aA, id aB){
  unsigned long n = msg0(unsigned long, aA, "count");
  unsigned long i;
  if( n!=msg0(unsigned long, aB, "count") ) return 0;
  for(i=0; i<n; i++){
    id x = msg1(id, aA, "objectAtIndex:", unsigned long, i);
    id y = msg1(id, aB, "objectAtIndex:", unsigned long, i);
    double d = msg0(double, x, "location") - msg0(double, y, "location");
    long eX = msg0(long, x, "alignment");
    long eY = msg0(long, y, "alignment");
    if( d>0.01 || d<-0.01 ) return 0;
    if( eX!=eY && !(eX==4 && eY==0) ) return 0;
  }
  return 1;
}

/*
** Record in pSet that attribute zKey changed.  bHidden is true if the
** change cannot be seen.  If the attribute was not already in the set
** and the output is verbose, print zDetail.
*/
static void noteDiff(StrSet *pSet, const char *zKey, int bHidden,
                     const char *zDetail){
  char z[80];
  snprintf(z, sizeof(z), "%s%s", zKey, bHidden ? "~" : "");
  if( setAdd(pSet, beStr(z)) && bVerbose ){
    printf("    %s: %s\n", z, zDetail);
  }
}

/*
** pA and pB are paragraph styles that are not equal.  Add to pSet the
** name of each property on which they differ.  bRtl is true if the
** paragraph holds right-to-left text.
*/
static void paraDiff(id pA, id pB, int bRtl, StrSet *pSet){
  static const struct {
    const char *zName;            /* Name of the property */
    char eType;                   /* d: double, f: float, l: long, @: id */
  } a[] = {
    { "alignment",                           'l' },
    { "lineSpacing",                         'd' },
    { "paragraphSpacing",                    'd' },
    { "paragraphSpacingBefore",              'd' },
    { "headIndent",                          'd' },
    { "tailIndent",                          'd' },
    { "firstLineHeadIndent",                 'd' },
    { "minimumLineHeight",                   'd' },
    { "maximumLineHeight",                   'd' },
    { "lineHeightMultiple",                  'd' },
    { "lineBreakMode",                       'l' },
    { "defaultTabInterval",                  'd' },
    { "baseWritingDirection",                'l' },
    { "hyphenationFactor",                   'f' },
    { "tighteningFactorForTruncation",       'f' },
    { "allowsDefaultTighteningForTruncation",'b' },
    { "headerLevel",                         'l' },
    { "tabStops",                            '@' },
    { "textBlocks",                          '@' },
    { "textLists",                           '@' },
  };
  int i;
  int nDiff = 0;
  for(i=0; i<ArraySize(a); i++){
    int bDiff = 0;
    int bHidden = 0;
    char zKey[80];
    char zVal[200];
    zVal[0] = 0;
    switch( a[i].eType ){
      case 'd': {
        double x = msg0(double, pA, a[i].zName);
        double y = msg0(double, pB, a[i].zName);
        bDiff = x-y>0.01 || y-x>0.01;
        snprintf(zVal, sizeof(zVal), "%g -> %g", x, y);
        break;
      }
      case 'f': {
        float x = msg0(float, pA, a[i].zName);
        float y = msg0(float, pB, a[i].zName);
        bDiff = x-y>0.001 || y-x>0.001;
        bHidden = 1;
        snprintf(zVal, sizeof(zVal), "%g -> %g", x, y);
        break;
      }
      case 'b': {
        int x = msg0(BOOL, pA, a[i].zName);
        int y = msg0(BOOL, pB, a[i].zName);
        bDiff = x!=y;
        bHidden = 1;
        snprintf(zVal, sizeof(zVal), "%d -> %d", x, y);
        break;
      }
      case 'l': {
        long x = msg0(long, pA, a[i].zName);
        long y = msg0(long, pB, a[i].zName);
        bDiff = x!=y;
        if( i==0 ) bHidden = (x==4 && y==0 && !bRtl);
        snprintf(zVal, sizeof(zVal), "%ld -> %ld", x, y);
        break;
      }
      default: {
        id x = msg0(id, pA, a[i].zName);
        id y = msg0(id, pB, a[i].zName);
        bDiff = !(x==y || (x && y && msg1(BOOL, x, "isEqual:", id, y)))
             && !(msg0(unsigned long, x, "count")==0
                  && msg0(unsigned long, y, "count")==0);
        if( bDiff && strcmp(a[i].zName, "tabStops")==0 ){
          bHidden = !bRtl && sameTabsButNatural(x, y);
        }else if( bDiff && sameBlocks(x, y) ){
          bDiff = 0;
        }
        if( bDiff ){
          snprintf(zVal, sizeof(zVal), "%.90s -> ", describe(x));
          snprintf(zVal+strlen(zVal), sizeof(zVal)-strlen(zVal), "%.90s",
                   describe(y));
        }
        break;
      }
    }
    if( bDiff ){
      nDiff++;
      snprintf(zKey, sizeof(zKey), "para.%s", a[i].zName);
      noteDiff(pSet, zKey, bHidden, zVal);
    }
  }
  UNUSED_PARAMETER(nDiff);
}

/*
** Add to pSet the key of every entry that differs between dictionaries
** pA and pB.  Either may be nil.
**
** If pText is not NULL then pA and pB are the attributes of text at
** index i of pText before and after it was saved, and the differences
** are examined to see whether they are visible.  Otherwise pA and pB
** are document attributes and any difference counts.
*/
static void dictDiff(id pA, id pB, id pText, unsigned long i, StrSet *pSet){
  int k;
  for(k=0; k<2; k++){
    id pX = k ? pB : pA;
    id pY = k ? pA : pB;
    id aKey = msg0(id, pX, "allKeys");
    unsigned long j, n = msg0(unsigned long, aKey, "count");
    for(j=0; j<n; j++){
      id pKey = msg1(id, aKey, "objectAtIndex:", unsigned long, j);
      id pV = msg1(id, pX, "objectForKey:", id, pKey);
      id pW = msg1(id, pY, "objectForKey:", id, pKey);
      const char *zKey;
      char zDetail[700];
      int bHidden = 0;
      if( pW && msg1(BOOL, pV, "isEqual:", id, pW) ) continue;
      if( pW && k==1 ) continue;            /* Already seen when k==0 */
      if( pW && sameColor(pV, pW) ) continue;
      if( pW && pText
       && msg1(BOOL, pV, "isKindOfClass:", id, cls("NSParagraphStyle")) ){
        paraDiff(pV, pW, isRtl(pText, i), pSet);
        continue;
      }
      zKey = describe(pKey);
      if( pText && pW==0 ){
        if( strcmp(zKey, "NSUnderlineColor")==0 ){
          /* An underline with no color takes the color of the text */
          id pFg = msg1(id, pX, "objectForKey:", id,
                        NSForegroundColorAttributeName);
          if( pFg==0 ) pFg = msg0(id, cls("NSColor"), "blackColor");
          bHidden = sameColor(pV, pFg)
                 || msg1(id, pX, "objectForKey:", id, S("NSLink"))!=0
                 || msg0(long, msg1(id, pX, "objectForKey:", id,
                                    S("NSUnderline")), "integerValue")==0;
        }else if( strcmp(zKey, "NSParagraphStyle")==0 ){
          /* Text with no paragraph style is laid out in the default
          ** style, so compare against that. */
          id pDflt = msg0(id, cls("NSParagraphStyle"),
                          "defaultParagraphStyle");
          if( !msg1(BOOL, pV, "isEqual:", id, pDflt) ){
            paraDiff(k ? pDflt : pV, k ? pV : pDflt, isRtl(pText, i), pSet);
          }
          continue;
        }else if( strcmp(zKey, "NSStrokeColor")==0 ){
          bHidden = msg1(id, pX, "objectForKey:", id, S("NSStrokeWidth"))==0
                 && msg1(id, pY, "objectForKey:", id, S("NSStrokeWidth"))==0;
        }
      }
      snprintf(zDetail, sizeof(zDetail), "%.300s -> ",
               k ? "(none)" : describe(pV));
      snprintf(zDetail+strlen(zDetail), sizeof(zDetail)-strlen(zDetail),
               "%.300s", k ? describe(pV) : describe(pW));
      noteDiff(pSet, zKey, bHidden, zDetail);
      if( bVerbose && pText && !bHidden ){
        printf("      at %lu, where the attributes were: %.150s\n", i,
               describe(msg0(id, pA, "allKeys")));
        printf("      and are now: %.150s\n",
               describe(msg0(id, pB, "allKeys")));
      }
    }
  }
}

/*
** Add to pSet the name of every attribute that differs anywhere
** between attributed strings pA and pB.
*/
static void attrDiff(id pA, id pB, StrSet *pSet){
  unsigned long nA = msg0(unsigned long, pA, "length");
  unsigned long nB = msg0(unsigned long, pB, "length");
  unsigned long n = nA<nB ? nA : nB;
  unsigned long i = 0;
  while( i<n ){
    NSRange rA, rB;
    unsigned long iEndA, iEndB;
    id pDA = msg2(id, pA, "attributesAtIndex:effectiveRange:",
                  unsigned long, i, NSRange*, &rA);
    id pDB = msg2(id, pB, "attributesAtIndex:effectiveRange:",
                  unsigned long, i, NSRange*, &rB);
    if( !msg1(BOOL, pDA, "isEqualToDictionary:", id, pDB) ){
      dictDiff(pDA, pDB, pA, i, pSet);
    }
    iEndA = rA.location + rA.length;
    iEndB = rB.location + rB.length;
    i = iEndA<iEndB ? iEndA : iEndB;
  }
}

/*
** Show the first place at which strings zA and zB differ.
*/
static void textDiff(id zA, id zB){
  unsigned long nA = msg0(unsigned long, zA, "length");
  unsigned long nB = msg0(unsigned long, zB, "length");
  unsigned long i = 0;
  unsigned long k;
  while( i<nA && i<nB
      && msg1(u16, zA, "characterAtIndex:", unsigned long, i)
         ==msg1(u16, zB, "characterAtIndex:", unsigned long, i) ){
    i++;
  }
  printf("    text differs at %lu of %lu/%lu:\n      before:", i, nA, nB);
  for(k=(i>6 ? i-6 : 0); k<i+14 && k<nA; k++){
    printf(" %04x", msg1(u16, zA, "characterAtIndex:", unsigned long, k));
  }
  printf("\n      after: ");
  for(k=(i>6 ? i-6 : 0); k<i+14 && k<nB; k++){
    printf(" %04x", msg1(u16, zB, "characterAtIndex:", unsigned long, k));
  }
  printf("\n");
}

/*
** Return the number of attachment characters in string zStr.
*/
static long countAttachments(id zStr){
  unsigned long n = msg0(unsigned long, zStr, "length");
  unsigned long i;
  long nAttach = 0;
  u16 aBuf[1024];
  for(i=0; i<n; i+=ArraySize(aBuf)){
    unsigned long k, m = n-i;
    if( m>(unsigned long)ArraySize(aBuf) ) m = ArraySize(aBuf);
    CFStringGetCharacters((CFStringRef)zStr, CFRangeMake(i, m), aBuf);
    for(k=0; k<m; k++) if( aBuf[k]==0xfffc ) nAttach++;
  }
  return nAttach;
}

/*
** Create a new, empty rich text document.
*/
static id newDoc(void){
  id pDoc = beNew("BeDocument");
  msg1(void, pDoc, "setFileType:", id, S("public.rtf"));
  return pDoc;
}

/*
** Test one file.  iFile is its position on the list.
*/
static void testFile(const char *zPath, int iFile, const char *zDump){
  id pData;                       /* Content of the file */
  id pDoc1 = 0, pDoc2 = 0;        /* The two documents */
  id pRead = 0;                   /* Text as read from the file */
  id pShown = 0;                  /* Text as displayed */
  id pBack = 0;                   /* Text as saved and read back */
  id pSave1 = 0, pSave2 = 0;      /* Bytes of the first and second save */
  id pErr = 0;                    /* Error, if any */
  id zText;                       /* Plain text as displayed */
  const char *zStatus = "ok";     /* The outcome */
  StrSet sFixed, sLost, sDoc;     /* Things that changed */
  i64 nWord = 0, nChar = 0;       /* Word and character counts */
  int bShown = 0, bText = 0, bStable = 0;
  long nAttach = 0;
  unsigned long nUnit = 0;

  memset(&sFixed, 0, sizeof(sFixed));
  memset(&sLost, 0, sizeof(sLost));
  memset(&sDoc, 0, sizeof(sDoc));
  pData = msg1(id, cls("NSData"), "dataWithContentsOfFile:",
               id, beStr(zPath));
  if( pData==0 ){
    zStatus = "unreadable";
    goto done;
  }
  pDoc1 = newDoc();
  if( !msg3(BOOL, pDoc1, "readFromData:ofType:error:", id, pData,
            id, S("public.rtf"), id*, &pErr) ){
    zStatus = "read-failed";
    goto done;
  }
  pRead = msg0(id, msg0(id, beTestDocLoaded(pDoc1), "copy"), "autorelease");
  msg0(void, pDoc1, "makeWindowControllers");
  pShown = msg0(id, beTestDocText(pDoc1), "textStorage");
  zText = msg0(id, pShown, "string");
  nUnit = msg0(unsigned long, zText, "length");
  nWord = beTestDocWords(pDoc1, &nChar);
  nAttach = countAttachments(zText);
  bShown = msg1(BOOL, msg0(id, pRead, "string"), "isEqualToString:",
                id, zText);
  attrDiff(pRead, pShown, &sFixed);
  if( zDump ){
    char zFile[1200];
    snprintf(zFile, sizeof(zFile), "%s/%d.txt", zDump, iFile);
    msg4(BOOL, zText, "writeToFile:atomically:encoding:error:",
         id, beStr(zFile), BOOL, NO, unsigned long, NSUTF8StringEncoding,
         id*, &pErr);
  }

  pSave1 = msg2(id, pDoc1, "dataOfType:error:", id, S("public.rtf"),
                id*, &pErr);
  if( pSave1==0 ){
    zStatus = "save-failed";
    goto done;
  }
  if( zDump && bVerbose ){
    char zFile[1200];
    snprintf(zFile, sizeof(zFile), "%s/%d.rtf", zDump, iFile);
    msg2(BOOL, pSave1, "writeToFile:atomically:", id, beStr(zFile), BOOL, NO);
  }
  pDoc2 = newDoc();
  if( !msg3(BOOL, pDoc2, "readFromData:ofType:error:", id, pSave1,
            id, S("public.rtf"), id*, &pErr) ){
    zStatus = "reread-failed";
    goto done;
  }
  pBack = beTestDocLoaded(pDoc2);
  bText = msg1(BOOL, msg0(id, pBack, "string"), "isEqualToString:",
               id, zText);
  if( bVerbose ) printf("# %s\n", zPath);
  if( bVerbose && !bText ) textDiff(zText, msg0(id, pBack, "string"));
  attrDiff(pShown, pBack, &sLost);
  dictDiff(beTestDocAttr(pDoc1), beTestDocAttr(pDoc2), 0, 0, &sDoc);
  pSave2 = msg2(id, pDoc2, "dataOfType:error:", id, S("public.rtf"),
                id*, &pErr);
  bStable = pSave2 && msg1(BOOL, pSave1, "isEqualToData:", id, pSave2);
  if( bVerbose && pSave2 && !bStable ){
    const char *z1 = msg0(const char*, pSave1, "bytes");
    const char *z2 = msg0(const char*, pSave2, "bytes");
    unsigned long n1 = msg0(unsigned long, pSave1, "length");
    unsigned long n2 = msg0(unsigned long, pSave2, "length");
    unsigned long k = 0;
    while( k<n1 && k<n2 && z1[k]==z2[k] ) k++;
    printf("    second save differs at byte %lu of %lu/%lu:\n", k, n1, n2);
    printf("      first : %.*s\n", (int)(n1-k>90 ? 90 : n1-k), z1+k);
    printf("      second: %.*s\n", (int)(n2-k>90 ? 90 : n2-k), z2+k);
  }

done:
  printf("%s\t%lu\t%lld\t%lld\t%ld\t%d\t%d\t", zStatus, nUnit, nWord, nChar,
         nAttach, bShown, bText);
  setPrint(&sFixed);
  printf("\t");
  setPrint(&sLost);
  printf("\t");
  setPrint(&sDoc);
  printf("\t%d\t%s\n", bStable, zPath);
  if( pDoc1 ){
    msg0(void, pDoc1, "close");
    msg0(void, pDoc1, "release");
  }
  if( pDoc2 ){
    msg0(void, pDoc2, "close");
    msg0(void, pDoc2, "release");
  }
}

/*
** Run the test, if BESTEDIT_RTFTEST is set, and then exit.
*/
void beRtfTest(void){
  const char *zList = getenv("BESTEDIT_RTFTEST");
  const char *zDump = getenv("BESTEDIT_RTFDUMP");
  FILE *pIn;                      /* The list of files */
  char zLine[2048];               /* One line of the list */
  int iFile = 0;                  /* Number of files seen */

  if( zList==0 || zList[0]==0 ) return;
  if( zDump && zDump[0]==0 ) zDump = 0;
  bVerbose = getenv("BESTEDIT_RTFVERBOSE")!=0;
  pIn = fopen(zList, "r");
  if( pIn==0 ){
    fprintf(stderr, "cannot open %s\n", zList);
    exit(1);
  }
  while( fgets(zLine, sizeof(zLine), pIn) ){
    size_t n = strlen(zLine);
    id pPool;
    while( n>0 && (zLine[n-1]=='\n' || zLine[n-1]=='\r') ) zLine[--n] = 0;
    if( n==0 ) continue;
    pPool = beNew("NSAutoreleasePool");
    testFile(zLine, iFile++, zDump);
    msg0(void, pPool, "drain");
  }
  fclose(pIn);
  fflush(stdout);
  exit(0);
}
