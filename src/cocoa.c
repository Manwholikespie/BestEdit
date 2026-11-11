/*
** 2026 November 11
**
** Helper routines for talking to Cocoa from C.  See cocoa.h for an
** explanation of how that is done.
**
** Unless stated otherwise, the objects returned by these routines are
** autoreleased.  The caller must retain any object that needs to
** outlive the current pass through the event loop.
*/
#include "cocoa.h"
#include "bestedit.h"

/*
** Convert a UTF-8 C string into an NSString.
*/
id beStr(const char *z){
  CFStringRef pStr = CFStringCreateWithCString(0, z, kCFStringEncodingUTF8);
  if( pStr==0 ) return S("");
  return msg0(id, (id)pStr, "autorelease");
}

/*
** Allocate and initialize a new instance of the class named zClass.
** The caller owns the result and must release it.
*/
id beNew(const char *zClass){
  return msg0(id, msg0(id, cls(zClass), "alloc"), "init");
}

/*
** Return one of the standard system colors.  zName is the name of a
** class method of NSColor.  Ex: "secondaryLabelColor"
*/
id beColor(const char *zName){
  return msg0(id, cls("NSColor"), zName);
}

/*
** Return the system font at size rSize and weight rWeight.  If bDigits
** is true, the digits of the font all have the same width, which stops
** numbers that change from jiggling.
*/
id beFont(double rSize, double rWeight, int bDigits){
  return msg2(id, cls("NSFont"),
              bDigits ? "monospacedDigitSystemFontOfSize:weight:"
                      : "systemFontOfSize:weight:",
              double, rSize, double, rWeight);
}

/*
** Create a static text label with frame r.  The text is initially
** empty.  zColor is a color name suitable for beColor().
*/
id beLabel(NSRect r, double rSize, double rWeight, const char *zColor,
           int bDigits){
  id p = msg1(id, cls("NSTextField"), "labelWithString:", id, S(""));
  msg1(void, p, "setFont:", id, beFont(rSize, rWeight, bDigits));
  msg1(void, p, "setTextColor:", id, beColor(zColor));
  msg1(void, p, "setLineBreakMode:", unsigned long,
       NSLineBreakByTruncatingTail);
  msg1(void, p, "setFrame:", NSRect, r);
  return p;
}

/*
** Change the text shown by a label.
*/
void beSetText(id pLabel, const char *z){
  msg1(void, pLabel, "setStringValue:", id, beStr(z));
}

/*
** Return the system symbol image named zName, or nil if there is no
** such image.  zDesc describes the image for accessibility.
*/
id beSymbol(const char *zName, const char *zDesc){
  return msg2(id, cls("NSImage"),
              "imageWithSystemSymbolName:accessibilityDescription:",
              id, beStr(zName), id, beStr(zDesc));
}

/*
** Return an NSDictionary holding one or two entries.  The second entry
** is omitted if pKey2 is nil.
*/
id beDict(id pKey1, id pVal1, id pKey2, id pVal2){
  id aKey[2];
  id aVal[2];
  aKey[0] = pKey1;  aVal[0] = pVal1;
  aKey[1] = pKey2;  aVal[1] = pVal2;
  return msg3(id, cls("NSDictionary"), "dictionaryWithObjects:forKeys:count:",
              id*, aVal, id*, aKey, unsigned long, pKey2 ? 2 : 1);
}
