/*
** 2026 November 11
**
** This file implements the persistent record of how much was written
** and how much time was spent writing.
**
** The record is an SQLite database holding a single table with one row
** for each day on which something happened:
**
**     CREATE TABLE daily(
**       day TEXT PRIMARY KEY,   -- Local date: YYYY-MM-DD
**       nAdded INTEGER,         -- Words added on this day
**       nDeleted INTEGER,       -- Words removed on this day
**       nSecond INTEGER         -- Seconds the timer ran on this day
**     );
**
** Words added and words removed are stored separately so that no
** information is lost.  The number reported to the user is the net
** number of words written, which is the difference of the two.
**
** If the database cannot be opened then every routine in this file is
** a harmless no-op and reports come back empty.
**
** This file is portable C and makes no use of Cocoa.
*/
#include "bestedit.h"
#include <sqlite3.h>
#include <stdio.h>
#include <string.h>

/*
** All global state of this module.
*/
static struct StatsGlobal {
  sqlite3 *db;                    /* The database connection, or NULL */
  sqlite3_stmt *pAdd;             /* Statement that adds to today's row */
  time_t (*xNow)(void);           /* Clock override for testing, or NULL */
} stats;

/*
** The database schema.  PRAGMA user_version identifies the schema
** version so that future versions can upgrade it.
*/
static const char zSchema[] =
  "PRAGMA journal_mode=WAL;\n"
  "PRAGMA synchronous=NORMAL;\n"
  "CREATE TABLE IF NOT EXISTS daily(\n"
  "  day TEXT PRIMARY KEY,                 -- Local date: YYYY-MM-DD\n"
  "  nAdded INTEGER NOT NULL DEFAULT 0,    -- Words added\n"
  "  nDeleted INTEGER NOT NULL DEFAULT 0,  -- Words removed\n"
  "  nSecond INTEGER NOT NULL DEFAULT 0    -- Seconds the timer ran\n"
  ") WITHOUT ROWID;\n"
  "PRAGMA user_version=1;\n"
;

/*
** Add to the totals for a day, creating the row for that day if it does
** not yet exist.
*/
static const char zAddSql[] =
  "INSERT INTO daily(day, nAdded, nDeleted, nSecond)"
  " VALUES(?1, ?2, ?3, ?4)"
  " ON CONFLICT(day) DO UPDATE SET"
  "   nAdded = nAdded + excluded.nAdded,"
  "   nDeleted = nDeleted + excluded.nDeleted,"
  "   nSecond = nSecond + excluded.nSecond"
;

/*
** Return the current time.
*/
static time_t statsNow(void){
  return stats.xNow ? stats.xNow() : time(0);
}

/*
** Register a function to be used in place of time() to find the current
** time.  Or pass in NULL to go back to using time().  This is a testing
** interface.
*/
void beStatsSetClock(time_t (*xNow)(void)){
  stats.xNow = xNow;
}

/*
** Compute the local calendar date that is nDay days and nMonth months
** before time t and write it into *pOut.  If nMonth is not zero then the
** result is the first day of the month.
**
** The time of day of the result is noon, which keeps the date
** arithmetic clear of the hours that are skipped or repeated when
** daylight saving time begins or ends.
*/
static void statsDate(time_t t, int nDay, int nMonth, struct tm *pOut){
  struct tm s;                    /* The broken-down time */
  localtime_r(&t, &s);
  s.tm_hour = 12;
  s.tm_min = 0;
  s.tm_sec = 0;
  s.tm_isdst = -1;
  if( nMonth ){
    s.tm_mday = 1;
    s.tm_mon -= nMonth;
  }
  s.tm_mday -= nDay;
  mktime(&s);
  *pOut = s;
}

/*
** Open the statistics database in file zFile, creating it if necessary.
** Return SQLITE_OK on success or an SQLite error code on failure.
*/
int beStatsOpen(const char *zFile){
  int rc;                         /* Result code */

  beStatsClose();
  rc = sqlite3_open(zFile, &stats.db);
  if( rc==SQLITE_OK ){
    sqlite3_busy_timeout(stats.db, 2000);
    rc = sqlite3_exec(stats.db, zSchema, 0, 0, 0);
  }
  if( rc==SQLITE_OK ){
    rc = sqlite3_prepare_v2(stats.db, zAddSql, -1, &stats.pAdd, 0);
  }
  if( rc!=SQLITE_OK ){
    beStatsClose();
  }
  return rc;
}

