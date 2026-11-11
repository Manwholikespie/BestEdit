/*
** 2026 November 11
**
** This file builds the main menu bar.
**
** Most menu items have no explicit target.  Cocoa sends the action of
** such an item to the first object in the responder chain that
** implements it, which is how a single "Copy" item works in whatever
** window is in front, and how "Save" finds the current document.  The
** menus therefore need almost no supporting code.  Only the items of
** the Writing menu are handled by BestEdit itself.
*/
#include "cocoa.h"
#include "bestedit.h"

/*
** Shorthand for the modifier keys.
*/
#define CMD    NSEventModifierFlagCommand
#define SHIFT  NSEventModifierFlagShift
#define OPT    NSEventModifierFlagOption
#define CTRL   NSEventModifierFlagControl

/*
** UTF-8 for the ellipsis that ends the title of a menu item which asks
** for more information before acting.
*/
#define DOTS "\xe2\x80\xa6"

/*
** The menu items whose appearance depends on the state of the timer.
*/
static id pTimerItem = 0;         /* "Start Timer" or "Pause Timer" */
static id pAutoItem = 0;          /* "Start and Pause Automatically" */

/*
** Append a new item to pMenu and return it.  zAction is the name of
** the action selector, or NULL for none.  zKey is the key equivalent,
** or "" for none, and mMod is the set of modifier keys that go with it.
*/
static id menuItem(
  id pMenu,                       /* Add the new item to this menu */
  const char *zTitle,             /* Title of the new item */
  const char *zAction,            /* Action selector, or NULL */
  const char *zKey,               /* Key equivalent, or "" */
  unsigned long mMod              /* Modifier keys for zKey */
){
  id pItem = msg0(id, cls("NSMenuItem"), "alloc");
  pItem = msg3(id, pItem, "initWithTitle:action:keyEquivalent:",
               id, beStr(zTitle),
               SEL, zAction ? sel_registerName(zAction) : (SEL)0,
               id, beStr(zKey));
  if( zKey[0] ){
    msg1(void, pItem, "setKeyEquivalentModifierMask:", unsigned long, mMod);
  }
  msg1(void, pMenu, "addItem:", id, pItem);
  msg0(void, pItem, "release");
  return pItem;
}

/*
** Append an item with an explicit target, and a tag that tells the
** target which variation of the action is wanted.
*/
static id menuItemTo(
  id pMenu,                       /* Add the new item to this menu */
  const char *zTitle,             /* Title of the new item */
  const char *zAction,            /* Action selector */
  const char *zKey,               /* Key equivalent, or "" */
  unsigned long mMod,             /* Modifier keys for zKey */
  id pTarget,                     /* Send the action to this object */
  long iTag                       /* Tag of the new item */
){
  id pItem = menuItem(pMenu, zTitle, zAction, zKey, mMod);
  msg1(void, pItem, "setTarget:", id, pTarget);
  msg1(void, pItem, "setTag:", long, iTag);
  return pItem;
}

/*
** Append a separator line to pMenu.
*/
static void menuSeparator(id pMenu){
  msg1(void, pMenu, "addItem:", id,
       msg0(id, cls("NSMenuItem"), "separatorItem"));
}

/*
** Append a submenu titled zTitle to pParent and return the submenu.
*/
static id menuSubmenu(id pParent, const char *zTitle){
  id pItem = menuItem(pParent, zTitle, 0, "", 0);
  id pMenu = msg0(id, cls("NSMenu"), "alloc");
  pMenu = msg1(id, pMenu, "initWithTitle:", id, beStr(zTitle));
  msg1(void, pItem, "setSubmenu:", id, pMenu);
  msg0(void, pMenu, "release");
  return pMenu;
}

/*
** The application menu.  Cocoa shows the name of the application as
** the title of this menu no matter what title it is given here.
*/
static void menuBuildApp(id pBar, id pApp){
  id pMenu = menuSubmenu(pBar, "BestEdit");
  id pServices;
  menuItem(pMenu, "About BestEdit", "orderFrontStandardAboutPanel:", "", 0);
  menuSeparator(pMenu);
  pServices = menuSubmenu(pMenu, "Services");
  msg1(void, pApp, "setServicesMenu:", id, pServices);
  menuSeparator(pMenu);
  menuItem(pMenu, "Hide BestEdit", "hide:", "h", CMD);
  menuItem(pMenu, "Hide Others", "hideOtherApplications:", "h", CMD|OPT);
  menuItem(pMenu, "Show All", "unhideAllApplications:", "", 0);
  menuSeparator(pMenu);
  menuItem(pMenu, "Quit BestEdit", "terminate:", "q", CMD);
}

