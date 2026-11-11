# BestEdit

I learned to type on TextEdit. For a time it was my favorite place to
write. At some point I started preferring Markdown editors, first nvALT
then Ulysses. For some forms of writing, though, I can't stand Markdown.
The editors have too many distracting features. The font is always
monospace. It's difficult to visualize the form your words are taking.

I wanted TextEdit, but with a word count. Oh, and I suppose a timer
might be good motivation so I threw that in too. Figured C was tasteful
for the task at hand.

## Building

    make            # build/BestEdit.app
    make run        # build and launch
    make install    # copy to /Applications
    make test       # unit tests

The timer starts when you type and pauses after three minutes idle.
Writing > Start and Pause Automatically turns that off.

Statistics are in `~/Library/Application Support/BestEdit/stats.db`, set
`BESTEDIT_DB` to use another file.
