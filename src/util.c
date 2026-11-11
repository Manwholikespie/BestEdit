/*
** 2026 November 11
**
** Utility functions that render numbers and times as text.
**
** Every function here writes into a buffer supplied by the caller and
** returns a pointer to that same buffer.  A buffer of 32 bytes is always
** big enough.
**
** This file is portable C and makes no use of Cocoa.
*/
#include "bestedit.h"
#include <stdio.h>

/*
** Render integer n with a comma between each group of three digits.
** Ex: "1,234,567"
*/
char *beFmtInt(i64 n, char *zBuf){
  char zDigit[24];                /* The digits of n */
  int nDigit;                     /* Number of digits in zDigit[] */
  int i;                          /* Index into zDigit[] */
  int j = 0;                      /* Index into zBuf[] */
  unsigned long long u;           /* Absolute value of n */

  u = n<0 ? 0-(unsigned long long)n : (unsigned long long)n;
  nDigit = snprintf(zDigit, sizeof(zDigit), "%llu", u);
  if( n<0 ) zBuf[j++] = '-';
  for(i=0; i<nDigit; i++){
    if( i>0 && (nDigit-i)%3==0 ) zBuf[j++] = ',';
    zBuf[j++] = zDigit[i];
  }
  zBuf[j] = 0;
  return zBuf;
}

/*
** Render a time interval of nSec seconds the way a stopwatch would.
** Ex: "0:07", "12:34", "1:02:03"
*/
char *beFmtClock(i64 nSec, char *zBuf){
  int h, m, s;                    /* Hours, minutes, and seconds */
  if( nSec<0 ) nSec = 0;
  if( nSec>359999 ) nSec = 359999;
  h = (int)(nSec/3600);
  m = (int)((nSec/60)%60);
  s = (int)(nSec%60);
  if( h>0 ){
    snprintf(zBuf, 32, "%d:%02d:%02d", h, m, s);
  }else{
    snprintf(zBuf, 32, "%d:%02d", m, s);
  }
  return zBuf;
}

/*
** Render a time interval of nSec seconds in a rounded-off form that is
** suitable for a summary.  Ex: "45s", "25m", "2h", "1h 05m", "1,200h"
*/
char *beFmtDuration(i64 nSec, char *zBuf){
  i64 h, m;                       /* Hours and minutes */
  if( nSec<0 ) nSec = 0;
  if( nSec<60 ){
    snprintf(zBuf, 32, "%ds", (int)nSec);
    return zBuf;
  }
  h = nSec/3600;
  m = (nSec/60)%60;
  if( h==0 ){
    snprintf(zBuf, 32, "%dm", (int)m);
  }else if( h>=100 || m==0 ){
    char zHour[32];
    snprintf(zBuf, 32, "%sh", beFmtInt(h, zHour));
  }else{
    snprintf(zBuf, 32, "%dh %02dm", (int)h, (int)m);
  }
  return zBuf;
}
