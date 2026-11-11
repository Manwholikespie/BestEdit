/*
** 2026 November 11
**
** Tests for the portable parts of BestEdit: word counting, formatting,
** the repair of RTF, and the statistics database.
**
** Usage:
**
**     betest TEMPFILE
**
** TEMPFILE is the name of a scratch database, which is deleted first.
** The exit code is zero if and only if every test passes.
*/
#include "bestedit.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int nTest = 0;             /* Number of tests run */
static int nErr = 0;              /* Number of tests that failed */

/*
** Check that integer expression X has value Y.
*/
#define EXPECT_INT(X,Y) expectInt(__LINE__, #X, (i64)(X), (i64)(Y))
static void expectInt(int iLine, const char *zExpr, i64 x, i64 y){
  nTest++;
  if( x!=y ){
    nErr++;
    printf("line %d: %s is %lld, expected %lld\n", iLine, zExpr, x, y);
  }
}

/*
** Check that string expression X has value Y.
*/
#define EXPECT_STR(X,Y) expectStr(__LINE__, #X, (X), (Y))
static void expectStr(int iLine, const char *zExpr, const char *x,
                      const char *y){
  nTest++;
  if( strcmp(x, y)!=0 ){
    nErr++;
    printf("line %d: %s is \"%s\", expected \"%s\"\n", iLine, zExpr, x, y);
  }
}

/*
** Count the words in UTF-8 string z.  If pnChar is not NULL, write the
** character count there.  The text is fed to the counter nPiece code
** units at a time to exercise the handling of piece boundaries.
*/
static i64 countWords(const char *z, int nPiece, i64 *pnChar){
  const unsigned char *zIn = (const unsigned char*)z;
  u16 a[512];                     /* The text as UTF-16 */
  int n = 0;                      /* Number of code units in a[] */
  int i;                          /* Loop counter */
  WordCounter wc;                 /* The counter */

  while( zIn[0] && n<ArraySize(a)-2 ){
    unsigned int c = *zIn++;
    if( c>=0xf0 ){
      c = (c&0x07)<<18 | (zIn[0]&0x3f)<<12 | (zIn[1]&0x3f)<<6 | (zIn[2]&0x3f);
      zIn += 3;
    }else if( c>=0xe0 ){
      c = (c&0x0f)<<12 | (zIn[0]&0x3f)<<6 | (zIn[1]&0x3f);
      zIn += 2;
    }else if( c>=0xc0 ){
      c = (c&0x1f)<<6 | (zIn[0]&0x3f);
      zIn += 1;
    }
    if( c>=0x10000 ){
      c -= 0x10000;
      a[n++] = (u16)(0xd800 + (c>>10));
      a[n++] = (u16)(0xdc00 + (c&0x3ff));
    }else{
      a[n++] = (u16)c;
    }
  }
  beWordInit(&wc);
  for(i=0; i<n; i+=nPiece){
    beWordFeed(&wc, &a[i], n-i<nPiece ? n-i : nPiece);
  }
  if( pnChar ) *pnChar = wc.nChar;
  return wc.nWord;
}

/*
** Tests of word counting.
*/
static void testWordCount(void){
  static const struct {
    const char *z;                /* The text */
    int nWord;                    /* Expected number of words */
    int nChar;                    /* Expected number of characters */
  } a[] = {
    { "",                                   0,  0 },
    { "   \n\t ",                           0,  5 },
    { "hello",                              1,  5 },
    { "hello world",                        2, 11 },
    { "  hello   world  ",                  2, 17 },
    { "one\ntwo\r\nthree",                  3, 11 },
    { "don't stop",                         2, 10 },
    { "well-known fact",                    2, 15 },
    { "wait - what",                        2, 11 },
    { "yes--no",                            1,  7 },
    { "yes\xe2\x80\x94no",                  2,  6 },
    { "pi is 3.14",                         3, 10 },
    { "... !!! ???",                        0, 11 },
    { "\"Quoted,\" she said.",              3, 19 },
    { "\xe2\x80\x9c" "Curly" "\xe2\x80\x9d quotes", 2, 14 },
    { "caf\xc3\xa9 na\xc3\xafve",           2, 10 },
    { "a\xc2\xa0" "b",                      2,  3 },
    { "\xf0\x9f\x98\x80 ok",                2,  4 },
    { "x\xe2\x80\xa8y",                     2,  2 },
  };
  int i;                          /* Loop counter */
  int nPiece;                     /* Size of pieces fed to the counter */
  for(i=0; i<ArraySize(a); i++){
    for(nPiece=1; nPiece<=4; nPiece++){
      i64 nChar = 0;
      i64 nWord = countWords(a[i].z, nPiece, &nChar);
      nTest++;
      if( nWord!=a[i].nWord || nChar!=a[i].nChar ){
        nErr++;
        printf("wordcount case %d piece %d: got %lld words %lld chars,"
               " expected %d and %d\n", i, nPiece, nWord, nChar,
               a[i].nWord, a[i].nChar);
      }
    }
  }
}

