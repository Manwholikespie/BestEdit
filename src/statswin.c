/*
** 2026 November 11
**
** This file implements the statistics window.
**
** The window shows how much was written, and how much time was spent
** writing, over the past week, month, or year.  From top to bottom it
** contains:
**
**    *  A selector for the period.
**    *  A row of four headline numbers.
**    *  A bar chart of words written.
**    *  A bar chart of time spent.
**
** Words and time are drawn as two separate charts because they are
** measured in different units and so cannot share an axis.
**
** The charts are instances of BeChartView, a subclass of NSView that is
** created here at run time.  The only instance variable of BeChartView
** is a pointer to a Chart structure.  Shapes are drawn with
** CoreGraphics, which is a C API.
*/
#include "cocoa.h"
#include "bestedit.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/*
** The size of the content of the window and the layout of its parts.
*/
#define SW_WIDTH        640.0
#define SW_HEIGHT       520.0
#define SW_MARGIN       24.0
#define SW_CHART_HEIGHT 176.0
#define SW_NTILE        4

/*
** Margins around the plot area of a chart: room for the axis labels on
** the left and bottom and for the title on top.
*/
#define CHART_LEFT      52.0
#define CHART_RIGHT     4.0
#define CHART_TOP       34.0
#define CHART_BOTTOM    20.0

/*
** What a chart measures.
*/
#define CHART_WORDS  0
#define CHART_TIME   1

/*
** The name under which the selected period is saved in the user
** defaults.
*/
#define SW_PERIOD_KEY CFSTR("StatsPeriod")

/*
** UTF-8 for a proper minus sign and for an em-dash.
*/
#define MINUS  "\xe2\x88\x92"
#define EMDASH "\xe2\x80\x94"

/*
** Each chart is described by an instance of the following structure.
*/
typedef struct Chart Chart;
struct Chart {
  int eUnit;                      /* CHART_WORDS or CHART_TIME */
  const StatsReport *pReport;     /* The data to display */
};

/*
** All global state of this module.
*/
static struct StatsWin {
  id pWin;                        /* The window, or NULL if never shown */
  id pPeriod;                     /* The Week/Month/Year selector */
  id aValue[SW_NTILE];            /* The headline numbers */
  id aChart[2];                   /* The two charts */
  int ePeriod;                    /* The period currently selected */
  StatsReport report;             /* The report currently displayed */
} sw;

/*
** The instance variable of BeChartView that holds the Chart pointer.
*/
static Ivar chartIvar;
#define chartOf(pView) \
  (*(Chart**)((char*)(pView) + ivar_getOffset(chartIvar)))

/*
** Return the value of the i-th bar of chart p.  The words chart shows
** net words written.  A day on which more was deleted than was written
** has no bar, since a bar cannot have a negative height.
*/
static double chartValue(const Chart *p, int i){
  const StatsBucket *pB = &p->pReport->a[i];
  if( p->eUnit==CHART_TIME ) return (double)pB->nSec;
  return pB->nWord>0 ? (double)pB->nWord : 0.0;
}

/*
** Render value r in the units of chart p.  Ex: "1,250" or "1h 05m"
*/
static char *chartFormat(const Chart *p, double r, char *zBuf){
  if( p->eUnit==CHART_TIME ) return beFmtDuration((i64)r, zBuf);
  return beFmtInt((i64)r, zBuf);
}

/*
** Return a round number, no less than rMax, for the top of the value
** axis.  Half of the result is also a round number, so that a grid
** line can be drawn there.
*/
static double chartTop(double rMax, int eUnit){
  static const double aTime[] = {
    60, 120, 240, 600, 1200, 1800, 2400, 3600, 7200, 10800, 14400, 21600,
    28800, 36000, 43200, 57600, 72000
  };
  double r;                       /* A power of ten */
  int i;                          /* Loop counter */

  if( eUnit==CHART_TIME ){
    for(i=0; i<ArraySize(aTime); i++){
      if( rMax<=aTime[i] ) return aTime[i];
    }
    r = 36000.0;                  /* Ten hours */
  }else{
    r = 10.0;
  }
  for(;;){
    if( rMax<=r ) return r;
    if( rMax<=r*2.0 ) return r*2.0;
    if( rMax<=r*4.0 ) return r*4.0;
    r *= 10.0;
  }
}

