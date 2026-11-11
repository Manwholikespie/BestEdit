# BestEdit

A small rich-text editor for macOS in the spirit of TextEdit, with a live
word count, a writing timer, and a record of how much you wrote and how
long you spent writing over the past week, month, and year.

It is written in C, in the style of the SQLite source code. There is no
Objective-C or Swift: the program talks to Cocoa through the C interface
of the Objective-C runtime.

## Building

    make            # builds build/BestEdit.app
    make run        # builds and launches it
    make install    # copies it into /Applications

Requires the Xcode command line tools. There are no other dependencies;
SQLite ships with macOS.

## Using it

Editing works the way it does in TextEdit: the same format bar, ruler,
fonts and colors panels, find bar, spelling, autosave, and versions.
Documents are RTF by default. Plain text files open in a plain window.

The status bar at the bottom of each window shows:

- **Word and character counts** for the document, and for the selection
  when there is one.
- **The session timer**, with a button to start and pause it and a button
  that opens the statistics.

The **Writing** menu has the same controls:

| Item | Key | What it does |
|---|---|---|
| Start / Pause Timer | ⌥⌘T | Starts or pauses the timer |
| Reset Timer | | Zeroes the session display; recorded time is kept |
| Start and Pause Automatically | | On by default; see below |
| Statistics | ⌥⌘S | Opens the statistics window |

With **Start and Pause Automatically** on, the timer starts when you
begin typing and pauses after three minutes without a keystroke. Turn it
off to run the timer entirely by hand.

The statistics window shows words written, time spent, days active, and
words per active day, with a bar chart of each, for the past 7 days, 30
days, or 12 months. Hover over a bar for its exact value.

## What gets counted

- **Words written** is net: words added minus words removed, in any
  document, by any means (typing, pasting, deleting, undo). Opening a
  file does not count. A day on which you cut more than you wrote shows
  no bar, but the totals still include it.
- **Time spent** is the time the timer was running.
- A word is a run of characters between spaces that contains a letter or
  digit, so `don't` and `well-known` are one word each and a dash standing
  alone is none. An em-dash separates words.

Statistics are kept in an SQLite database at
`~/Library/Application Support/BestEdit/stats.db`, one row per day. Set
the environment variable `BESTEDIT_DB` to use a different file.

## Tests

    make test       # word counting, formatting, RTF repair, statistics database
    make uitest     # the application drives its own user interface
    make rtftest    # every RTF file on this computer, through the editor

`make uitest` builds a second copy of the application that types into a
document, checks the counts and the statistics, saves and reopens files,
and leaves pictures of its windows in `build/uitest`.

`make rtftest` finds every `.rtf` file on the machine with `locate` and
Spotlight, and for each one does, in memory, what happens when a document is opened,
edited, and saved. The files are only read. It reports anything that does
not survive, and cross-checks the text against a second, independent RTF
reader in `test/rtfreport.py`.

## RTF fidelity

RTF is read and written by Cocoa, the same code TextEdit uses, so
BestEdit understands what TextEdit understands. `make rtftest` measures
how well that holds up. On the machine this was developed on it covered
4,109 RTF files holding 3.2 million words, written by Cocoa, Microsoft
Word, RichEdit, and LibreOffice:

- Every file opens.
- Every letter and digit that the independent reader finds in a file is
  displayed (18.6 million of them).
- Every file has identical text after being saved and read back.
- Formatting is identical after saving in 3,649 files and differs in no
  visible way in 451 more. Nine files change visibly, as described below.

Getting there took two fixes for problems that TextEdit has:

- **List markers.** Cocoa's RTF writer duplicates the marker of a list
  paragraph (`1. First` comes back as `1. 1. First`) whenever the
  paragraph's text does not begin with exactly the marker Cocoa expects,
  which is always the case for lists written by Word. BestEdit takes
  such paragraphs out of their list: they keep their text and
  indentation but stop being renumbered automatically.
- **Unescaped UTF-8.** RTF is supposed to be 7-bit, but files with raw
  UTF-8 in them are common. Cocoa reads them as garbage or silently
  drops whole passages. BestEdit detects UTF-8 and reads it as text.

What remains are quirks of Cocoa's RTF writer that BestEdit shares with
TextEdit:

- Text in the system interface font comes back in Helvetica Neue
  (4 files, all resources of Apple applications).
- Underlined text that follows text with a colored underline takes on
  that underline color (4 files).
- Text with no paragraph formatting at all gains a default tab interval
  (1 file), and a creation date that is not a valid date changes
  (4 files).
- Pictures are not supported. A document is RTF, not RTFD.

## Source

    src/bestedit.h    Internal interfaces
    src/cocoa.h       Glue for calling Cocoa from C
    src/main.c        Startup and the application delegate
    src/menu.c        The menu bar
    src/doc.c         The document class and the editor window
    src/statswin.c    The statistics window and its charts
    src/cocoa.c       Helpers for calling Cocoa from C
    src/timer.c       The writing timer
    src/stats.c       The statistics database          (portable C)
    src/wordcount.c   Word and character counting      (portable C)
    src/rtfrepair.c   Repair of RTF holding raw UTF-8  (portable C)
    src/util.c        Number and time formatting       (portable C)
    test/             Tests
