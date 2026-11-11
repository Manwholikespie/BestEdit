/*
** 2026 November 11
**
** This file implements the statistics window.
**
** The window shows how much was written, and how much time was spent
** writing, over the past week, month, or year.  The period is chosen
** with a control in the toolbar.  Below the toolbar are two panels, one
** for words and one for time.  Words and time get separate panels
** because they are measured in different units and so cannot share an
** axis.  Each panel shows:
**
**    *  The total for the period.
**    *  How that compares with the period before.
**    *  A sentence about the average day.
**    *  A bar chart with one bar per day, or per month for a year.
**
** While the mouse is over a bar, the other bars fade and the total
** gives way to the value and the date of that one bar.
**
** Each panel is an instance of BeChartView, a subclass of NSView that
** is created here at run time, and each panel draws all of its own
** content.  The only instance variable of BeChartView is a pointer to
** a Chart structure.  Shapes are drawn with CoreGraphics, which is a C
** API.
**
** The toolbar needs a delegate, and the period control needs a target.
** Both are the single instance of BeStatsController, another class
** created here.
*/
#include "cocoa.h"
#include "bestedit.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/*
** The size of the content of the window and the layout of its parts.
*/
#define SW_WIDTH        600.0     /* Width of the content */
#define SW_MARGIN       28.0      /* Space to the left and right */
#define SW_PANEL        250.0     /* Height of each panel */
#define SW_GAP          30.0      /* Space between the panels */
#define SW_TOP          14.0      /* Space above the first panel */
#define SW_BOTTOM       22.0      /* Space below the second panel */
#define SW_HEIGHT       (2*SW_PANEL + SW_GAP + SW_TOP + SW_BOTTOM)

/*
** The layout of a panel.  The plot is the rectangle that the bars
** stand in.  To its right is a gutter for the labels of the grid
** lines, and below it is a strip for the labels of the bars.
*/
#define CHART_GUTTER    46.0      /* Width of the gutter */
#define CHART_BOTTOM    22.0      /* Height of the strip of labels */
#define CHART_HEAD      92.0      /* Height of the text above the plot */

/*
** What a chart measures.
*/
#define CHART_WORDS  0
#define CHART_TIME   1

/*
** The name under which the selected period is saved in the user
** defaults, and the identifier of the period control in the toolbar.
*/
#define SW_PERIOD_KEY   CFSTR("StatsPeriod")
#define SW_PERIOD_ITEM  S("BePeriod")

/*
** UTF-8 for a proper minus sign and for arrows.
*/
#define MINUS     "\xe2\x88\x92"
#define ARROW_UP  "\xe2\x86\x91"
#define ARROW_DN  "\xe2\x86\x93"

/*
** Each panel is described by an instance of the following structure.
*/
typedef struct Chart Chart;
struct Chart {
  int eUnit;                      /* CHART_WORDS or CHART_TIME */
  int iHover;                     /* Bar under the mouse, or -1 for none */
  const StatsReport *pReport;     /* The data to display */
};