/*
** The File menu.
**
** Cocoa fills in the Open Recent submenu by itself, but only if it can
** find it, and it finds it by a name that can only be set through a
** method that is not part of the public interface.  Hence the check
** that the method exists.
*/
static void menuBuildFile(id pBar){
  id pMenu = menuSubmenu(pBar, "File");
  id pRecent;
  menuItem(pMenu, "New", "newDocument:", "n", CMD);
  menuItem(pMenu, "Open" DOTS, "openDocument:", "o", CMD);
  pRecent = menuSubmenu(pMenu, "Open Recent");
  if( msg1(BOOL, pRecent, "respondsToSelector:",
           SEL, sel_registerName("_setMenuName:")) ){
    msg1(void, pRecent, "_setMenuName:", id, S("NSRecentDocumentsMenu"));
  }
  menuItem(pRecent, "Clear Menu", "clearRecentDocuments:", "", 0);
  menuSeparator(pMenu);
  menuItem(pMenu, "Close", "performClose:", "w", CMD);
  menuItem(pMenu, "Save" DOTS, "saveDocument:", "s", CMD);
  menuItem(pMenu, "Save As" DOTS, "saveDocumentAs:", "s", CMD|SHIFT|OPT);
  menuItem(pMenu, "Duplicate", "duplicateDocument:", "s", CMD|SHIFT);
  menuItem(pMenu, "Rename" DOTS, "renameDocument:", "", 0);
  menuItem(pMenu, "Move To" DOTS, "moveDocument:", "", 0);
  menuItem(pMenu, "Revert to Saved", "revertDocumentToSaved:", "", 0);
  menuSeparator(pMenu);
  menuItem(pMenu, "Page Setup" DOTS, "runPageLayout:", "p", CMD|SHIFT);
  menuItem(pMenu, "Print" DOTS, "printDocument:", "p", CMD);
}

/*
** The Edit menu.
*/
static void menuBuildEdit(id pBar){
  id pMenu = menuSubmenu(pBar, "Edit");
  id pSub;
  menuItem(pMenu, "Undo", "undo:", "z", CMD);
  menuItem(pMenu, "Redo", "redo:", "z", CMD|SHIFT);
  menuSeparator(pMenu);
  menuItem(pMenu, "Cut", "cut:", "x", CMD);
  menuItem(pMenu, "Copy", "copy:", "c", CMD);
  menuItem(pMenu, "Paste", "paste:", "v", CMD);
  menuItem(pMenu, "Paste and Match Style", "pasteAsPlainText:", "v",
           CMD|SHIFT|OPT);
  menuItem(pMenu, "Delete", "delete:", "", 0);
  menuItem(pMenu, "Select All", "selectAll:", "a", CMD);
  menuSeparator(pMenu);

  pSub = menuSubmenu(pMenu, "Find");
  menuItemTo(pSub, "Find" DOTS, "performTextFinderAction:", "f", CMD,
             0, NSTextFinderActionShowFindInterface);
  menuItemTo(pSub, "Find and Replace" DOTS, "performTextFinderAction:", "f",
             CMD|OPT, 0, NSTextFinderActionShowReplaceInterface);
  menuItemTo(pSub, "Find Next", "performTextFinderAction:", "g", CMD,
             0, NSTextFinderActionNextMatch);
  menuItemTo(pSub, "Find Previous", "performTextFinderAction:", "g",
             CMD|SHIFT, 0, NSTextFinderActionPreviousMatch);
  menuItemTo(pSub, "Use Selection for Find", "performTextFinderAction:", "e",
             CMD, 0, NSTextFinderActionSetSearchString);
  menuItem(pSub, "Jump to Selection", "centerSelectionInVisibleArea:", "j",
           CMD);

  pSub = menuSubmenu(pMenu, "Spelling and Grammar");
  menuItem(pSub, "Show Spelling and Grammar", "showGuessPanel:", ":", CMD);
  menuItem(pSub, "Check Document Now", "checkSpelling:", ";", CMD);
  menuSeparator(pSub);
  menuItem(pSub, "Check Spelling While Typing",
           "toggleContinuousSpellChecking:", "", 0);
  menuItem(pSub, "Check Grammar With Spelling", "toggleGrammarChecking:",
           "", 0);
  menuItem(pSub, "Correct Spelling Automatically",
           "toggleAutomaticSpellingCorrection:", "", 0);

  pSub = menuSubmenu(pMenu, "Substitutions");
  menuItem(pSub, "Smart Quotes", "toggleAutomaticQuoteSubstitution:", "", 0);
  menuItem(pSub, "Smart Dashes", "toggleAutomaticDashSubstitution:", "", 0);
  menuItem(pSub, "Smart Links", "toggleAutomaticLinkDetection:", "", 0);
  menuItem(pSub, "Text Replacement", "toggleAutomaticTextReplacement:",
           "", 0);

  pSub = menuSubmenu(pMenu, "Speech");
  menuItem(pSub, "Start Speaking", "startSpeaking:", "", 0);
  menuItem(pSub, "Stop Speaking", "stopSpeaking:", "", 0);
}