/*
** Draw string z using attributes pAttr.  The text is positioned
** relative to point (x,y) according to eAlign.  Return the width of
** the text.
*/
static double chartText(const char *z, double x, double y, int eAlign,
                        id pAttr){
  id zStr = beStr(z);
  NSSize sz = msg1(NSSize, zStr, "sizeWithAttributes:", id, pAttr);
  NSPoint pt;
  if( eAlign==NSTextAlignmentCenter ) x -= sz.width/2.0;
  if( eAlign==NSTextAlignmentRight ) x -= sz.width;
  pt.x = x;
  pt.y = y;
  msg2(void, zStr, "drawAtPoint:withAttributes:", NSPoint, pt, id, pAttr);
  return sz.width;
}

/*
** Fill a bar that stands on the baseline and has a rounded top.
*/
static void chartBar(CGContextRef c, double x, double y, double w, double h){
  double r = w/2.0<4.0 ? w/2.0 : 4.0;      /* Corner radius */
  if( r>h ) r = h;
  CGContextBeginPath(c);
  CGContextMoveToPoint(c, x, y);
  CGContextAddLineToPoint(c, x, y+h-r);
  CGContextAddArcToPoint(c, x, y+h, x+r, y+h, r);
  CGContextAddLineToPoint(c, x+w-r, y+h);
  CGContextAddArcToPoint(c, x+w, y+h, x+w, y+h-r, r);
  CGContextAddLineToPoint(c, x+w, y);
  CGContextClosePath(c);
  CGContextFillPath(c);
}