/*
** Close the statistics database.  This is a no-op if the database is
** not open.
*/
void beStatsClose(void){
  sqlite3_finalize(stats.pAdd);
  stats.pAdd = 0;
  sqlite3_close(stats.db);
  stats.db = 0;
}

/*
** Add nAdded, nDeleted, and nSec to the totals for the current day.
*/
static int statsAdd(i64 nAdded, i64 nDeleted, i64 nSec){
  struct tm s;                    /* Today */
  char zDay[16];                  /* Today as YYYY-MM-DD */
  int rc;                         /* Result code */

  if( stats.db==0 ) return SQLITE_OK;
  statsDate(statsNow(), 0, 0, &s);
  strftime(zDay, sizeof(zDay), "%Y-%m-%d", &s);
  sqlite3_bind_text(stats.pAdd, 1, zDay, -1, SQLITE_TRANSIENT);
  sqlite3_bind_int64(stats.pAdd, 2, nAdded);
  sqlite3_bind_int64(stats.pAdd, 3, nDeleted);
  sqlite3_bind_int64(stats.pAdd, 4, nSec);
  sqlite3_step(stats.pAdd);
  rc = sqlite3_reset(stats.pAdd);
  return rc;
}

/*
** Record that nAdded words were added and nDeleted words were removed
** just now.  Return an SQLite result code.
*/
int beStatsAddWords(i64 nAdded, i64 nDeleted){
  if( nAdded<=0 && nDeleted<=0 ) return SQLITE_OK;
  return statsAdd(nAdded>0 ? nAdded : 0, nDeleted>0 ? nDeleted : 0, 0);
}

/*
** Record that nSec seconds were just spent writing.  Return an SQLite
** result code.
*/
int beStatsAddSeconds(i64 nSec){
  if( nSec<=0 ) return SQLITE_OK;
  return statsAdd(0, 0, nSec);
}

/*
** Fill in the keys and labels of the buckets of report p, which covers
** the period p->ePeriod ending at time tNow.
*/
static void statsBuckets(StatsReport *p, time_t tNow){
  struct tm s;                    /* Date of the current bucket */
  char zDow[8];                   /* Day of week.  Ex: "Wed" */
  char zMon[8];                   /* Month.  Ex: "Nov" */
  int i;                          /* Loop counter */

  p->nBucket = p->ePeriod==BE_WEEK ? 7 : p->ePeriod==BE_MONTH ? 30 : 12;
  for(i=0; i<p->nBucket; i++){
    StatsBucket *pB = &p->a[i];
    int nBack = p->nBucket - 1 - i;
    if( p->ePeriod==BE_YEAR ){
      statsDate(tNow, 0, nBack, &s);
      strftime(pB->zKey, sizeof(pB->zKey), "%Y-%m", &s);
      strftime(pB->zLabel, sizeof(pB->zLabel), "%b", &s);
      strftime(pB->zLong, sizeof(pB->zLong), "%B %Y", &s);
    }else{
      statsDate(tNow, nBack, 0, &s);
      strftime(pB->zKey, sizeof(pB->zKey), "%Y-%m-%d", &s);
      strftime(zDow, sizeof(zDow), "%a", &s);
      strftime(zMon, sizeof(zMon), "%b", &s);
      if( p->ePeriod==BE_WEEK ){
        snprintf(pB->zLabel, sizeof(pB->zLabel), "%s", zDow);
      }else{
        snprintf(pB->zLabel, sizeof(pB->zLabel), "%s %d", zMon, s.tm_mday);
      }
      snprintf(pB->zLong, sizeof(pB->zLong), "%s, %s %d",
               zDow, zMon, s.tm_mday);
    }
  }

  /* Count the days covered.  For a year this is the number of days from
  ** the first day of the oldest month up to and including today. */
  if( p->ePeriod==BE_YEAR ){
    struct tm sToday;
    statsDate(tNow, 0, p->nBucket-1, &s);
    statsDate(tNow, 0, 0, &sToday);
    p->nDay = 1 + (int)((difftime(mktime(&sToday), mktime(&s)) + 43200.0)
                        / 86400.0);
  }else{
    p->nDay = p->nBucket;
  }
}

