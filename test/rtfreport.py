#!/usr/bin/env python3
#
# 2026 November 11
#
# Summarize the output of the RTF test.  See test/rtftest.c.
#
# Usage:
#
#     rtfreport.py RESULT.TSV [TEXTDIR]
#
# RESULT.TSV is the output of the test: one line per file.  If TEXTDIR
# is given, it is the directory into which the test wrote the text of
# each file as BestEdit displays it.  That text is compared against the
# text found by the small RTF reader in this script, which shares no
# code with the RTF reader inside of Cocoa that BestEdit uses.  A
# letter or digit that this script finds in a file, but that BestEdit
# does not display, is text that would be lost from the file.  (Text
# set in a symbol font is ignored, as are bytes above 127 that appear
# in a file unescaped, which is not valid RTF, unless they are UTF-8.)
#
# In the test output, the name of a formatting change ends in "~" if
# the change has no visible effect.  Those are counted separately.
#
# The exit code is zero unless a file could not be read, could not be
# saved, or lost text when saved.
#
import collections
import re
import sys
import unicodedata

# Destinations whose content is not part of the text of the document.
SKIP = frozenset("""
  fonttbl colortbl stylesheet info pict object objdata header headerl
  headerr headerf footer footerl footerr footerf fldinst generator
  listtable listoverridetable revtbl rsidtbl themedata datastore
  colorschememapping latentstyles xmlnstbl filetbl bkmkstart bkmkend
  panose falt pgdsctbl mmathpr datafield template operator author title
  subject keywords doccomm company manager category userprops
  nextgraphic nonshppict shpinst sp sn sv xe tc atnid atnauthor
  annotation atrfstart atrfend
""".split())

# Control words that stand for a character.
SPECIAL = {
  "par": "\n", "line": "\n", "sect": "\n", "page": "\n", "row": "\n",
  "tab": "\t", "cell": "\t", "emdash": "\u2014", "endash": "\u2013",
  "emspace": "\u2003", "enspace": "\u2002", "qmspace": "\u2005",
  "bullet": "\u2022", "lquote": "\u2018", "rquote": "\u2019",
  "ldblquote": "\u201c", "rdblquote": "\u201d",
}

# The text encoding implied by each font character set.
CHARSET = {
  0: "cp1252", 1: "cp1252", 2: "cp1252", 77: "mac_roman", 78: "shift_jis",
  79: "euc_kr", 80: "gb2312", 81: "big5", 83: "mac_roman",
  84: "mac_roman", 128: "cp932", 129: "cp949", 130: "johab", 134: "gbk",
  136: "cp950", 161: "cp1253", 162: "cp1254", 163: "cp1258",
  177: "cp1255", 178: "cp1256", 186: "cp1257", 204: "cp1251",
  222: "cp874", 238: "cp1250", 254: "cp437", 255: "cp850",
}

TOKEN = re.compile(
  r"\\([a-zA-Z]{1,32})(-?\d{1,10})?[ ]?"   # 1,2: control word, parameter
  r"|\\'([0-9a-fA-F]{2})"                  # 3: byte in hexadecimal
  r"|\\([^a-zA-Z])"                        # 4: control symbol
  r"|([{}])"                               # 5: group
  r"|[\r\n]+"                              #    ignored
  r"|([\x80-\xff]+)"                       # 6: bytes above 127
  r"|(.)",                                 # 7: literal character
  re.S)


def codec_ok(name):
    try:
        b"".decode(name)
        return True
    except LookupError:
        return False


