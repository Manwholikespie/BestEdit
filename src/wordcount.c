/*
** 2026 November 11
**
** This file contains the code used to count words and characters.
**
** Text is supplied as UTF-16, which is the native encoding of the Cocoa
** text system, and may be supplied in as many pieces as is convenient.
** A piece boundary is allowed to fall anywhere, even in the middle of a
** word or between the two halves of a surrogate pair.
**
** A "word" is a maximal run of characters that are not separators and
** that contains at least one letter or digit.  Hence:
**
**     don't            1 word     Punctuation inside a word is part of it
**     well-known       1 word     So is a hyphen
**     wait - what      2 words    A dash standing alone is not a word
**     yes--no          1 word
**     yes(U+2014)no    2 words    But an em-dash does separate words
**     3.14             1 word
**
** This is the same answer that most word processors give.
**
** This file is portable C and makes no use of Cocoa.
*/
#include "bestedit.h"

/*
** Character classes.
*/
#define WC_SEP    0     /* Separates one word from the next */
#define WC_PUNCT  1     /* Does not separate words, nor make a word */
#define WC_WORD   2     /* A letter, digit, or other word-forming character */

/*
** Return the class of UTF-16 code unit c.
**
** Characters outside of the basic multilingual plane are encoded as a
** pair of surrogates.  All such characters are word-forming, so both
** halves of the pair are reported as WC_WORD and no attempt is made to
** combine them.
*/
static int wordClass(unsigned int c){
  if( c<0x80 ){
    if( c==' ' || (c>=0x09 && c<=0x0d) ) return WC_SEP;
    if( c>='0' && c<='9' ) return WC_WORD;
    if( (c|0x20)>='a' && (c|0x20)<='z' ) return WC_WORD;
    return WC_PUNCT;
  }
  if( c<0x2000 ){
    if( c==0x85 || c==0xa0 || c==0x1680 ) return WC_SEP;
    if( c<0xc0 ){
      /* Latin-1 symbols, except for the three that are letters */
      return (c==0xaa || c==0xb5 || c==0xba) ? WC_WORD : WC_PUNCT;
    }
    if( c==0xd7 || c==0xf7 ) return WC_PUNCT;
    return WC_WORD;
  }
  if( c<=0x200b ) return WC_SEP;               /* Spaces of various widths */
  if( c<0x2070 ){
    if( c==0x2014 || c==0x2015 ) return WC_SEP;      /* Em-dash */
    if( c==0x2028 || c==0x2029 ) return WC_SEP;      /* Line separators */
    if( c==0x202f || c==0x205f ) return WC_SEP;      /* More spaces */
    return WC_PUNCT;                                 /* Quotes, dashes, etc */
  }
  if( c==0x3000 ) return WC_SEP;               /* Ideographic space */
  if( c>=0x3001 && c<=0x3003 ) return WC_PUNCT;
  if( c>=0x3008 && c<=0x3020 ) return WC_PUNCT;
  if( c==0xfeff || c==0xfffc ) return WC_PUNCT;
  return WC_WORD;
}

/*
** Initialize a word counter.
*/
void beWordInit(WordCounter *p){
  p->nWord = 0;
  p->nChar = 0;
  p->bCounted = 0;
}

/*
** Add the n UTF-16 code units at a[] to the text counted by p.
**
** The character count is a count of Unicode code points, so the second
** half of a surrogate pair does not count.  Line breaks are not counted
** as characters either.
*/
void beWordFeed(WordCounter *p, const u16 *a, int n){
  int i;                          /* Loop counter */
  for(i=0; i<n; i++){
    unsigned int c = a[i];
    int eClass = wordClass(c);
    if( eClass==WC_SEP ){
      p->bCounted = 0;
      if( (c>=0x0a && c<=0x0d) || c==0x85 || c==0x2028 || c==0x2029 ){
        continue;
      }
    }else if( eClass==WC_WORD ){
      if( !p->bCounted ){
        p->nWord++;
        p->bCounted = 1;
      }
      if( (c & 0xfc00)==0xdc00 ) continue;
    }
    p->nChar++;
  }
}