/*
** All global state of this module.
*/
static struct StatsWin {
  id pWin;                        /* The window, or NULL if never shown */
  id pCtrl;                       /* The BeStatsController */
  id pPeriod;                     /* The Week/Month/Year control */
  id aChart[2];                   /* The two panels */
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
** Render n, a count of words or of seconds according to eUnit, as
** text.  A negative count of words gets a proper minus sign.
*/
static char *chartFormat(int eUnit, i64 n, char *zBuf){
  char zNum[32];
  if( eUnit==CHART_TIME ) return beFmtDuration(n, zBuf);
  if( n<0 ){
    snprintf(zBuf, 40, MINUS "%s", beFmtInt(-n, zNum));
    return zBuf;
  }
  return beFmtInt(n, zBuf);
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
** Return the font for the large number at the top of a panel: the
** system font in its rounded design, which is what the system itself
** uses for the numbers in its own charts.
*/
static id chartBigFont(void){
  id pFont = beFont(30.0, NSFontWeightSemibold, 0);
  id pDesc = msg1(id, msg0(id, pFont, "fontDescriptor"),
                  "fontDescriptorWithDesign:",
                  id, NSFontDescriptorSystemDesignRounded);
  id pRound = pDesc ? msg2(id, cls("NSFont"), "fontWithDescriptor:size:",
                           id, pDesc, double, 30.0) : (id)0;
  return pRound ? pRound : pFont;
}

/*
** Return a dictionary of text attributes: font pFont in the system
** color named zColor.
*/
static id chartAttr(id pFont, const char *zColor){
  return beDict(NSFontAttributeName, pFont,
                NSForegroundColorAttributeName, beColor(zColor));
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
** Stroke a horizontal line one point thick from x0 to x1 at height y
** in the system color named zColor.  The line is dashed if bDash.
*/
static void chartLine(CGContextRef c, double x0, double x1, double y,
                      const char *zColor, int bDash){
  static const CGFloat aDash[2] = { 2.0, 3.0 };
  CGContextSetStrokeColorWithColor(c,
      msg0(CGColorRef, beColor(zColor), "CGColor"));
  CGContextSetLineWidth(c, 1.0);
  CGContextSetLineDash(c, 0.0, aDash, bDash ? 2 : 0);
  CGContextBeginPath(c);
  CGContextMoveToPoint(c, x0, y);
  CGContextAddLineToPoint(c, x1, y);
  CGContextStrokePath(c);
  CGContextSetLineDash(c, 0.0, 0, 0);
}

/*
** Write into zBuf a phrase that compares nNow, the total for the
** current period, against nPrev, the total for the period before.
** Ex: "(up arrow) 12% from last week".  The result is an empty string
** if there is nothing to compare against.
*/
static void chartCompare(i64 nNow, i64 nPrev, int ePeriod, char *zBuf,
                         int nBuf){
  static const char *azSince[] = {
    "last week", "the 30 days before", "the 12 months before"
  };
  char zPct[32];                  /* The percentage */
  i64 nPct;                       /* Change, in percent */

  zBuf[0] = 0;
  if( nPrev<=0 || nNow<0 ) return;
  nPct = ((nNow - nPrev)*100 + (nNow>=nPrev ? nPrev/2 : -(nPrev/2)))/nPrev;
  if( nPct==0 ){
    snprintf(zBuf, nBuf, "Same as %s", azSince[ePeriod]);
  }else{
    snprintf(zBuf, nBuf, "%s %s%% from %s", nPct>0 ? ARROW_UP : ARROW_DN,
             beFmtInt(nPct>0 ? nPct : -nPct, zPct), azSince[ePeriod]);
  }
}

/*
** Draw the text at the top of the panel of chart p: a caption, a large
** number, a comparison with the previous period, and a sentence about
** the average day.  b is the bounds of the panel.
**
** If the mouse is over a bar, the caption is the date of that bar, the
** number is its value, and the rest is omitted.
*/
static void chartHeader(Chart *p, NSRect b){
  const StatsReport *pRep = p->pReport;
  id pBig = chartBigFont();       /* Font of the large number */
  id pUnit = beFont(15.0, NSFontWeightMedium, 0);
  id pSmall = beFont(12.0, NSFontWeightRegular, 1);
  double yTop = b.size.height;    /* Top of the panel */
  double yBig = yTop - 58.0;      /* Where the large number goes */
  double x;                       /* Right edge of the large number */
  i64 n;                          /* The number to show */
  const char *zCaption;           /* The caption */
  char zNum[40];                  /* The number as text */
  char zLine[160];                /* A line of small text */
  char zAvg[40];                  /* An average as text */

  if( p->iHover>=0 ){
    const StatsBucket *pB = &pRep->a[p->iHover];
    zCaption = pB->zLong;
    n = p->eUnit==CHART_TIME ? pB->nSec : pB->nWord;
  }else{
    zCaption = p->eUnit==CHART_TIME ? "Time writing" : "Words written";
    n = p->eUnit==CHART_TIME ? pRep->nSec : pRep->nWord;
  }
  chartText(zCaption, 0.0, yTop-18.0, NSTextAlignmentLeft,
            chartAttr(beFont(13.0, NSFontWeightMedium, 0),
                      "secondaryLabelColor"));
  x = chartText(chartFormat(p->eUnit, n, zNum), 0.0, yBig,
                NSTextAlignmentLeft, chartAttr(pBig, "labelColor"));
  if( p->eUnit==CHART_WORDS ){
    /* The unit follows the number in smaller type on the same baseline */
    double rDrop = msg0(double, pUnit, "descender")
                 - msg0(double, pBig, "descender");
    chartText(n==1 ? "word" : "words", x+6.0, yBig+rDrop,
              NSTextAlignmentLeft, chartAttr(pUnit, "secondaryLabelColor"));
  }
  if( p->iHover>=0 ) return;

  if( p->eUnit==CHART_TIME ){
    chartCompare(pRep->nSec, pRep->nPrevSec, pRep->ePeriod, zLine,
                 sizeof(zLine));
  }else{
    chartCompare(pRep->nWord, pRep->nPrevWord, pRep->ePeriod, zLine,
                 sizeof(zLine));
  }
  chartText(zLine, b.size.width, yBig+8.0, NSTextAlignmentRight,
            chartAttr(pSmall, "secondaryLabelColor"));

  zLine[0] = 0;
  if( pRep->nActive>0 && p->eUnit==CHART_WORDS ){
    if( pRep->nWord>0 ){
      beFmtInt((pRep->nWord + pRep->nActive/2)/pRep->nActive, zAvg);
      snprintf(zLine, sizeof(zLine),
               "You wrote on %d of %d days, averaging %s words.",
               pRep->nActive, pRep->nDay, zAvg);
    }else{
      snprintf(zLine, sizeof(zLine), "You wrote on %d of %d days.",
               pRep->nActive, pRep->nDay);
    }
  }else if( pRep->nActive>0 && pRep->nSec>0 ){
    snprintf(zLine, sizeof(zLine), "About %s on each day you wrote.",
             beFmtDuration(pRep->nSec/pRep->nActive, zAvg));
  }
  chartText(zLine, 0.0, yBig-20.0, NSTextAlignmentLeft,
            chartAttr(pSmall, "secondaryLabelColor"));
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
  id pAvgAttr;                    /* Text attributes of the "avg" label */
  double x0 = 0.0;                /* Left edge of the plot */
  double y0 = CHART_BOTTOM;       /* Bottom edge of the plot */
  double w = b.size.width - CHART_GUTTER;
  double h = b.size.height - CHART_HEAD - CHART_BOTTOM;
  double rSlot = w/pRep->nBucket; /* Width allotted to each bar */
  double rBar;                    /* Width of each bar */
  double rMax = 0.0;              /* Largest value */
  double rSum = 0.0;              /* Sum of the values */
  double rTop;                    /* Value at the top of the axis */
  double yAvg = -100.0;           /* Height of the average line */
  double xPrev;                   /* Left edge of previous bar label */
  int nBar = 0;                   /* Number of bars that are not zero */
  int i;                          /* Loop counter */
  char zBuf[40];                  /* A number rendered as text */

  UNUSED_PARAMETER(NotUsed);
  UNUSED_PARAMETER(NotUsed2);
  c = msg0(CGContextRef,
           msg0(id, cls("NSGraphicsContext"), "currentContext"), "CGContext");
  pAxisAttr = chartAttr(beFont(10.0, NSFontWeightRegular, 1),
                        "tertiaryLabelColor");
  pAvgAttr = chartAttr(beFont(10.0, NSFontWeightMedium, 1),
                       "controlAccentColor");
  chartHeader(p, b);

  for(i=0; i<pRep->nBucket; i++){
    double r = chartValue(p, i);
    if( r>rMax ) rMax = r;
    if( r>0.0 ){
      rSum += r;
      nBar++;
    }
  }
  if( rMax<=0.0 ){
    /* Nothing to chart.  Say what will make something appear. */
    chartLine(c, x0, x0+w, y0+0.5, "separatorColor", 0);
    chartText(p->eUnit==CHART_TIME
                ? "Time appears here once the timer has run."
                : "Words appear here as you write.",
              x0+w/2.0, y0+h/2.0-8.0, NSTextAlignmentCenter,
              chartAttr(beFont(12.0, NSFontWeightRegular, 0),
                        "tertiaryLabelColor"));
    return;
  }
  rTop = chartTop(rMax, p->eUnit);

  /* A dashed line marks the average of the days that have a bar.  It
  ** is labeled in the gutter. */
  if( nBar>=2 ){
    yAvg = y0 + h*(rSum/nBar)/rTop;
  }

  /* Grid lines at the half-way point and at the top, labeled in the
  ** gutter.  A label that would collide with "avg" is left out. */
  for(i=1; i<=2; i++){
    double y = y0 + h*i/2.0;
    chartLine(c, x0, x0+w, y+0.5, "separatorColor", 1);
    if( y-yAvg<12.0 && yAvg-y<12.0 ) continue;
    chartText(chartFormat(p->eUnit, (i64)(rTop*i/2.0), zBuf), x0+w+8.0,
              y-6.0, NSTextAlignmentLeft, pAxisAttr);
  }

  /* The bars.  If the mouse is over one, the others fade. */
  rBar = rSlot*0.62;
  if( rBar>28.0 ) rBar = 28.0;
  if( rBar>rSlot-2.0 ) rBar = rSlot-2.0;
  CGContextSetFillColorWithColor(c,
      msg0(CGColorRef, beColor("controlAccentColor"), "CGColor"));
  for(i=0; i<pRep->nBucket; i++){
    double r = chartValue(p, i);
    double rHeight = h*r/rTop;
    if( r<=0.0 ) continue;
    if( rHeight<2.0 ) rHeight = 2.0;
    CGContextSetAlpha(c, p->iHover>=0 && p->iHover!=i ? 0.3 : 1.0);
    chartBar(c, x0 + rSlot*(i+0.5) - rBar/2.0, y0, rBar, rHeight);
  }
  CGContextSetAlpha(c, 1.0);

  /* The baseline and the average, drawn over the bars */
  chartLine(c, x0, x0+w, y0+0.5, "separatorColor", 0);
  if( yAvg>0.0 ){
    chartLine(c, x0, x0+w, yAvg+0.5, "controlAccentColor", 1);
    chartText("avg", x0+w+8.0, yAvg-6.0, NSTextAlignmentLeft, pAvgAttr);
  }

  /* Labels under the bars.  Work from right to left so that the most
  ** recent bar always gets a label, and skip any label that would run
  ** into the one before. */
  xPrev = x0 + w + 100.0;
  for(i=pRep->nBucket-1; i>=0; i--){
    const char *z = pRep->a[i].zLabel;
    double xMid = x0 + rSlot*(i+0.5);
    NSSize sz = msg1(NSSize, beStr(z), "sizeWithAttributes:", id, pAxisAttr);
    if( xMid+sz.width/2.0 > x0+w ) xMid = x0 + w - sz.width/2.0;
    if( xMid+sz.width/2.0 > xPrev-12.0 ) continue;
    if( xMid-sz.width/2.0 < 0.0 ) continue;
    chartText(z, xMid, 3.0, NSTextAlignmentCenter, pAxisAttr);
    xPrev = xMid - sz.width/2.0;
  }
}

/*
** The mouse is at point pt, in the coordinates of chart view pView.
** Work out which bar it is over, if any, and redraw if that changed.
*/
static void chartHoverAt(id pView, NSPoint pt){
  Chart *p = chartOf(pView);
  NSRect b = msgRect(pView, "bounds");
  double w = b.size.width - CHART_GUTTER;
  double yTop = b.size.height - CHART_HEAD + 6.0;
  int iHover = -1;                /* The bar under the mouse */

  if( pt.x>=0.0 && pt.x<w && pt.y>=0.0 && pt.y<yTop ){
    iHover = (int)(pt.x*p->pReport->nBucket/w);
    if( iHover>=p->pReport->nBucket ) iHover = p->pReport->nBucket - 1;
  }
  if( iHover!=p->iHover ){
    p->iHover = iHover;
    msg1(void, pView, "setNeedsDisplay:", BOOL, YES);
  }
}

/*
** Implementation of -[BeChartView mouseMoved:].
*/
static void chartMouseMoved(id self, SEL NotUsed, id pEvent){
  NSPoint pt = msg0(NSPoint, pEvent, "locationInWindow");
  UNUSED_PARAMETER(NotUsed);
  pt = msg2(NSPoint, self, "convertPoint:fromView:", NSPoint, pt, id, (id)0);
  chartHoverAt(self, pt);
}

/*
** Implementation of -[BeChartView mouseExited:].
*/
static void chartMouseExited(id self, SEL NotUsed, id pEvent){
  NSPoint pt;
  UNUSED_PARAMETER(NotUsed);
  UNUSED_PARAMETER(pEvent);
  pt.x = -1.0;
  pt.y = -1.0;
  chartHoverAt(self, pt);
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
** units of eUnit.  The view asks to be told when the mouse moves over
** it.  The caller owns the result.
*/
static id chartNew(NSRect r, int eUnit){
  id pView;                       /* The new view */
  id pArea;                       /* Its mouse tracking area */
  Chart *p;                       /* Its chart */

  p = calloc(1, sizeof(*p));
  if( p==0 ) return 0;
  p->eUnit = eUnit;
  p->iHover = -1;
  p->pReport = &sw.report;
  pView = msg0(id, cls("BeChartView"), "alloc");
  pView = msg1(id, pView, "initWithFrame:", NSRect, r);
  if( pView==0 ){
    free(p);
    return 0;
  }
  chartOf(pView) = p;
  pArea = msg0(id, cls("NSTrackingArea"), "alloc");
  pArea = msg4(id, pArea, "initWithRect:options:owner:userInfo:",
               NSRect, CGRectZero,
               unsigned long, NSTrackingMouseEnteredAndExited
                              | NSTrackingMouseMoved
                              | NSTrackingActiveAlways
                              | NSTrackingInVisibleRect,
               id, pView, id, (id)0);
  msg1(void, pView, "addTrackingArea:", id, pArea);
  msg0(void, pArea, "release");
  return pView;
}

/*
** Implementation of -[BeStatsController statsPeriod:], the action of
** the Week/Month/Year control.
*/
static void ctrlPeriod(id self, SEL NotUsed, id pSender){
  UNUSED_PARAMETER(self);
  UNUSED_PARAMETER(NotUsed);
  beStatsWinSetPeriod((int)msg0(long, pSender, "selectedSegment"));
}

/*
** Implementation of -[BeStatsController windowDidBecomeKey:].  Bring
** the statistics up to date whenever the window comes to the front.
*/
static void ctrlDidBecomeKey(id self, SEL NotUsed, id pNote){
  UNUSED_PARAMETER(self);
  UNUSED_PARAMETER(NotUsed);
  UNUSED_PARAMETER(pNote);
  beStatsWinRefresh();
}

/*
** Implementation of the three methods of the toolbar delegate.  The
** toolbar holds a single item, which is the period control.
*/
static id ctrlToolbarItem(id self, SEL NotUsed, id pToolbar, id zIdent,
                          BOOL bInsert){
  id pItem;
  UNUSED_PARAMETER(self);
  UNUSED_PARAMETER(NotUsed);
  UNUSED_PARAMETER(pToolbar);
  UNUSED_PARAMETER(bInsert);
  pItem = msg0(id, cls("NSToolbarItem"), "alloc");
  pItem = msg1(id, pItem, "initWithItemIdentifier:", id, zIdent);
  msg1(void, pItem, "setView:", id, sw.pPeriod);
  msg1(void, pItem, "setLabel:", id, S("Period"));
  return msg0(id, pItem, "autorelease");
}
static id ctrlToolbarIdents(id self, SEL NotUsed, id pToolbar){
  UNUSED_PARAMETER(self);
  UNUSED_PARAMETER(NotUsed);
  UNUSED_PARAMETER(pToolbar);
  return msg1(id, cls("NSArray"), "arrayWithObject:", id, SW_PERIOD_ITEM);
}

/*
** Create the BeChartView and BeStatsController classes.
*/
void beStatsWinRegister(void){
  Class pClass;                   /* The new class */

  pClass = objc_allocateClassPair(objc_getClass("NSView"), "BeChartView", 0);
  class_addIvar(pClass, "pChart", sizeof(Chart*), 3, "^v");
  class_addMethod(pClass, sel_registerName("drawRect:"),
                  (IMP)chartDraw, "v@:" BE_RECT);
  class_addMethod(pClass, sel_registerName("mouseMoved:"),
                  (IMP)chartMouseMoved, "v@:@");
  class_addMethod(pClass, sel_registerName("mouseExited:"),
                  (IMP)chartMouseExited, "v@:@");
  class_addMethod(pClass, sel_registerName("dealloc"),
                  (IMP)chartDealloc, "v@:");
  objc_registerClassPair(pClass);
  chartIvar = class_getInstanceVariable(pClass, "pChart");

  pClass = objc_allocateClassPair(objc_getClass("NSObject"),
                                  "BeStatsController", 0);
  class_addMethod(pClass, sel_registerName("statsPeriod:"),
                  (IMP)ctrlPeriod, "v@:@");
  class_addMethod(pClass, sel_registerName("windowDidBecomeKey:"),
                  (IMP)ctrlDidBecomeKey, "v@:@");
  class_addMethod(pClass,
      sel_registerName("toolbar:itemForItemIdentifier:"
                       "willBeInsertedIntoToolbar:"),
      (IMP)ctrlToolbarItem, "@@:@@" BE_BOOL);
  class_addMethod(pClass, sel_registerName("toolbarDefaultItemIdentifiers:"),
                  (IMP)ctrlToolbarIdents, "@@:@");
  class_addMethod(pClass, sel_registerName("toolbarAllowedItemIdentifiers:"),
                  (IMP)ctrlToolbarIdents, "@@:@");
  objc_registerClassPair(pClass);
}

/*
** Create the Week/Month/Year control and select the period that was
** showing the last time the window was used.
*/
static void statsWinBuildPeriod(void){
  id aTitle[3];                   /* Titles of the segments */
  CFPropertyListRef pSaved;       /* The saved period */
  int i;                          /* Loop counter */

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
  sw.pPeriod = msg4(id, cls("NSSegmentedControl"),
                    "segmentedControlWithLabels:trackingMode:target:action:",
                    id, msg2(id, cls("NSArray"), "arrayWithObjects:count:",
                             id*, aTitle, unsigned long, 3),
                    unsigned long, 0, id, sw.pCtrl,
                    SEL, sel_registerName("statsPeriod:"));
  msg0(id, sw.pPeriod, "retain");
  for(i=0; i<3; i++){
    msg2(void, sw.pPeriod, "setWidth:forSegment:", double, 72.0, long, i);
  }
  msg1(void, sw.pPeriod, "setSelectedSegment:", long, sw.ePeriod);
}

/*
** Build the statistics window.
*/
static void statsWinBuild(void){
  id pRoot;                       /* The content view of the window */
  id pBar;                        /* The toolbar */
  int i;                          /* Loop counter */

  sw.pCtrl = beNew("BeStatsController");
  statsWinBuildPeriod();

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
  msg1(void, sw.pWin, "setDelegate:", id, sw.pCtrl);

  /* The period control sits in the middle of the toolbar, where the
  ** title of the window would otherwise be. */
  pBar = msg0(id, cls("NSToolbar"), "alloc");
  pBar = msg1(id, pBar, "initWithIdentifier:", id, S("BeStatistics"));
  msg1(void, pBar, "setDelegate:", id, sw.pCtrl);
  msg1(void, pBar, "setDisplayMode:", unsigned long,
       NSToolbarDisplayModeIconOnly);
  msg1(void, pBar, "setAllowsUserCustomization:", BOOL, NO);
  if( msg1(BOOL, pBar, "respondsToSelector:",
           SEL, sel_registerName("setCenteredItemIdentifiers:")) ){
    msg1(void, pBar, "setCenteredItemIdentifiers:", id,
         msg1(id, cls("NSSet"), "setWithObject:", id, SW_PERIOD_ITEM));
  }else{
    msg1(void, pBar, "setCenteredItemIdentifier:", id, SW_PERIOD_ITEM);
  }
  msg1(void, sw.pWin, "setTitleVisibility:", long, NSWindowTitleHidden);
  msg1(void, sw.pWin, "setToolbarStyle:", long, NSWindowToolbarStyleUnified);
  msg1(void, sw.pWin, "setToolbar:", id, pBar);
  msg0(void, pBar, "release");

  /* Adding a toolbar changes how much of the window is left for the
  ** content, so set the size of the content again. */
  msg1(void, sw.pWin, "setContentSize:", NSSize,
       CGSizeMake(SW_WIDTH, SW_HEIGHT));
  msg0(void, sw.pWin, "center");
  msg1(BOOL, sw.pWin, "setFrameAutosaveName:", id, S("BeStatsWindow"));

  /* The two panels: words above, time below */
  pRoot = msg0(id, sw.pWin, "contentView");
  for(i=0; i<2; i++){
    NSRect r = CGRectMake(SW_MARGIN,
                          SW_BOTTOM + (1-i)*(SW_PANEL + SW_GAP),
                          SW_WIDTH - 2.0*SW_MARGIN, SW_PANEL);
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
  int i;                          /* Loop counter */
  if( sw.pWin==0 || !msg0(BOOL, sw.pWin, "isVisible") ) return;
  beStatsReport(sw.ePeriod, &sw.report);
  for(i=0; i<2; i++){
    Chart *p = chartOf(sw.aChart[i]);
    if( p->iHover>=sw.report.nBucket ) p->iHover = -1;
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
  int i;                          /* Loop counter */

  if( ePeriod<BE_WEEK || ePeriod>BE_YEAR ) return;
  sw.ePeriod = ePeriod;
  msg1(void, sw.pPeriod, "setSelectedSegment:", long, ePeriod);
  for(i=0; i<2; i++){
    if( sw.aChart[i] ) chartOf(sw.aChart[i])->iHover = -1;
  }
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
** Test interfaces: return the statistics window, and act as if the
** mouse were over bar iBar of panel iChart.
*/
#ifdef BE_TEST
void *beTestStatsWindow(void){
  return sw.pWin;
}
void beTestStatsHover(int iChart, int iBar){
  chartOf(sw.aChart[iChart])->iHover = iBar;
  msg1(void, sw.aChart[iChart], "setNeedsDisplay:", BOOL, YES);
}
#endif
