/*
** 2026 November 11
**
** This header defines the glue that lets plain C talk to Cocoa.
**
** Cocoa is an Objective-C API, but every Objective-C message send is
** really a call to the C function objc_msgSend() and every class can be
** looked up or created through the C interface of the Objective-C
** runtime.  The macros below wrap objc_msgSend() so that a message send
** such as
**
**       [pWindow setTitle: zTitle];
**
** is written
**
**       msg1(void, pWindow, "setTitle:", id, zTitle);
**
** The first macro argument is the return type.  Each message argument
** is preceded by its type.  The types matter: objc_msgSend() must be
** cast to the exact prototype of the method being invoked or else
** arguments end up in the wrong registers.
**
** Because the Cocoa headers are Objective-C and cannot be included from
** C, the handful of AppKit constants and symbols that BestEdit needs
** are redeclared here.
*/
#ifndef BE_COCOA_H
#define BE_COCOA_H

#include <objc/objc.h>
#include <objc/runtime.h>
#include <objc/message.h>
#include <CoreFoundation/CoreFoundation.h>
#include <CoreGraphics/CoreGraphics.h>

/*
** Geometry types.  NSRange is the same as CFRange except unsigned.
*/
typedef CGPoint NSPoint;
typedef CGSize NSSize;
typedef CGRect NSRect;
typedef struct NSRange NSRange;
struct NSRange {
  unsigned long location;      /* Index of the first element */
  unsigned long length;        /* Number of elements */
};

/*
** Look up a class by name.  Convert a C string literal into a constant
** NSString.
*/
#define cls(z)  ((id)objc_getClass(z))
#define S(z)    ((id)CFSTR(z))

/*
** Message sends taking between zero and four arguments.
*/
#define msg0(R,o,s) \
  (((R(*)(id,SEL))objc_msgSend)((id)(o),sel_registerName(s)))
#define msg1(R,o,s,A,a) \
  (((R(*)(id,SEL,A))objc_msgSend)((id)(o),sel_registerName(s),(a)))
#define msg2(R,o,s,A,a,B,b) \
  (((R(*)(id,SEL,A,B))objc_msgSend)((id)(o),sel_registerName(s),(a),(b)))
#define msg3(R,o,s,A,a,B,b,C,c) \
  (((R(*)(id,SEL,A,B,C))objc_msgSend) \
      ((id)(o),sel_registerName(s),(a),(b),(c)))
#define msg4(R,o,s,A,a,B,b,C,c,D,d) \
  (((R(*)(id,SEL,A,B,C,D))objc_msgSend) \
      ((id)(o),sel_registerName(s),(a),(b),(c),(d)))

/*
** A message send that returns an NSRect.  On x86_64 a structure this
** large is returned through a hidden pointer, which requires a different
** entry point into the runtime.
*/
#if defined(__x86_64__)
# define msgRect(o,s) \
  (((NSRect(*)(id,SEL))objc_msgSend_stret)((id)(o),sel_registerName(s)))
#else
# define msgRect(o,s) msg0(NSRect,o,s)
#endif

/*
** The Objective-C type encoding of BOOL, for use in method signatures.
*/
#if defined(__x86_64__)
# define BE_BOOL "c"
#else
# define BE_BOOL "B"
#endif
#define BE_RECT "{CGRect={CGPoint=dd}{CGSize=dd}}"
#define BE_POINT "{CGPoint=dd}"

/*
** NSTextAlignment has different values on Intel than on Apple silicon.
*/
#define NSTextAlignmentLeft            0
#if defined(__x86_64__)
# define NSTextAlignmentRight          1
# define NSTextAlignmentCenter         2
#else
# define NSTextAlignmentCenter         1
# define NSTextAlignmentRight          2
#endif

/*
** Assorted AppKit and Foundation constants.
*/
#define NSWindowStyleMaskTitled         1
#define NSWindowStyleMaskClosable       2
#define NSWindowStyleMaskMiniaturizable 4
#define NSWindowStyleMaskResizable      8
#define NSBackingStoreBuffered          2

#define NSViewMinXMargin                1
#define NSViewWidthSizable              2
#define NSViewMaxXMargin                4
#define NSViewMinYMargin                8
#define NSViewHeightSizable             16
#define NSViewMaxYMargin                32

#define NSEventModifierFlagShift        (1UL<<17)
#define NSEventModifierFlagControl      (1UL<<18)
#define NSEventModifierFlagOption       (1UL<<19)
#define NSEventModifierFlagCommand      (1UL<<20)

#define NSItalicFontMask                1
#define NSBoldFontMask                  2
#define NSSizeUpFontAction              3
#define NSSizeDownFontAction            4

#define NSTextFinderActionShowFindInterface     1
#define NSTextFinderActionNextMatch             2
#define NSTextFinderActionPreviousMatch         3
#define NSTextFinderActionSetSearchString       7
#define NSTextFinderActionShowReplaceInterface  12

#define NSTrackingMouseEnteredAndExited 0x01
#define NSTrackingMouseMoved            0x02
#define NSTrackingActiveAlways          0x80
#define NSTrackingInVisibleRect         0x200

#define NSWindowTitleHidden             1
#define NSWindowToolbarStyleUnified     3
#define NSToolbarDisplayModeIconOnly    2

#define NSBoxSeparator                  2
#define NSImageOnly                     1
#define NSLineBreakByTruncatingTail     4
#define NSControlStateValueOn           1
#define NSControlStateValueOff          0
#define NSUTF8StringEncoding            4
#define NSApplicationSupportDirectory   14
#define NSUserDomainMask                1
#define NSFileReadCorruptFileError      259
#define NSFileWriteUnknownError         512
#define NSBitmapImageFileTypePNG        4

/*
** Symbols exported by AppKit and Foundation.
*/
extern id NSFontAttributeName;
extern id NSForegroundColorAttributeName;
extern id NSParagraphStyleAttributeName;
extern id NSDocumentTypeDocumentAttribute;
extern id NSRTFTextDocumentType;
extern id NSAppearanceNameAqua;
extern id NSCocoaErrorDomain;
extern id NSFontDescriptorSystemDesignRounded;
extern const double NSFontWeightRegular;
extern const double NSFontWeightMedium;
extern const double NSFontWeightSemibold;
extern id NSSearchPathForDirectoriesInDomains(unsigned long, unsigned long,
                                              BOOL);

/*
** cocoa.c
*/
id beStr(const char*);
id beNew(const char*);
id beColor(const char*);
id beFont(double, double, int);
id beLabel(NSRect, double, double, const char*, int);
id beSymbol(const char*, const char*);
id beDict(id, id, id, id);
void beSetText(id, const char*);

/*
** The application delegate.  It is the target of every custom action.
*/
extern id beDelegate;

#endif /* BE_COCOA_H */