/*
** Fill in p->nPrevWord and p->nPrevSec, the totals for the period of
** the same length that came just before the period of report p, so
** that the two can be compared.  The buckets of p must already be
** filled in.  Return an SQLite result code.
*/
static int statsPrevious(StatsReport *p, time_t tNow){
  static const char zSql[] =
    "SELECT coalesce(sum(nAdded-nDeleted),0), coalesce(sum(nSecond),0)"
    " FROM daily WHERE day>=?1 AND day<?2";
  sqlite3_stmt *pStmt = 0;        /* The query */
  struct tm s;                    /* First day of the earlier period */
  char zFrom[16];                 /* The same, as text */
  int rc;                         /* Result code */

  if( p->ePeriod==BE_YEAR ){
    statsDate(tNow, 0, 2*p->nBucket-1, &s);
    strftime(zFrom, sizeof(zFrom), "%Y-%m", &s);
  }else{
    statsDate(tNow, 2*p->nBucket-1, 0, &s);
    strftime(zFrom, sizeof(zFrom), "%Y-%m-%d", &s);
  }
  rc = sqlite3_prepare_v2(stats.db, zSql, -1, &pStmt, 0);
  if( rc!=SQLITE_OK ) return rc;
  sqlite3_bind_text(pStmt, 1, zFrom, -1, SQLITE_STATIC);
  sqlite3_bind_text(pStmt, 2, p->a[0].zKey, -1, SQLITE_STATIC);
  if( sqlite3_step(pStmt)==SQLITE_ROW ){
    p->nPrevWord = sqlite3_column_int64(pStmt, 0);
    p->nPrevSec = sqlite3_column_int64(pStmt, 1);
  }
  return sqlite3_finalize(pStmt);
}

/*
** Generate a report covering period ePeriod (one of BE_WEEK, BE_MONTH,
** or BE_YEAR) ending today and write it into *p.
**
** The buckets of the report are always filled in, even if the database
** is not open or an error occurs, so that the caller always has a
** well-formed report to display.  Return an SQLite result code.
*/
int beStatsReport(int ePeriod, StatsReport *p){
  static const char zSql[] =
    "SELECT day, nAdded, nDeleted, nSecond FROM daily"
    " WHERE day>=?1 AND day<=?2"
    " ORDER BY day";
  sqlite3_stmt *pStmt = 0;        /* The query */
  time_t tNow = statsNow();       /* Current time */
  struct tm s;                    /* Today */
  char zToday[16];                /* Today as YYYY-MM-DD */
  int iB = 0;                     /* Index of the current bucket */
  int rc;                         /* Result code */

  memset(p, 0, sizeof(*p));
  p->ePeriod = ePeriod;
  statsBuckets(p, tNow);
  if( stats.db==0 ) return SQLITE_OK;

  statsDate(tNow, 0, 0, &s);
  strftime(zToday, sizeof(zToday), "%Y-%m-%d", &s);
  rc = sqlite3_prepare_v2(stats.db, zSql, -1, &pStmt, 0);
  if( rc!=SQLITE_OK ) return rc;
  sqlite3_bind_text(pStmt, 1, p->a[0].zKey, -1, SQLITE_STATIC);
  sqlite3_bind_text(pStmt, 2, zToday, -1, SQLITE_STATIC);
  while( sqlite3_step(pStmt)==SQLITE_ROW ){
    const char *zDay = (const char*)sqlite3_column_text(pStmt, 0);
    i64 nAdded = sqlite3_column_int64(pStmt, 1);
    i64 nDeleted = sqlite3_column_int64(pStmt, 2);
    i64 nSec = sqlite3_column_int64(pStmt, 3);
    if( zDay==0 ) continue;

    /* Rows arrive in date order, as are the buckets, so the bucket for
    ** this row is never earlier than the bucket for the previous row. */
    while( iB<p->nBucket
        && strncmp(zDay, p->a[iB].zKey, strlen(p->a[iB].zKey))!=0 ){
      iB++;
    }
    if( iB>=p->nBucket ) break;
    p->a[iB].nWord += nAdded - nDeleted;
    p->a[iB].nSec += nSec;
    p->nAdded += nAdded;
    p->nDeleted += nDeleted;
    p->nSec += nSec;
    if( nAdded>0 || nSec>0 ) p->nActive++;
  }
  p->nWord = p->nAdded - p->nDeleted;
  rc = sqlite3_finalize(pStmt);
  if( rc==SQLITE_OK ) rc = statsPrevious(p, tNow);
  return rc;
}
