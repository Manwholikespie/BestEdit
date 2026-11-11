/*
** 2026 November 11
**
** This program draws the application icon of BestEdit.
**
** Usage:
**
**     mkicon DIRECTORY
**
** The icon is written into DIRECTORY as the set of PNG files, at sizes
** from 16 to 1024 pixels, that the iconutil program turns into an
** .icns file.  DIRECTORY must already exist and should have a name
** that ends in ".iconset".
**
** The icon is a sheet of paper bearing a few lines of text, with the
** insertion point blinking at the end of the last line.  The lines are
** of unequal length, so that they also read as the bars of a chart.
** The lines already written are gray.  The line being written is
** black.
**
** The drawing is done with CoreGraphics on a canvas 1024 units square,
** following the layout that macOS icons share: a rounded square 824
** units on a side, centered, with a soft shadow beneath it.  Each size
** is drawn afresh at its own resolution rather than scaled down from a
** larger one.
*/
#include <CoreGraphics/CoreGraphics.h>
#include <ImageIO/ImageIO.h>
#include <stdio.h>

/*
** Geometry of the icon, in units of the 1024-unit canvas.
*/
#define ICON_CANVAS   1024.0      /* Size of the canvas */
#define ICON_INSET    100.0       /* From the canvas to the paper */
#define ICON_RADIUS   185.0       /* Corner radius of the paper */
#define LINE_LEFT     262.0       /* Left end of every line of text */
#define LINE_WIDTH    500.0       /* Length of the longest line */
#define LINE_HEIGHT   50.0        /* Thickness of a line */
#define LINE_PITCH    112.0       /* From one line to the next */
#define LINE_TOP      680.0       /* Center of the topmost line */

/*
** The length of each line of text, as a fraction of LINE_WIDTH.
*/
static const double aLength[] = { 1.00, 0.66, 0.84, 0.40 };
#define NLINE ((int)(sizeof(aLength)/sizeof(aLength[0])))

/*
** Add a rounded rectangle to the current path of c.
*/
static void roundRect(CGContextRef c, CGRect r, double rRadius){
  CGPathRef pPath = CGPathCreateWithRoundedRect(r, rRadius, rRadius, 0);
  CGContextAddPath(c, pPath);
  CGPathRelease(pPath);
}

/*
** Draw the icon into context c, which has been scaled so that the
** canvas is ICON_CANVAS units square.
*/
static void drawIcon(CGContextRef c, CGColorSpaceRef pSpace){
  static const CGFloat aPaper[8] = {
    1.000, 1.000, 1.000, 1.0,     /* Top of the paper */
    0.905, 0.912, 0.930, 1.0      /* Bottom of the paper */
  };
  static const CGFloat aShadow[4] = { 0.0, 0.0, 0.0, 0.30 };
  static const CGFloat aInk[4] = { 0.110, 0.118, 0.141, 1.0 };
  static const CGFloat aPast[4] = { 0.710, 0.725, 0.765, 1.0 };
  static const CGFloat aCaret[4] = { 0.039, 0.518, 1.000, 1.0 };
  static const CGFloat aEdge[4] = { 1.0, 1.0, 1.0, 0.9 };
  CGRect rPaper = CGRectMake(ICON_INSET, ICON_INSET,
                             ICON_CANVAS - 2*ICON_INSET,
                             ICON_CANVAS - 2*ICON_INSET);
  CGColorRef pColor;              /* A color */
  CGGradientRef pGrad;            /* The shading of the paper */
  double x = 0.0;                 /* Right end of the last line */
  double y = 0.0;                 /* Center of the last line */
  int i;                          /* Loop counter */

  /* The paper, first as a solid shape that casts the shadow */
  CGContextSaveGState(c);
  pColor = CGColorCreate(pSpace, aShadow);
  CGContextSetShadowWithColor(c, CGSizeMake(0.0, -12.0), 28.0, pColor);
  CGColorRelease(pColor);
  CGContextSetRGBFillColor(c, 0.93, 0.93, 0.95, 1.0);
  roundRect(c, rPaper, ICON_RADIUS);
  CGContextFillPath(c);
  CGContextRestoreGState(c);

  /* The paper again, shaded from top to bottom */
  CGContextSaveGState(c);
  roundRect(c, rPaper, ICON_RADIUS);
  CGContextClip(c);
  pGrad = CGGradientCreateWithColorComponents(pSpace, aPaper, 0, 2);
  CGContextDrawLinearGradient(c, pGrad,
      CGPointMake(0.0, ICON_CANVAS - ICON_INSET),
      CGPointMake(0.0, ICON_INSET), 0);
  CGGradientRelease(pGrad);

  /* A bright rim just inside the top edge, where the light catches */
  pColor = CGColorCreate(pSpace, aEdge);
  CGContextSetStrokeColorWithColor(c, pColor);
  CGColorRelease(pColor);
  CGContextSetLineWidth(c, 6.0);
  roundRect(c, CGRectOffset(rPaper, 0.0, -3.0), ICON_RADIUS);
  CGContextStrokePath(c);
  CGContextRestoreGState(c);

  /* The lines of text */
  for(i=0; i<NLINE; i++){
    pColor = CGColorCreate(pSpace, i==NLINE-1 ? aInk : aPast);
    CGContextSetFillColorWithColor(c, pColor);
    CGColorRelease(pColor);
    x = LINE_LEFT + LINE_WIDTH*aLength[i];
    y = LINE_TOP - LINE_PITCH*i;
    roundRect(c, CGRectMake(LINE_LEFT, y - LINE_HEIGHT/2.0,
                            x - LINE_LEFT, LINE_HEIGHT), LINE_HEIGHT/2.0);
    CGContextFillPath(c);
  }

  /* The insertion point, after the last line */
  pColor = CGColorCreate(pSpace, aCaret);
  CGContextSetFillColorWithColor(c, pColor);
  CGColorRelease(pColor);
  roundRect(c, CGRectMake(x + 32.0, y - 66.0, 24.0, 132.0), 12.0);
  CGContextFillPath(c);
}