/*
** The Format menu.  The items that change the font are sent directly
** to the font manager, which uses the tag of the item to learn what
** change is wanted.
*/
static void menuBuildFormat(id pBar){
  id pMenu = menuSubmenu(pBar, "Format");
  id pFontMgr = msg0(id, cls("NSFontManager"), "sharedFontManager");
  id pSub;

  pSub = menuSubmenu(pMenu, "Font");
  menuItemTo(pSub, "Show Fonts", "orderFrontFontPanel:", "t", CMD,
             pFontMgr, 0);
  menuItemTo(pSub, "Bold", "addFontTrait:", "b", CMD,
             pFontMgr, NSBoldFontMask);
  menuItemTo(pSub, "Italic", "addFontTrait:", "i", CMD,
             pFontMgr, NSItalicFontMask);
  menuItem(pSub, "Underline", "underline:", "u", CMD);
  menuSeparator(pSub);
  menuItemTo(pSub, "Bigger", "modifyFont:", "+", CMD,
             pFontMgr, NSSizeUpFontAction);
  menuItemTo(pSub, "Smaller", "modifyFont:", "-", CMD,
             pFontMgr, NSSizeDownFontAction);
  menuSeparator(pSub);
  menuItem(pSub, "Show Colors", "orderFrontColorPanel:", "c", CMD|SHIFT);
  menuSeparator(pSub);
  menuItem(pSub, "Copy Style", "copyFont:", "c", CMD|OPT);
  menuItem(pSub, "Paste Style", "pasteFont:", "v", CMD|OPT);

  pSub = menuSubmenu(pMenu, "Text");
  menuItem(pSub, "Align Left", "alignLeft:", "{", CMD);
  menuItem(pSub, "Center", "alignCenter:", "|", CMD);
  menuItem(pSub, "Justify", "alignJustified:", "", 0);
  menuItem(pSub, "Align Right", "alignRight:", "}", CMD);
  menuSeparator(pSub);
  menuItem(pSub, "Show Ruler", "toggleRuler:", "r", CMD);
  menuItem(pSub, "Copy Ruler", "copyRuler:", "c", CMD|CTRL);
  menuItem(pSub, "Paste Ruler", "pasteRuler:", "v", CMD|CTRL);

  menuSeparator(pMenu);
  menuItem(pMenu, "Add Link" DOTS, "orderFrontLinkPanel:", "k", CMD);
  menuItem(pMenu, "List" DOTS, "orderFrontListPanel:", "", 0);
  menuItem(pMenu, "Table" DOTS, "orderFrontTablePanel:", "", 0);
}

/*
** The Writing menu, which controls the timer and shows the statistics.
*/
static void menuBuildWriting(id pBar){
  id pMenu = menuSubmenu(pBar, "Writing");
  pTimerItem = menuItemTo(pMenu, "Start Timer", "toggleTimer:", "t",
                          CMD|OPT, beDelegate, 0);
  menuItemTo(pMenu, "Reset Timer", "resetTimer:", "", 0, beDelegate, 0);
  pAutoItem = menuItemTo(pMenu, "Start and Pause Automatically",
                         "toggleAutoTimer:", "", 0, beDelegate, 0);
  menuSeparator(pMenu);
  menuItemTo(pMenu, "Statistics", "showStats:", "s", CMD|OPT,
             beDelegate, 0);
}

/*
** The View and Window menus.
*/
static void menuBuildWindow(id pBar, id pApp){
  id pMenu = menuSubmenu(pBar, "View");
  menuItem(pMenu, "Enter Full Screen", "toggleFullScreen:", "f", CMD|CTRL);

  pMenu = menuSubmenu(pBar, "Window");
  menuItem(pMenu, "Minimize", "performMiniaturize:", "m", CMD);
  menuItem(pMenu, "Zoom", "performZoom:", "", 0);
  menuSeparator(pMenu);
  menuItem(pMenu, "Bring All to Front", "arrangeInFront:", "", 0);
  msg1(void, pApp, "setWindowsMenu:", id, pMenu);
}

/*
** Build the main menu bar and install it.  The application delegate
** must already exist.
*/
void beMenuBuild(void){
  id pApp = msg0(id, cls("NSApplication"), "sharedApplication");
  id pBar = beNew("NSMenu");
  menuBuildApp(pBar, pApp);
  menuBuildFile(pBar);
  menuBuildEdit(pBar);
  menuBuildFormat(pBar);
  menuBuildWriting(pBar);
  menuBuildWindow(pBar, pApp);
  msg1(void, pApp, "setMainMenu:", id, pBar);
  msg0(void, pBar, "release");
}

/*
** Bring the Writing menu up to date with the state of the timer.
*/
void beMenuTimerChanged(void){
  if( pTimerItem==0 ) return;
  msg1(void, pTimerItem, "setTitle:", id,
       beTimerIsRunning() ? S("Pause Timer") : S("Start Timer"));
  msg1(void, pAutoItem, "setState:", long,
       beTimerIsAuto() ? NSControlStateValueOn : NSControlStateValueOff);
}
