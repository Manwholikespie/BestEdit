/*
** 2026 November 11
**
** Internal interface definitions for BestEdit.
**
** BestEdit is a small rich-text editor for macOS in the spirit of
** TextEdit.  In addition to editing RTF it keeps a live word count,
** runs a writing timer, and records how many words were written and how
** much time was spent each day so that the totals for the past week,
** month, and year can be reviewed.
**
** The source is organized as follows:
**
**    main.c        Program startup and the application delegate
**    menu.c        Construction of the main menu bar
**    doc.c         The document class and the editor window
**    statswin.c    The statistics window and its bar charts
**    cocoa.c       Helpers for talking to Cocoa from C
**    timer.c       The writing timer                    (no Cocoa)
**    stats.c       Persistent statistics in SQLite      (portable)
**    wordcount.c   Word and character counting          (portable)
**    rtfrepair.c   Repair of RTF that holds raw UTF-8   (portable)
**    util.c        Number and time formatting           (portable)
*/
#ifndef BESTEDIT_H
#define BESTEDIT_H

#include <time.h>

/*
** Integers of known sizes.
*/
typedef long long i64;             /* 8-byte signed integer */
typedef unsigned short u16;        /* 2-byte unsigned integer */
typedef unsigned char u8;          /* 1-byte unsigned integer */

/*
** A macro to discourage compiler warnings about unused parameters.
*/
#define UNUSED_PARAMETER(x) (void)(x)

/*
** The number of elements in a fixed-size array.
*/
#define ArraySize(X) ((int)(sizeof(X)/sizeof(X[0])))

/*
** The time periods that a statistics report can cover.
*/
#define BE_WEEK    0               /* The past 7 days, one bucket per day */
#define BE_MONTH   1               /* The past 30 days, one bucket per day */
#define BE_YEAR    2               /* The past 12 months, one per month */

/*
** Maximum number of buckets in any statistics report.
*/
#define BE_MX_BUCKET 30

/*
** If the automatic timer is enabled, the timer pauses after this many
** seconds have gone by without a keystroke.
*/
#define BE_IDLE_SECONDS 180

/*
** Forward declarations of structures.
*/
typedef struct StatsBucket StatsBucket;
typedef struct StatsReport StatsReport;
typedef struct WordCounter WordCounter;

/*
** An instance of the following structure accumulates the word count of
** text that is fed to it in pieces.  See wordcount.c.
*/
struct WordCounter {
  i64 nWord;              /* Number of words seen so far */
  i64 nChar;              /* Number of characters, not counting line breaks */
  u8 bCounted;            /* True if the current run was counted as a word */
};

/*
** One bar of a statistics report: a single day or a single month.
*/
struct StatsBucket {
  char zKey[12];          /* "YYYY-MM-DD" for a day, "YYYY-MM" for a month */
  char zLabel[12];        /* Short label for the chart axis.  Ex: "Nov 11" */
  char zLong[32];         /* Long name.  Ex: "Wed, Nov 11" */
  i64 nWord;              /* Net words written.  Might be negative */
  i64 nSec;               /* Seconds spent with the timer running */
};

/*
** A statistics report as filled in by beStatsReport().
*/
struct StatsReport {
  int ePeriod;            /* One of BE_WEEK, BE_MONTH, or BE_YEAR */
  int nBucket;            /* Number of entries of a[] in use */
  int nDay;               /* Number of days covered by the report */
  int nActive;            /* Days on which anything was written or timed */
  i64 nAdded;             /* Total words added */
  i64 nDeleted;           /* Total words removed */
  i64 nWord;              /* Net words written: nAdded - nDeleted */
  i64 nSec;               /* Total seconds with the timer running */
  i64 nPrevWord;          /* Net words in the period before this one */
  i64 nPrevSec;           /* Seconds in the period before this one */
  StatsBucket a[BE_MX_BUCKET];   /* Oldest bucket first */
};

/*
** wordcount.c
*/
void beWordInit(WordCounter*);
void beWordFeed(WordCounter*, const u16*, int);

/*
** util.c
*/
char *beFmtInt(i64, char*);
char *beFmtClock(i64, char*);
char *beFmtDuration(i64, char*);

/*
** rtfrepair.c
*/
char *beRtfRepair(const char*, i64, i64*);

/*
** stats.c
*/
int beStatsOpen(const char*);
void beStatsClose(void);
int beStatsAddWords(i64, i64);
int beStatsAddSeconds(i64);
int beStatsReport(int, StatsReport*);
void beStatsSetClock(time_t(*)(void));

/*
** timer.c
*/
void beTimerInit(void);
void beTimerShutdown(void);
void beTimerToggle(void);
void beTimerReset(void);
void beTimerActivity(void);
void beTimerSetAuto(int);
int beTimerIsAuto(void);
int beTimerIsRunning(void);
i64 beTimerSession(void);

/*
** Routines that the timer calls to keep the user interface current.
** Both are implemented in main.c.
*/
void beUiTimerChanged(void);
void beUiStatsChanged(void);

/*
** doc.c
*/
void beDocRegister(void);
void beDocTimerChanged(void);

/*
** menu.c
*/
void beMenuBuild(void);
void beMenuTimerChanged(void);

/*
** statswin.c
*/
void beStatsWinRegister(void);
void beStatsWinShow(void);
void beStatsWinRefresh(void);
void beStatsWinSetPeriod(int);

/*
** Interfaces that exist only in test builds.  See test/uitest.c.
*/
#ifdef BE_TEST
void beUiTestStart(void);
void *beTestDocText(void*);
void *beTestDocCount(void*);
void *beTestDocLoaded(void*);
void *beTestDocAttr(void*);
i64 beTestDocWords(void*, i64*);
void beRtfTest(void);
void *beTestStatsWindow(void);
void beTestStatsHover(int, int);
#endif

#endif /* BESTEDIT_H */