def rtf_text(data):
    """Return the text of the RTF document in the bytes of data."""
    src = data.decode("latin-1")
    out = []                    # Pieces of the result
    pending = bytearray()       # Bytes waiting to be decoded
    stack = []                  # Saved state of enclosing groups
    skip = False                # True inside a skipped destination
    uc = 1                      # Characters to skip after a \u
    font = None                 # Current font number
    default = "cp1252"          # Encoding of text with no font
    fonts = {}                  # Font number -> encoding
    in_fonttbl = False          # True inside of the font table
    tblfont = None              # Font being defined by the font table
    drop = 0                    # Characters left to skip after a \u
    star = False                # True just after a \*

    # Bytes above 127 are not valid RTF.  If all of them together are
    # well-formed UTF-8, take them to be text.  Otherwise ignore them.
    try:
        for run in re.findall(rb"[\x80-\xff]+", data):
            run.decode("utf-8")
        raw_utf8 = True
    except UnicodeDecodeError:
        raw_utf8 = False

    def flush():
        if pending:
            enc = fonts.get(font, default)
            if enc != "symbol":
                out.append(pending.decode(enc, "replace"))
            pending.clear()

    for m in TOKEN.finditer(src):
        word, arg, hexbyte, symbol, brace, raw, char = m.groups()
        if brace == "{":
            flush()
            stack.append((skip, uc, font, in_fonttbl))
            star = False
            continue
        if brace == "}":
            flush()
            if stack:
                skip, uc, font, in_fonttbl = stack.pop()
            drop = 0
            continue
        if word is not None:
            was_star, star = star, False
            if word == "bin" and arg:
                continue
            if in_fonttbl:
                if word == "f" and arg:
                    tblfont = int(arg)
                elif word == "ftech" and tblfont is not None:
                    fonts[tblfont] = "symbol"
                elif word == "fcharset" and arg and tblfont is not None:
                    enc = CHARSET.get(int(arg))
                    if enc and codec_ok(enc):
                        fonts[tblfont] = enc
                elif word == "cpg" and arg and tblfont is not None:
                    if codec_ok("cp" + arg):
                        fonts[tblfont] = "cp" + arg
                continue
            if word == "fonttbl":
                in_fonttbl = True
                skip = True
                continue
            if word.lower() in SKIP or was_star:
                skip = True
                continue
            if word == "ansicpg" and arg:
                if codec_ok("cp" + arg):
                    default = "cp" + arg
            elif word == "mac":
                default = "mac_roman"
            elif word == "pc":
                default = "cp437"
            elif word == "uc" and arg:
                uc = int(arg)
            elif word == "f" and arg:
                flush()
                font = int(arg)
            elif word == "u" and arg:
                flush()
                code = int(arg)
                if code < 0:
                    code += 65536
                if not skip:
                    out.append(chr(code))
                drop = uc
            elif word in SPECIAL:
                flush()
                if not skip:
                    out.append(SPECIAL[word])
            continue
        if symbol is not None:
            was_star, star = star, False
            if symbol == "*":
                star = True
            elif drop > 0:
                drop -= 1
            elif skip:
                pass
            elif symbol == "~":
                flush()
                out.append("\u00a0")
            elif symbol in "{}\\":
                flush()
                out.append(symbol)
            elif symbol in "\r\n":
                flush()
                out.append("\n")
            continue
        star = False
        if hexbyte is not None:
            if drop > 0:
                drop -= 1
            elif not skip:
                pending.append(int(hexbyte, 16))
            continue
        if raw is not None:
            if raw_utf8 and not skip:
                flush()
                out.append(raw.encode("latin-1").decode("utf-8"))
            continue
        if char is not None:
            if drop > 0:
                drop -= 1
            elif not skip:
                flush()
                out.append(char)
    flush()

    # Join the two halves of surrogate pairs written as two \u words.
    text = "".join(out)
    return text.encode("utf-16", "surrogatepass").decode("utf-16", "replace")


def letters(text):
    """Return a count of each letter and digit in text."""
    text = unicodedata.normalize("NFKC", text)
    return collections.Counter(c for c in text if c.isalnum())


def main():
    rows = []
    for line in open(sys.argv[1], encoding="utf-8", errors="replace"):
        f = line.rstrip("\n").split("\t")
        if len(f) >= 12:
            rows.append(f)
    textdir = sys.argv[2] if len(sys.argv) > 2 else None

    status = collections.Counter(r[0] for r in rows)
    ok = [(i, r) for i, r in enumerate(rows) if r[0] == "ok"]
    words = sum(int(r[2]) for _, r in ok)
    print("%d files, %s words" % (len(rows), format(words, ",")))
    for name, n in status.most_common():
        print("  %-14s %d" % (name, n))

    def tally(title, col, ignore=()):
        seen = collections.Counter()
        hidden = collections.Counter()
        clean = same_look = 0
        for _, r in ok:
            keys = [k for k in r[col].split(",") if k != "-" and k not in ignore]
            visible = [k for k in keys if not k.endswith("~")]
            if not keys:
                clean += 1
            elif not visible:
                same_look += 1
            for k in keys:
                (hidden if k.endswith("~") else seen)[k.rstrip("~")] += 1
        print("%s:" % title)
        print("  %d files identical, %d more look the same, %d differ"
              % (clean, same_look, len(ok) - clean - same_look))
        for name, n in seen.most_common():
            print("    visible:   %-40s %d files" % (name, n))
        for name, n in hidden.most_common():
            print("    invisible: %-40s %d files" % (name, n))

    print("text displayed differs from text read: %d"
          % sum(1 for _, r in ok if r[5] != "1"))
    lost_text = [r for _, r in ok if r[6] != "1"]
    print("text changed by saving and reading back: %d" % len(lost_text))
    for r in lost_text[:20]:
        print("  attachments=%s  %s" % (r[4], r[11]))
    print("files with attachments: %d"
          % sum(1 for _, r in ok if r[4] != "0"))
    tally("formatting, when displayed", 7)
    tally("formatting, after saving and reading back", 8)
    tally("document properties, after saving and reading back", 9,
          ignore=("CocoaRTFVersion", "TextScaling"))
    print("second save not byte-for-byte the same as the first: %d"
          % sum(1 for _, r in ok if r[10] != "1"))

    missing_files = []
    if textdir:
        total = absent = 0
        for i, r in ok:
            try:
                ours = open("%s/%d.txt" % (textdir, i), encoding="utf-8").read()
                theirs = rtf_text(open(r[11], "rb").read())
            except OSError:
                continue
            want = letters(theirs)
            have = letters(ours)
            gone = want - have
            n = sum(gone.values())
            total += sum(want.values())
            absent += n
            if n > 0:
                missing_files.append((n, sum(want.values()), r[11],
                                      "".join(sorted(gone))[:40]))
        print("letters and digits found by the independent reader: %s"
              % format(total, ","))
        print("  of which BestEdit does not display: %s in %d files"
              % (format(absent, ","), len(missing_files)))
        for n, t, path, sample in sorted(missing_files, reverse=True)[:25]:
            print("  %6d of %-8d %s  [%s]" % (n, t, path, sample))

    bad = (sum(1 for r in rows if r[0] not in ("ok", "unreadable"))
           + len(lost_text))
    sys.exit(1 if bad else 0)


main()