/*
** Tests of the formatting utilities.
*/
static void testFormat(void){
  char z[32];
  EXPECT_STR( beFmtInt(0, z), "0" );
  EXPECT_STR( beFmtInt(999, z), "999" );
  EXPECT_STR( beFmtInt(1000, z), "1,000" );
  EXPECT_STR( beFmtInt(1234567, z), "1,234,567" );
  EXPECT_STR( beFmtInt(-12345, z), "-12,345" );
  EXPECT_STR( beFmtInt(-999, z), "-999" );
  EXPECT_STR( beFmtClock(0, z), "0:00" );
  EXPECT_STR( beFmtClock(7, z), "0:07" );
  EXPECT_STR( beFmtClock(754, z), "12:34" );
  EXPECT_STR( beFmtClock(3723, z), "1:02:03" );
  EXPECT_STR( beFmtDuration(0, z), "0s" );
  EXPECT_STR( beFmtDuration(45, z), "45s" );
  EXPECT_STR( beFmtDuration(1500, z), "25m" );
  EXPECT_STR( beFmtDuration(3900, z), "1h 05m" );
  EXPECT_STR( beFmtDuration(7200, z), "2h" );
  EXPECT_STR( beFmtDuration(4320000, z), "1,200h" );
}

/*
** Check that repairing RTF zIn gives zExpect, or, if zExpect is NULL,
** that zIn is left alone.
*/
static void checkRepair(int iLine, const char *zIn, const char *zExpect){
  i64 nOut = -1;
  char *zOut = beRtfRepair(zIn, (i64)strlen(zIn), &nOut);
  nTest++;
  if( zExpect==0 ){
    if( zOut ){
      nErr++;
      printf("line %d: unexpected repair: \"%s\"\n", iLine, zOut);
    }
  }else if( zOut==0 ){
    nErr++;
    printf("line %d: no repair, expected \"%s\"\n", iLine, zExpect);
  }else if( strcmp(zOut, zExpect)!=0 || nOut!=(i64)strlen(zExpect) ){
    nErr++;
    printf("line %d: repair is \"%s\", expected \"%s\"\n", iLine, zOut,
           zExpect);
  }
  free(zOut);
}

/*
** Tests of the repair of RTF that holds unescaped UTF-8.
*/
static void testRepair(void){
  /* Nothing to repair */
  checkRepair(__LINE__, "", 0);
  checkRepair(__LINE__, "{\\rtf1 plain \\'e9 \\u233? text}", 0);

  /* Characters of two, three, and four bytes */
  checkRepair(__LINE__, "{\\rtf1 caf\xc3\xa9 au lait}",
                        "{\\rtf1 caf\\u233 ? au lait}");
  checkRepair(__LINE__, "{\\rtf1 \xe0\xa4\xb8\xe0\xa5\x87}",
                        "{\\rtf1 \\u2360 ?\\u2375 ?}");
  checkRepair(__LINE__, "{\\rtf1 \xe2\x80\x94}",
                        "{\\rtf1 \\u8212 ?}");
  checkRepair(__LINE__, "{\\rtf1 \xef\xbc\x81}",
                        "{\\rtf1 \\u-255 ?}");
  checkRepair(__LINE__, "{\\rtf1 \xf0\x9f\x98\x80}",
                        "{\\rtf1 \\u-10179 ?\\u-8704 ?}");

  /* The number of placeholders follows \uc, which is local to a group */
  checkRepair(__LINE__, "{\\rtf1\\uc0 \xc3\xa9{\\uc2 \xc3\xa9}\xc3\xa9}",
                        "{\\rtf1\\uc0 \\u233 {\\uc2 \\u233 ??}\\u233 }");
  checkRepair(__LINE__, "{\\rtf1{\\uc0 x}\xc3\xa9}",
                        "{\\rtf1{\\uc0 x}\\u233 ?}");

  /* Existing escapes and escaped braces pass through untouched */
  checkRepair(__LINE__, "{\\rtf1 \\{\\'e9\\}\\\\ \\u8212- \xc3\xa9}",
                        "{\\rtf1 \\{\\'e9\\}\\\\ \\u8212- \\u233 ?}");

  /* Bytes above 127 that are not UTF-8: leave the whole file alone */
  checkRepair(__LINE__, "{\\rtf1 caf\xe9 au lait}", 0);
  checkRepair(__LINE__, "{\\rtf1 \xc3\xa9 and \xe9}", 0);
  checkRepair(__LINE__, "{\\rtf1 \xc0\xaf}", 0);
  checkRepair(__LINE__, "{\\rtf1 \xed\xa0\x80}", 0);
  checkRepair(__LINE__, "{\\rtf1 \xc3}", 0);

  /* Binary data: leave the whole file alone */
  checkRepair(__LINE__, "{\\rtf1 \xc3\xa9{\\pict\\bin2 ab}}", 0);
  checkRepair(__LINE__, "{\\rtf1 \xc3\xa9 \\binary }",
                        "{\\rtf1 \\u233 ? \\binary }");
}

