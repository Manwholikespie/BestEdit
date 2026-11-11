/*
** 2026 November 11
**
** This file implements the writing timer.
**
** The timer is a stopwatch.  While it runs, the time that goes by is
** added both to a session total, which is what the editor windows
** display, and to the statistics database, where it is attributed to
** the current day.
**
** The timer can be started and paused by hand.  It also has an
** automatic mode, enabled by default, in which it starts by itself
** when the user begins to type and pauses by itself after
** BE_IDLE_SECONDS seconds without a keystroke.
**
** There is one timer for the whole application, not one per document.
**
** This file uses CoreFoundation, which is a C API, but not Cocoa.
*/
#include "bestedit.h"
#include <CoreFoundation/CoreFoundation.h>

/*
** Elapsed time is written to the database whenever this many seconds
** have accumulated, and whenever the timer is paused.
*/
#define TIMER_FLUSH_SECONDS 10

/*
** The name under which the automatic-mode setting is saved in the user
** defaults.
*/
#define TIMER_AUTO_KEY CFSTR("AutoTimer")

/*
** All global state of this module.
*/
static struct TimerGlobal {
  int bRunning;                   /* True if the timer is running */
  int bAuto;                      /* True if in automatic mode */
  CFAbsoluteTime tLast;           /* When time was last accounted for */
  CFAbsoluteTime tActive;         /* When the user last typed */
  double rSession;                /* Seconds accumulated this session */
  double rPending;                /* Seconds not yet in the database */
  CFRunLoopTimerRef pTick;        /* Fires each second while running */
} timer;

/*
** Account for the time that has gone by since the previous call.
**
** The tick fires once a second.  If far more time than that appears to
** have gone by, then the computer was asleep or the clock was changed.
** Such an interval is not time spent writing and is discarded.
*/
static void timerAccount(void){
  CFAbsoluteTime tNow = CFAbsoluteTimeGetCurrent();
  double rElapsed = tNow - timer.tLast;
  timer.tLast = tNow;
  if( rElapsed<0.0 || rElapsed>5.0 ) return;
  timer.rSession += rElapsed;
  timer.rPending += rElapsed;
}

/*
** Move whole seconds of pending time into the database.  If bAll is
** true, round the remaining fraction of a second to the nearest second
** and move that as well.
*/
static void timerFlush(int bAll){
  i64 nSec = (i64)(timer.rPending + (bAll ? 0.5 : 0.0));
  if( nSec>0 ){
    beStatsAddSeconds(nSec);
    timer.rPending -= (double)nSec;
    if( timer.rPending<0.0 || bAll ) timer.rPending = 0.0;
    beUiStatsChanged();
  }
}

/*
** Stop the timer from running.
*/
static void timerPause(void){
  if( !timer.bRunning ) return;
  timerAccount();
  timerFlush(0);
  timer.bRunning = 0;
  if( timer.pTick ){
    CFRunLoopTimerInvalidate(timer.pTick);
    CFRelease(timer.pTick);
    timer.pTick = 0;
  }
  beUiTimerChanged();
}

/*
** This routine is called once per second while the timer runs.
*/
static void timerTick(CFRunLoopTimerRef NotUsed, void *NotUsed2){
  UNUSED_PARAMETER(NotUsed);
  UNUSED_PARAMETER(NotUsed2);
  timerAccount();
  if( timer.rPending>=TIMER_FLUSH_SECONDS ) timerFlush(0);
  if( timer.bAuto && timer.tLast-timer.tActive>BE_IDLE_SECONDS ){
    timerPause();
  }else{
    beUiTimerChanged();
  }
}

/*
** Start the timer running.
*/
static void timerStart(void){
  if( timer.bRunning ) return;
  timer.bRunning = 1;
  timer.tLast = timer.tActive = CFAbsoluteTimeGetCurrent();
  timer.pTick = CFRunLoopTimerCreate(0, timer.tLast+1.0, 1.0, 0, 0,
                                     timerTick, 0);
  if( timer.pTick ){
    CFRunLoopTimerSetTolerance(timer.pTick, 0.1);
    CFRunLoopAddTimer(CFRunLoopGetMain(), timer.pTick,
                      kCFRunLoopCommonModes);
  }
  beUiTimerChanged();
}

/*
** Initialize the timer.  This must be called once at startup.
*/
void beTimerInit(void){
  CFPropertyListRef pVal;         /* The saved automatic-mode setting */
  timer.bAuto = 1;
  pVal = CFPreferencesCopyAppValue(TIMER_AUTO_KEY,
                                   kCFPreferencesCurrentApplication);
  if( pVal ){
    if( CFGetTypeID(pVal)==CFBooleanGetTypeID() ){
      timer.bAuto = CFBooleanGetValue((CFBooleanRef)pVal);
    }
    CFRelease(pVal);
  }
  beUiTimerChanged();
}

/*
** Stop the timer and make sure all elapsed time has been recorded.
** This is called as the application exits.
*/
void beTimerShutdown(void){
  timerPause();
  timerFlush(1);
}

/*
** Start the timer if it is paused, or pause it if it is running.
*/
void beTimerToggle(void){
  if( timer.bRunning ){
    timerPause();
  }else{
    timerStart();
  }
}

/*
** Set the session total back to zero.  Time already recorded in the
** statistics database is unaffected.
*/
void beTimerReset(void){
  if( timer.bRunning ) timerAccount();
  timer.rSession = 0.0;
  beUiTimerChanged();
}

/*
** The editor calls this routine each time the user changes the text of
** a document.
*/
void beTimerActivity(void){
  timer.tActive = CFAbsoluteTimeGetCurrent();
  if( timer.bAuto && !timer.bRunning ) timerStart();
}

/*
** Turn automatic mode on or off and remember the setting.
*/
void beTimerSetAuto(int bAuto){
  timer.bAuto = bAuto!=0;
  timer.tActive = CFAbsoluteTimeGetCurrent();
  CFPreferencesSetAppValue(TIMER_AUTO_KEY,
                           bAuto ? kCFBooleanTrue : kCFBooleanFalse,
                           kCFPreferencesCurrentApplication);
  CFPreferencesAppSynchronize(kCFPreferencesCurrentApplication);
  beUiTimerChanged();
}

/*
** Return true if automatic mode is enabled.
*/
int beTimerIsAuto(void){
  return timer.bAuto;
}

/*
** Return true if the timer is running.
*/
int beTimerIsRunning(void){
  return timer.bRunning;
}

/*
** Return the number of whole seconds accumulated this session.
*/
i64 beTimerSession(void){
  return (i64)timer.rSession;
}