/*
** Implementation of -[BeChartView drawRect:].
*/
static void chartDraw(id self, SEL NotUsed, NSRect NotUsed2){
  Chart *p = chartOf(self);
  const StatsReport *pRep = p->pReport;
  NSRect b = msgRect(self, "bounds");
  CGContextRef c;                 /* The drawing context */
  id pAxisAttr;                   /* Text attributes of axis labels */
  id pValueAttr;                  /* Text attributes of values on bars */
  id pTitleAttr;                  /* Text attributes of the title */
  double x0 = CHART_LEFT;         /* Left edge of the plot area */
  double y0 = CHART_BOTTOM;       /* Bottom edge of the plot area */
  double w = b.size.width - CHART_LEFT - CHART_RIGHT;
  double h = b.size.height - CHART_TOP - CHART_BOTTOM;
  double rSlot = w/pRep->nBucket; /* Width allotted to each bar */
  double rBar;                    /* Width of each bar */
  double rMax = 0.0;              /* Largest value */
  double rTop;                    /* Value at the top of the axis */
  double xPrev;                   /* Left edge of previous axis label */
  int iMax = 0;                   /* Index of the largest value */
  int i;                          /* Loop counter */
  char zBuf[32];                  /* A number rendered as text */
  char zTitle[64];                /* The title of the chart */

  UNUSED_PARAMETER(NotUsed);
  UNUSED_PARAMETER(NotUsed2);
  c = msg0(CGContextRef,
           msg0(id, cls("NSGraphicsContext"), "currentContext"), "CGContext");
  pAxisAttr = beDict(NSFontAttributeName, beFont(10.0, NSFontWeightRegular, 1),
                     NSForegroundColorAttributeName,
                     beColor("secondaryLabelColor"));
  pValueAttr = beDict(NSFontAttributeName,
                      beFont(10.0, NSFontWeightRegular, 1),
                      NSForegroundColorAttributeName, beColor("labelColor"));
  pTitleAttr = beDict(NSFontAttributeName,
                      beFont(12.0, NSFontWeightSemibold, 0),
                      NSForegroundColorAttributeName, beColor("labelColor"));

  for(i=0; i<pRep->nBucket; i++){
    double r = chartValue(p, i);
    if( r>rMax ){
      rMax = r;
      iMax = i;
    }
  }
  rTop = chartTop(rMax, p->eUnit);

  /* The title */
  snprintf(zTitle, sizeof(zTitle), "%s per %s",
           p->eUnit==CHART_TIME ? "Time spent" : "Words written",
           pRep->ePeriod==BE_YEAR ? "month" : "day");
  chartText(zTitle, 0.0, b.size.height-16.0, NSTextAlignmentLeft, pTitleAttr);

  /* Grid lines at the half-way point and at the top, with their values */
  CGContextSetFillColorWithColor(c,
      msg0(CGColorRef, beColor("separatorColor"), "CGColor"));
  for(i=1; i<=2; i++){
    double y = y0 + h*i/2.0;
    CGContextFillRect(c, CGRectMake(x0, y-0.5, w, 1.0));
    chartText(chartFormat(p, rTop*i/2.0, zBuf), x0-8.0, y-6.0,
              NSTextAlignmentRight, pAxisAttr);
  }

  /* The bars */
  rBar = rSlot*0.62;
  if( rBar>26.0 ) rBar = 26.0;
  if( rBar>rSlot-2.0 ) rBar = rSlot-2.0;
  CGContextSetFillColorWithColor(c,
      msg0(CGColorRef, beColor("systemBlueColor"), "CGColor"));
  for(i=0; i<pRep->nBucket; i++){
    double r = chartValue(p, i);
    double rHeight = h*r/rTop;
    if( r<=0.0 ) continue;
    if( rHeight<2.0 ) rHeight = 2.0;
    chartBar(c, x0 + rSlot*(i+0.5) - rBar/2.0, y0, rBar, rHeight);
  }

  /* The values of the bars.  With only a few bars there is room to
  ** print the value of each one.  Otherwise just the largest is
  ** labeled; the rest can be read from the grid or from the tooltips.
  ** This is a separate pass because drawing text changes the fill
  ** color of the context. */
  for(i=0; i<pRep->nBucket; i++){
    double r = chartValue(p, i);
    double rHeight = h*r/rTop;
    if( r<=0.0 ) continue;
    if( rHeight<2.0 ) rHeight = 2.0;
    if( pRep->nBucket<=7 || i==iMax ){
      chartText(chartFormat(p, r, zBuf), x0 + rSlot*(i+0.5),
                y0+rHeight+3.0, NSTextAlignmentCenter, pValueAttr);
    }
  }

  /* The baseline, drawn after the bars so that it lies on top */
  CGContextSetFillColorWithColor(c,
      msg0(CGColorRef, beColor("tertiaryLabelColor"), "CGColor"));
  CGContextFillRect(c, CGRectMake(x0, y0-0.5, w, 1.0));

  /* Labels along the bottom.  Work from right to left so that the most
  ** recent bar always gets a label, and skip any label that would run
  ** into the one before. */
  xPrev = x0 + w + 100.0;
  for(i=pRep->nBucket-1; i>=0; i--){
    const char *z = pRep->a[i].zLabel;
    double xMid = x0 + rSlot*(i+0.5);
    NSSize sz = msg1(NSSize, beStr(z), "sizeWithAttributes:", id, pAxisAttr);
    if( xMid+sz.width/2.0 > b.size.width ){
      xMid = b.size.width - sz.width/2.0;
    }
    if( xMid+sz.width/2.0 > xPrev-10.0 ) continue;
    if( xMid-sz.width/2.0 < 0.0 ) continue;
    chartText(z, xMid, 3.0, NSTextAlignmentCenter, pAxisAttr);
    xPrev = xMid - sz.width/2.0;
  }

  if( rMax<=0.0 ){
    chartText("Nothing recorded", x0+w/2.0, y0+h/2.0-18.0,
              NSTextAlignmentCenter, pAxisAttr);
  }
}