/*
** Draw the icon nPixel pixels square and write it to file zName in
** directory zDir.  Return 0 on success and 1 on failure.
*/
static int writeIcon(const char *zDir, const char *zName, int nPixel){
  char zPath[2048];               /* Name of the output file */
  CGColorSpaceRef pSpace;         /* sRGB */
  CGContextRef c;                 /* The bitmap being drawn */
  CGImageRef pImage;              /* The finished picture */
  CFStringRef pPath;              /* zPath as a CFString */
  CFURLRef pUrl;                  /* zPath as a CFURL */
  CGImageDestinationRef pDest;    /* The PNG writer */
  int rc = 1;                     /* Return code */

  snprintf(zPath, sizeof(zPath), "%s/%s", zDir, zName);
  pSpace = CGColorSpaceCreateWithName(kCGColorSpaceSRGB);
  c = CGBitmapContextCreate(0, nPixel, nPixel, 8, 0, pSpace,
                            kCGImageAlphaPremultipliedLast);
  if( c==0 ){
    CGColorSpaceRelease(pSpace);
    return 1;
  }
  CGContextScaleCTM(c, nPixel/ICON_CANVAS, nPixel/ICON_CANVAS);
  drawIcon(c, pSpace);
  pImage = CGBitmapContextCreateImage(c);
  pPath = CFStringCreateWithCString(0, zPath, kCFStringEncodingUTF8);
  pUrl = CFURLCreateWithFileSystemPath(0, pPath, kCFURLPOSIXPathStyle, 0);
  pDest = CGImageDestinationCreateWithURL(pUrl, CFSTR("public.png"), 1, 0);
  if( pDest ){
    CGImageDestinationAddImage(pDest, pImage, 0);
    if( CGImageDestinationFinalize(pDest) ) rc = 0;
    CFRelease(pDest);
  }
  if( rc ) fprintf(stderr, "cannot write %s\n", zPath);
  CFRelease(pUrl);
  CFRelease(pPath);
  CGImageRelease(pImage);
  CGContextRelease(c);
  CGColorSpaceRelease(pSpace);
  return rc;
}

int main(int argc, char **argv){
  static const struct {
    const char *zName;            /* Name of the file */
    int nPixel;                   /* Size in pixels */
  } aIcon[] = {
    { "icon_16x16.png",        16 },
    { "icon_16x16@2x.png",     32 },
    { "icon_32x32.png",        32 },
    { "icon_32x32@2x.png",     64 },
    { "icon_128x128.png",     128 },
    { "icon_128x128@2x.png",  256 },
    { "icon_256x256.png",     256 },
    { "icon_256x256@2x.png",  512 },
    { "icon_512x512.png",     512 },
    { "icon_512x512@2x.png", 1024 },
  };
  int i;                          /* Loop counter */
  int rc = 0;                     /* Exit code */

  if( argc!=2 ){
    fprintf(stderr, "Usage: %s DIRECTORY\n", argv[0]);
    return 1;
  }
  for(i=0; i<(int)(sizeof(aIcon)/sizeof(aIcon[0])); i++){
    rc |= writeIcon(argv[1], aIcon[i].zName, aIcon[i].nPixel);
  }
  return rc;
}
