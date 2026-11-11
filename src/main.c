/*
** 2026 November 11
**
** Program startup and the application delegate.
**
** The application delegate is an instance of class BeAppDelegate, which
** is created here at run time.  It is the target of the menu items and
** buttons that are specific to BestEdit: those that control the writing
** timer and the statistics window.  Everything else in the menu bar is
** handled by Cocoa itself.
*/
#include "cocoa.h"
#include "bestedit.h"
#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>

/*
** The one and only application delegate.
*/
id beDelegate = 0;

/*
** The timer calls this routine each second while it runs, and whenever
** it starts or stops.
*/
void beUiTimerChanged(void){
  beDocTimerChanged();
  beMenuTimerChanged();
}

/*
** The timer calls this routine after adding time to the statistics
** database.
*/
void beUiStatsChanged(void){
  beStatsWinRefresh();
}

/*
** Action methods of the application delegate.
*/
static void appToggleTimer(id self, SEL NotUsed, id pSender){
  UNUSED_PARAMETER(self);
  UNUSED_PARAMETER(NotUsed);
  UNUSED_PARAMETER(pSender);
  beTimerToggle();
}
static void appResetTimer(id self, SEL NotUsed, id pSender){
  UNUSED_PARAMETER(self);
  UNUSED_PARAMETER(NotUsed);
  UNUSED_PARAMETER(pSender);
  beTimerReset();
}
static void appToggleAuto(id self, SEL NotUsed, id pSender){
  UNUSED_PARAMETER(self);
  UNUSED_PARAMETER(NotUsed);
  UNUSED_PARAMETER(pSender);
  beTimerSetAuto(!beTimerIsAuto());
}
static void appShowStats(id self, SEL NotUsed, id pSender){
  UNUSED_PARAMETER(self);
  UNUSED_PARAMETER(NotUsed);
  UNUSED_PARAMETER(pSender);
  beStatsWinShow();
}

/*
** The action of the Week/Month/Year selector in the statistics window.
*/
static void appStatsPeriod(id self, SEL NotUsed, id pSender){
  UNUSED_PARAMETER(self);
  UNUSED_PARAMETER(NotUsed);
  beStatsWinSetPeriod((int)msg0(long, pSender, "selectedSegment"));
}

/*
** The delegate is also the delegate of the statistics window.  Bring
** the statistics up to date whenever that window comes to the front.
*/
static void appWindowDidBecomeKey(id self, SEL NotUsed, id pNote){
  UNUSED_PARAMETER(self);
  UNUSED_PARAMETER(NotUsed);
  UNUSED_PARAMETER(pNote);
  beStatsWinRefresh();
}

/*
** The application is about to exit.  Record any time that the timer has
** not yet written to the database.
*/
static void appWillTerminate(id self, SEL NotUsed, id pNote){
  UNUSED_PARAMETER(self);
  UNUSED_PARAMETER(NotUsed);
  UNUSED_PARAMETER(pNote);
  beTimerShutdown();
  beStatsClose();
}

/*
** Tell Cocoa that saved window state may be decoded securely.
*/
static BOOL appSecureState(id self, SEL NotUsed, id pApp){
  UNUSED_PARAMETER(self);
  UNUSED_PARAMETER(NotUsed);
  UNUSED_PARAMETER(pApp);
  return YES;
}

/*
** Create the BeAppDelegate class and the one instance of it.
*/
static void mainCreateDelegate(void){
  Class pClass;                   /* The new class */
  pClass = objc_allocateClassPair(objc_getClass("NSObject"),
                                  "BeAppDelegate", 0);
  class_addMethod(pClass, sel_registerName("toggleTimer:"),
                  (IMP)appToggleTimer, "v@:@");
  class_addMethod(pClass, sel_registerName("resetTimer:"),
                  (IMP)appResetTimer, "v@:@");
  class_addMethod(pClass, sel_registerName("toggleAutoTimer:"),
                  (IMP)appToggleAuto, "v@:@");
  class_addMethod(pClass, sel_registerName("showStats:"),
                  (IMP)appShowStats, "v@:@");
  class_addMethod(pClass, sel_registerName("statsPeriod:"),
                  (IMP)appStatsPeriod, "v@:@");
  class_addMethod(pClass, sel_registerName("windowDidBecomeKey:"),
                  (IMP)appWindowDidBecomeKey, "v@:@");
  class_addMethod(pClass, sel_registerName("applicationWillTerminate:"),
                  (IMP)appWillTerminate, "v@:@");
  class_addMethod(pClass,
                  sel_registerName("applicationSupportsSecureRestorableState:"),
                  (IMP)appSecureState, BE_BOOL "@:@");
  objc_registerClassPair(pClass);
  beDelegate = msg0(id, msg0(id, (id)pClass, "alloc"), "init");
}

/*
** Open the statistics database.
**
** The database is normally the file
**
**     ~/Library/Application Support/BestEdit/stats.db
**
** but if the environment variable BESTEDIT_DB is set, it names the file
** to use instead.  If the database cannot be opened, BestEdit still
** runs but statistics are not recorded.
*/
static void mainOpenStats(void){
  const char *zEnv = getenv("BESTEDIT_DB");
  char zFile[1100];               /* Name of the database file */
  int rc;                         /* Result code from SQLite */

  if( zEnv && zEnv[0] ){
    snprintf(zFile, sizeof(zFile), "%s", zEnv);
  }else{
    char zDir[1024];              /* Directory holding the database */
    const char *zBase = 0;        /* The Application Support directory */
    id aPath = NSSearchPathForDirectoriesInDomains(
                   NSApplicationSupportDirectory, NSUserDomainMask, YES);
    id pFirst = msg0(id, aPath, "firstObject");
    if( pFirst ) zBase = msg0(const char*, pFirst, "fileSystemRepresentation");
    if( zBase==0 ){
      fprintf(stderr, "BestEdit: cannot locate Application Support\n");
      return;
    }
    snprintf(zDir, sizeof(zDir), "%s/BestEdit", zBase);
    mkdir(zBase, 0755);
    mkdir(zDir, 0755);
    snprintf(zFile, sizeof(zFile), "%s/stats.db", zDir);
  }
  rc = beStatsOpen(zFile);
  if( rc ){
    fprintf(stderr, "BestEdit: cannot open statistics database \"%s\""
                    " (SQLite error %d)\n", zFile, rc);
  }
}

int main(int argc, char **argv){
  id pPool;                       /* Autorelease pool used during startup */
  id pApp;                        /* The NSApplication object */

  UNUSED_PARAMETER(argc);
  UNUSED_PARAMETER(argv);
  pPool = beNew("NSAutoreleasePool");
  pApp = msg0(id, cls("NSApplication"), "sharedApplication");
  msg1(BOOL, pApp, "setActivationPolicy:", long, 0);
  mainOpenStats();
  beDocRegister();
  beStatsWinRegister();
  mainCreateDelegate();
  msg1(void, pApp, "setDelegate:", id, beDelegate);
  beMenuBuild();
  beTimerInit();
#ifdef BE_TEST
  beRtfTest();
  beUiTestStart();
#endif
  msg0(void, pPool, "drain");
  msg0(void, pApp, "run");
  return 0;
}