/*
** Implementation of -[BeChartView view:stringForToolTip:point:userData:].
** Return the text of the tooltip for a bar.  The index of the bar was
** supplied as the user data when the tooltip was registered.
*/
static id chartToolTip(id self, SEL NotUsed, id pView, long iTag, NSPoint pt,
                       void *pData){
  Chart *p = chartOf(self);
  int i = (int)(long)pData;       /* Which bar */
  const StatsBucket *pB;          /* The bucket for that bar */
  char zVal[32];                  /* The value */
  char zTip[100];                 /* The complete tooltip */

  UNUSED_PARAMETER(NotUsed);
  UNUSED_PARAMETER(pView);
  UNUSED_PARAMETER(iTag);
  UNUSED_PARAMETER(pt);
  if( i<0 || i>=p->pReport->nBucket ) return S("");
  pB = &p->pReport->a[i];
  if( p->eUnit==CHART_TIME ){
    snprintf(zTip, sizeof(zTip), "%s: %s", pB->zLong,
             beFmtDuration(pB->nSec, zVal));
  }else{
    snprintf(zTip, sizeof(zTip), "%s: %s %s", pB->zLong,
             beFmtInt(pB->nWord, zVal), pB->nWord==1 ? "word" : "words");
  }
  return beStr(zTip);
}

/*
** Register one tooltip for each bar of chart view pView.  The area
** that triggers a tooltip is the full height of the plot and the full
** width allotted to the bar, which is easier to hit than the bar is.
*/
static void chartSetToolTips(id pView){
  Chart *p = chartOf(pView);
  NSRect b = msgRect(pView, "bounds");
  double w = b.size.width - CHART_LEFT - CHART_RIGHT;
  double h = b.size.height - CHART_TOP - CHART_BOTTOM;
  double rSlot = w/p->pReport->nBucket;
  int i;                          /* Loop counter */

  msg0(void, pView, "removeAllToolTips");
  for(i=0; i<p->pReport->nBucket; i++){
    NSRect r = CGRectMake(CHART_LEFT + rSlot*i, CHART_BOTTOM, rSlot, h);
    msg3(long, pView, "addToolTipRect:owner:userData:",
         NSRect, r, id, pView, void*, (void*)(long)i);
  }
}

/*
** Implementation of -[BeChartView dealloc].
*/
static void chartDealloc(id self, SEL cmd){
  struct objc_super sSuper;       /* For sending a message to super */
  free(chartOf(self));
  chartOf(self) = 0;
  sSuper.receiver = self;
  sSuper.super_class = objc_getClass("NSView");
  ((void(*)(struct objc_super*,SEL))objc_msgSendSuper)(&sSuper, cmd);
}

/*
** Create a chart view with frame r that displays the current report in
** units of eUnit.  The caller owns the result.
*/
static id chartNew(NSRect r, int eUnit){
  id pView;                       /* The new view */
  Chart *p;                       /* Its chart */

  p = calloc(1, sizeof(*p));
  if( p==0 ) return 0;
  p->eUnit = eUnit;
  p->pReport = &sw.report;
  pView = msg0(id, cls("BeChartView"), "alloc");
  pView = msg1(id, pView, "initWithFrame:", NSRect, r);
  if( pView==0 ){
    free(p);
    return 0;
  }
  chartOf(pView) = p;
  return pView;
}