/*
** A settable clock for the statistics tests.
*/
static time_t tFake;
static time_t fakeNow(void){ return tFake; }

/*
** Set the fake clock to noon, local time, on the given date.
*/
static void setDate(int Y, int M, int D){
  struct tm s;
  memset(&s, 0, sizeof(s));
  s.tm_year = Y - 1900;
  s.tm_mon = M - 1;
  s.tm_mday = D;
  s.tm_hour = 12;
  s.tm_isdst = -1;
  tFake = mktime(&s);
}

/*
** Tests of the statistics database.
*/
static void testStats(const char *zFile){
  StatsReport r;                  /* A report */

  remove(zFile);
  beStatsSetClock(fakeNow);

  /* Reports are well-formed even with no database */
  setDate(2026, 11, 11);
  EXPECT_INT( beStatsAddWords(5, 0), 0 );
  EXPECT_INT( beStatsReport(BE_WEEK, &r), 0 );
  EXPECT_INT( r.nBucket, 7 );
  EXPECT_INT( r.nWord, 0 );
  EXPECT_STR( r.a[6].zKey, "2026-11-11" );

  EXPECT_INT( beStatsOpen(zFile), 0 );

  /* Something written long ago: outside the year */
  setDate(2025, 11, 30);
  beStatsAddWords(9999, 0);

  /* 2025-12-01 is the first day covered by the year report */
  setDate(2025, 12, 1);
  beStatsAddWords(1000, 0);
  beStatsAddSeconds(3600);

  /* 31 days ago: outside the month, inside the year */
  setDate(2026, 10, 11);
  beStatsAddWords(400, 100);

  /* 29 days ago: the first day of the month report */
  setDate(2026, 10, 13);
  beStatsAddWords(200, 0);
  beStatsAddSeconds(600);

  /* 6 days ago: the first day of the week report */
  setDate(2026, 11, 5);
  beStatsAddWords(50, 0);
  beStatsAddWords(0, 20);
  beStatsAddSeconds(300);

  /* A day with time but no words */
  setDate(2026, 11, 9);
  beStatsAddSeconds(120);

  /* Today */
  setDate(2026, 11, 11);
  beStatsAddWords(500, 0);
  beStatsAddWords(0, 40);
  beStatsAddSeconds(1500);
  beStatsAddWords(0, 0);
  beStatsAddSeconds(0);

  /* The week */
  EXPECT_INT( beStatsReport(BE_WEEK, &r), 0 );
  EXPECT_INT( r.nBucket, 7 );
  EXPECT_INT( r.nDay, 7 );
  EXPECT_STR( r.a[0].zKey, "2026-11-05" );
  EXPECT_STR( r.a[0].zLabel, "Thu" );
  EXPECT_STR( r.a[0].zLong, "Thu, Nov 5" );
  EXPECT_STR( r.a[6].zKey, "2026-11-11" );
  EXPECT_STR( r.a[6].zLabel, "Wed" );
  EXPECT_INT( r.a[0].nWord, 30 );
  EXPECT_INT( r.a[0].nSec, 300 );
  EXPECT_INT( r.a[1].nWord, 0 );
  EXPECT_INT( r.a[4].nSec, 120 );
  EXPECT_INT( r.a[6].nWord, 460 );
  EXPECT_INT( r.a[6].nSec, 1500 );
  EXPECT_INT( r.nAdded, 550 );
  EXPECT_INT( r.nDeleted, 60 );
  EXPECT_INT( r.nWord, 490 );
  EXPECT_INT( r.nSec, 1920 );
  EXPECT_INT( r.nActive, 3 );
  EXPECT_INT( r.nPrevWord, 0 );
  EXPECT_INT( r.nPrevSec, 0 );

  /* The month */
  EXPECT_INT( beStatsReport(BE_MONTH, &r), 0 );
  EXPECT_INT( r.nBucket, 30 );
  EXPECT_INT( r.nDay, 30 );
  EXPECT_STR( r.a[0].zKey, "2026-10-13" );
  EXPECT_STR( r.a[0].zLabel, "Oct 13" );
  EXPECT_STR( r.a[29].zKey, "2026-11-11" );
  EXPECT_INT( r.a[0].nWord, 200 );
  EXPECT_INT( r.a[23].nWord, 30 );
  EXPECT_INT( r.nWord, 690 );
  EXPECT_INT( r.nSec, 2520 );
  EXPECT_INT( r.nActive, 4 );
  EXPECT_INT( r.nPrevWord, 300 );
  EXPECT_INT( r.nPrevSec, 0 );

  /* The year */
  EXPECT_INT( beStatsReport(BE_YEAR, &r), 0 );
  EXPECT_INT( r.nBucket, 12 );
  EXPECT_INT( r.nDay, 346 );
  EXPECT_STR( r.a[0].zKey, "2025-12" );
  EXPECT_STR( r.a[0].zLabel, "Dec" );
  EXPECT_STR( r.a[0].zLong, "December 2025" );
  EXPECT_STR( r.a[11].zKey, "2026-11" );
  EXPECT_INT( r.a[0].nWord, 1000 );
  EXPECT_INT( r.a[0].nSec, 3600 );
  EXPECT_INT( r.a[10].nWord, 500 );
  EXPECT_INT( r.a[11].nWord, 490 );
  EXPECT_INT( r.a[11].nSec, 1920 );
  EXPECT_INT( r.nWord, 1990 );
  EXPECT_INT( r.nSec, 6120 );
  EXPECT_INT( r.nActive, 6 );
  EXPECT_INT( r.nPrevWord, 9999 );
  EXPECT_INT( r.nPrevSec, 0 );

  /* The week that ended on 2026-10-19 follows one with 300 net words */
  setDate(2026, 10, 19);
  EXPECT_INT( beStatsReport(BE_WEEK, &r), 0 );
  EXPECT_INT( r.nWord, 200 );
  EXPECT_INT( r.nPrevWord, 300 );
  EXPECT_INT( r.nPrevSec, 0 );
  setDate(2026, 11, 11);

  /* Month arithmetic at the end of a long month, and in January */
  setDate(2026, 3, 31);
  EXPECT_INT( beStatsReport(BE_YEAR, &r), 0 );
  EXPECT_STR( r.a[0].zKey, "2025-04" );
  EXPECT_STR( r.a[10].zKey, "2026-02" );
  EXPECT_STR( r.a[11].zKey, "2026-03" );
  EXPECT_INT( r.nDay, 365 );
  setDate(2026, 1, 1);
  EXPECT_INT( beStatsReport(BE_WEEK, &r), 0 );
  EXPECT_STR( r.a[0].zKey, "2025-12-26" );

  /* The data survives closing and reopening the database */
  beStatsClose();
  EXPECT_INT( beStatsOpen(zFile), 0 );
  setDate(2026, 11, 11);
  EXPECT_INT( beStatsReport(BE_WEEK, &r), 0 );
  EXPECT_INT( r.nWord, 490 );
  beStatsClose();
  beStatsSetClock(0);
}

int main(int argc, char **argv){
  if( argc!=2 ){
    fprintf(stderr, "Usage: %s TEMPFILE\n", argv[0]);
    return 1;
  }
  testWordCount();
  testFormat();
  testRepair();
  testStats(argv[1]);
  printf("%d errors out of %d tests\n", nErr, nTest);
  return nErr!=0;
}
