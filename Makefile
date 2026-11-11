# Build BestEdit.
#
#     make          Build build/BestEdit.app
#     make test     Build and run the tests of the portable code
#     make uitest   Build a test copy of the application and have it
#                   exercise its own user interface
#     make rtftest  Run every RTF file on this computer through the
#                   editor and report what does not survive
#     make run      Build and launch the application
#     make install  Copy the application into /Applications
#     make clean    Delete everything that was built
#
CC      = cc
CFLAGS  = -std=c99 -O2 -Wall -Wextra -Isrc
LIBS    = -framework Cocoa -lsqlite3 -lobjc

BUILD   = build
APP     = $(BUILD)/BestEdit.app
EXE     = $(APP)/Contents/MacOS/BestEdit
ICON    = $(APP)/Contents/Resources/BestEdit.icns

# Source that is portable C, and source that needs macOS.
CORE    = src/wordcount.c src/util.c src/stats.c src/rtfrepair.c
MAC     = src/main.c src/menu.c src/doc.c src/statswin.c src/cocoa.c \
          src/timer.c
HDR     = src/bestedit.h src/cocoa.h

all: $(APP)

$(APP): $(EXE) $(APP)/Contents/Info.plist $(ICON)
	codesign --force --sign - $(APP)
	touch $(APP)

# The application icon is drawn by a program, tools/mkicon.c, and then
# packed into an .icns file.
$(BUILD)/mkicon: tools/mkicon.c
	mkdir -p $(BUILD)
	$(CC) $(CFLAGS) -o $(BUILD)/mkicon tools/mkicon.c \
	    -framework CoreGraphics -framework ImageIO -framework CoreFoundation

$(BUILD)/BestEdit.icns: $(BUILD)/mkicon
	rm -rf $(BUILD)/BestEdit.iconset
	mkdir -p $(BUILD)/BestEdit.iconset
	$(BUILD)/mkicon $(BUILD)/BestEdit.iconset
	iconutil -c icns -o $(BUILD)/BestEdit.icns $(BUILD)/BestEdit.iconset

$(ICON): $(BUILD)/BestEdit.icns
	mkdir -p $(APP)/Contents/Resources
	cp $(BUILD)/BestEdit.icns $(ICON)

$(EXE): $(CORE) $(MAC) $(HDR)
	mkdir -p $(APP)/Contents/MacOS
	$(CC) $(CFLAGS) -o $(EXE) $(CORE) $(MAC) $(LIBS)

$(APP)/Contents/Info.plist: Info.plist
	mkdir -p $(APP)/Contents
	cp Info.plist $(APP)/Contents/Info.plist

$(BUILD)/betest: test/betest.c $(CORE) src/bestedit.h
	mkdir -p $(BUILD)
	$(CC) $(CFLAGS) -o $(BUILD)/betest test/betest.c $(CORE) -lsqlite3

test: $(BUILD)/betest
	$(BUILD)/betest $(BUILD)/test.db

# The test copy of the application.  It includes the files in test/ and
# has its own bundle identifier, so that it does not disturb the
# preferences of the real one.
TESTAPP = $(BUILD)/uitest/BestEdit.app
TESTEXE = $(TESTAPP)/Contents/MacOS/BestEdit
TESTSRC = test/uitest.c test/rtftest.c

$(TESTEXE): $(CORE) $(MAC) $(HDR) $(TESTSRC) Info.plist $(BUILD)/BestEdit.icns
	rm -rf $(BUILD)/uitest
	mkdir -p $(TESTAPP)/Contents/MacOS $(TESTAPP)/Contents/Resources
	cp $(BUILD)/BestEdit.icns $(TESTAPP)/Contents/Resources/BestEdit.icns
	sed 's/com.bestedit.BestEdit/com.bestedit.BestEditTest/' Info.plist \
	    > $(TESTAPP)/Contents/Info.plist
	$(CC) $(CFLAGS) -DBE_TEST -o $(TESTEXE) $(CORE) $(MAC) $(TESTSRC) $(LIBS)

# The user interface test.  The application drives itself, using a
# scratch statistics database.  Pictures of its windows are left in
# build/uitest.
uitest: $(TESTEXE)
	rm -f $(BUILD)/uitest/stats.db*
	BESTEDIT_DB=$(abspath $(BUILD))/uitest/stats.db \
	BESTEDIT_UITEST=$(abspath $(BUILD))/uitest \
	BESTEDIT_TESTDATA=$(abspath test/data) \
	    $(TESTEXE) -ApplePersistenceIgnoreState YES

# The RTF test.  Every RTF file on this computer that locate or
# Spotlight knows about is opened, displayed, saved, and read back, in
# memory, and the results are summarized.  The files are only read.
# RTFLIST can be set to a file that lists the files to test, one per
# line.
RTFLIST = $(BUILD)/rtftest/list.txt

$(BUILD)/rtftest/list.txt:
	mkdir -p $(BUILD)/rtftest
	(locate '*.rtf'; mdfind 'kMDItemFSName == "*.rtf"c') 2>/dev/null \
	    | grep -v '^$(CURDIR)/$(BUILD)/' | sort -u > $@

rtftest: $(TESTEXE) $(RTFLIST)
	mkdir -p $(BUILD)/rtftest/text
	BESTEDIT_DB=$(abspath $(BUILD))/rtftest/stats.db \
	BESTEDIT_RTFTEST=$(RTFLIST) \
	BESTEDIT_RTFDUMP=$(abspath $(BUILD))/rtftest/text \
	    $(TESTEXE) -ApplePersistenceIgnoreState YES \
	    > $(BUILD)/rtftest/result.tsv
	python3 -I test/rtfreport.py $(BUILD)/rtftest/result.tsv \
	    $(BUILD)/rtftest/text

run: $(APP)
	open $(APP)

install: $(APP)
	rm -rf /Applications/BestEdit.app
	cp -R $(APP) /Applications/BestEdit.app

clean:
	rm -rf $(BUILD)

.PHONY: all test uitest rtftest run install clean