/*
** Create the BeChartView class.
*/
void beStatsWinRegister(void){
  Class pClass;                   /* The new class */
  pClass = objc_allocateClassPair(objc_getClass("NSView"), "BeChartView", 0);
  class_addIvar(pClass, "pChart", sizeof(Chart*), 3, "^v");
  class_addMethod(pClass, sel_registerName("drawRect:"),
                  (IMP)chartDraw, "v@:" BE_RECT);
  class_addMethod(pClass,
                  sel_registerName("view:stringForToolTip:point:userData:"),
                  (IMP)chartToolTip, "@@:@q" BE_POINT "^v");
  class_addMethod(pClass, sel_registerName("dealloc"),
                  (IMP)chartDealloc, "v@:");
  objc_registerClassPair(pClass);
  chartIvar = class_getInstanceVariable(pClass, "pChart");
}

/*
** Build the statistics window.
*/
static void statsWinBuild(void){
  static const char *azCaption[SW_NTILE] = {
    "Words written", "Time spent", "Days active", "Words per active day"
  };
  id pRoot;                       /* The content view of the window */
  id pLabels;                     /* Titles of the period selector */
  id aTitle[3];                   /* The same, as a C array */
  double rTile;                   /* Width of each headline number */
  double y;                       /* Top of the next thing to be added */
  int i;                          /* Loop counter */
  CFPropertyListRef pSaved;       /* The saved period */

  sw.pWin = msg0(id, cls("NSWindow"), "alloc");
  sw.pWin = msg4(id, sw.pWin, "initWithContentRect:styleMask:backing:defer:",
                 NSRect, CGRectMake(0.0, 0.0, SW_WIDTH, SW_HEIGHT),
                 unsigned long, NSWindowStyleMaskTitled
                                | NSWindowStyleMaskClosable
                                | NSWindowStyleMaskMiniaturizable,
                 unsigned long, NSBackingStoreBuffered,
                 BOOL, YES);
  msg1(void, sw.pWin, "setReleasedWhenClosed:", BOOL, NO);
  msg1(void, sw.pWin, "setTitle:", id, S("Writing Statistics"));
  msg1(void, sw.pWin, "setDelegate:", id, beDelegate);
  msg0(void, sw.pWin, "center");
  msg1(BOOL, sw.pWin, "setFrameAutosaveName:", id, S("BeStatistics"));
  pRoot = msg0(id, sw.pWin, "contentView");

  /* The period selector, centered at the top */
  pSaved = CFPreferencesCopyAppValue(SW_PERIOD_KEY,
                                     kCFPreferencesCurrentApplication);
  if( pSaved ){
    if( CFGetTypeID(pSaved)==CFNumberGetTypeID() ){
      CFNumberGetValue((CFNumberRef)pSaved, kCFNumberIntType, &sw.ePeriod);
    }
    CFRelease(pSaved);
  }
  if( sw.ePeriod<BE_WEEK || sw.ePeriod>BE_YEAR ) sw.ePeriod = BE_WEEK;
  aTitle[0] = S("Week");
  aTitle[1] = S("Month");
  aTitle[2] = S("Year");
  pLabels = msg2(id, cls("NSArray"), "arrayWithObjects:count:",
                 id*, aTitle, unsigned long, 3);
  sw.pPeriod = msg4(id, cls("NSSegmentedControl"),
                    "segmentedControlWithLabels:trackingMode:target:action:",
                    id, pLabels, unsigned long, 0, id, beDelegate,
                    SEL, sel_registerName("statsPeriod:"));
  y = SW_HEIGHT - 18.0;
  msg1(void, sw.pPeriod, "setFrame:", NSRect,
       CGRectMake((SW_WIDTH-240.0)/2.0, y-24.0, 240.0, 24.0));
  msg1(void, sw.pPeriod, "setSelectedSegment:", long, sw.ePeriod);
  msg1(void, pRoot, "addSubview:", id, sw.pPeriod);
  y -= 24.0 + 22.0;

  /* The headline numbers, each with a caption underneath */
  rTile = (SW_WIDTH - 2.0*SW_MARGIN)/SW_NTILE;
  for(i=0; i<SW_NTILE; i++){
    double x = SW_MARGIN + rTile*i;
    id pCaption;
    sw.aValue[i] = beLabel(CGRectMake(x, y-30.0, rTile-8.0, 30.0), 24.0,
                           NSFontWeightSemibold, "labelColor", 0);
    msg1(void, pRoot, "addSubview:", id, sw.aValue[i]);
    pCaption = beLabel(CGRectMake(x, y-48.0, rTile-8.0, 16.0), 11.0,
                       NSFontWeightRegular, "secondaryLabelColor", 0);
    beSetText(pCaption, azCaption[i]);
    msg1(void, pRoot, "addSubview:", id, pCaption);
  }

  /* The two charts, at the bottom */
  for(i=0; i<2; i++){
    NSRect r = CGRectMake(SW_MARGIN,
                          SW_MARGIN + (1-i)*(SW_CHART_HEIGHT + 20.0),
                          SW_WIDTH - 2.0*SW_MARGIN, SW_CHART_HEIGHT);
    sw.aChart[i] = chartNew(r, i==0 ? CHART_WORDS : CHART_TIME);
    msg1(void, pRoot, "addSubview:", id, sw.aChart[i]);
    msg0(void, sw.aChart[i], "release");
  }
}

/*
** Reload the statistics from the database and redisplay them.  This is
** a no-op if the statistics window is not on screen.
*/
void beStatsWinRefresh(void){
  StatsReport *p = &sw.report;
  char zBuf[32];                  /* A number rendered as text */
  char zText[64];                 /* The text of a headline number */
  int i;                          /* Loop counter */

  if( sw.pWin==0 || !msg0(BOOL, sw.pWin, "isVisible") ) return;
  beStatsReport(sw.ePeriod, p);

  if( p->nWord<0 ){
    snprintf(zText, sizeof(zText), MINUS "%s", beFmtInt(-p->nWord, zBuf));
  }else{
    beFmtInt(p->nWord, zText);
  }
  beSetText(sw.aValue[0], zText);
  beSetText(sw.aValue[1], beFmtDuration(p->nSec, zText));
  snprintf(zText, sizeof(zText), "%d of %d", p->nActive, p->nDay);
  beSetText(sw.aValue[2], zText);
  if( p->nActive>0 && p->nWord>0 ){
    beFmtInt((p->nWord + p->nActive/2)/p->nActive, zText);
  }else{
    snprintf(zText, sizeof(zText), EMDASH);
  }
  beSetText(sw.aValue[3], zText);

  for(i=0; i<2; i++){
    chartSetToolTips(sw.aChart[i]);
    msg1(void, sw.aChart[i], "setNeedsDisplay:", BOOL, YES);
  }
}

/*
** Change the period that the statistics window displays to ePeriod,
** which must be one of BE_WEEK, BE_MONTH, or BE_YEAR.  The choice is
** remembered for next time.
*/
void beStatsWinSetPeriod(int ePeriod){
  CFNumberRef pNum;               /* The period as a CFNumber */
  if( ePeriod<BE_WEEK || ePeriod>BE_YEAR ) return;
  sw.ePeriod = ePeriod;
  msg1(void, sw.pPeriod, "setSelectedSegment:", long, ePeriod);
  pNum = CFNumberCreate(0, kCFNumberIntType, &ePeriod);
  if( pNum ){
    CFPreferencesSetAppValue(SW_PERIOD_KEY, pNum,
                             kCFPreferencesCurrentApplication);
    CFPreferencesAppSynchronize(kCFPreferencesCurrentApplication);
    CFRelease(pNum);
  }
  beStatsWinRefresh();
}

/*
** Show the statistics window, creating it first if this is the first
** time it has been shown.
*/
void beStatsWinShow(void){
  if( sw.pWin==0 ) statsWinBuild();
  msg1(void, sw.pWin, "makeKeyAndOrderFront:", id, (id)0);
  beStatsWinRefresh();
}

/*
** Test interface: return the statistics window.
*/
#ifdef BE_TEST
void *beTestStatsWindow(void){
  return sw.pWin;
}
#endif
