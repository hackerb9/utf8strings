#include <stdio.h>		/* getc, ungetc, putchar() */
#include <ctype.h>		/* isprint() */
#include <errno.h>		/* errno */
#include <stdlib.h>		/* exit() */

void checkandprint(int c, int count);
int istoobig(char bytes[]);
int issurrogate(char bytes[]);
void maybeputchar(int c);
void maybeputsequence(char b[]);
void resetfound();


static FILE *fp;		/* File stream to read from */
int found=0;			/* Num printable chars found in a row so far */
int minlen=4;			/* Minimum number before we'll print */

char *buf; /* Null terminated string of UTF-8 bytes to maybe output */
int idx=0; /* Index into buf */
int neednewline=0;		/* Have we already printed a string?  */

int main(int argc, char *argv[]) {
  /* Read stdin (or a file) and print out only the valid UTF-8 strings. */

  fp=stdin;
  if (argc>1) fp=fopen(argv[1], "r");
  if (fp == NULL) { perror(argv[1]); exit(1); }

  buf=malloc(minlen*4+1);
  if (buf == NULL) { perror("buf[] to hold potential output"); exit(1); }

  int c;
  while ((c=getc(fp)) != EOF) {
    if (isprint(c) || c=='\n') { maybeputchar(c); continue; } /* ASCII text */
    if (c<0x80) goto fail;			 /* Unprintable ASCII */
    if (c<0xA0) goto fail;			 /* Unicode control chars */
    if (c==0xC0 || c==0xC1 || c>=0xF5) goto fail; /* Illegal values in UTF-8 */

    /* Print this byte only if next n bytes begin with 0b10 */
    if (c>>5 == 0b110   && (c&0b11111) != 0) { checkandprint(c, 1); continue; }
    if (c>>4 == 0b1110  && (c&0b1111) != 0)  { checkandprint(c, 2); continue; }
    if (c>>3 == 0b11110 && (c&0b111) != 0)   { checkandprint(c, 3); continue; }

  fail:
    resetfound();
  }

  if (neednewline) puts("");
  return errno;
}

void maybeputbuf() {
  buf[idx]='\0';
  if (found==minlen && neednewline)  puts("");
  if (found>=minlen) {
    fputs(buf, stdout);
    if (buf[idx-1] != '\n') neednewline=1;
    else neednewline=0;
    idx=0;
  }    
}

void maybeputchar(int c) {
  // Found an ASCII character. Add it to the output buffer.
  found++;
  buf[idx++]=c;
  buf[idx]='\0';
  maybeputbuf();
}

void maybeputsequence(char *b) {
  // Found a valid UTF-8 sequence. Add it to the output buffer.
  found++;
  while (*b) {
    buf[idx++]=*b++;
  }
  maybeputbuf();
}

void resetfound() {
  found=0;
  idx=0;
}

void checkandprint(int c, int count) {
  /* Given a character 'c', check the next 'count' bytes if they are
   * valid UTF-8 continuation bytes.  (That is, the top two bits are 1-0). 
   * If all are valid, then print 'c', followed by the bytes. */
  char bytes[5];
  bytes[0]=c;
  int i=1;
  while (count--) {
    if ((c=getc(fp)) == EOF) {
      resetfound();
      return;
    }
    if (c>>6 != 0b10) {		/* Invalid continuation byte? */
      ungetc(c, fp);		/* ...but maybe it's fine on its own */
      resetfound();
      return;
    }
    bytes[i++]=c;
  }

  /* Skip UTF-16's surrogate halves. Skip codepoints greater than U+10FFFF */
  if (issurrogate(bytes)||istoobig(bytes)) {
    resetfound();
    return;
  }

  /* Success. Print out all read bytes. */
  bytes[i]='\0';
  maybeputsequence(bytes);
}


int issurrogate(char bytes[]) {
  /* Return true is the code point is within the range 
   * U+D800 to U+DFFF, inclusive. 
   * 0xD7FF is 1101 011111 111111, 
   * so check this  ^  bit.
   */
  return ( bytes[0]==0xED  &&  ((bytes[1] & 0b111111)>>5)==1 );
}

int istoobig(char bytes[]) { 
  /* Return true if the code point is greater than U+10FFFF
   * (100 001111 111111 111111) 
   */
  return ( bytes[0]==0xF4  &&  ((bytes[1] & 0b111111)>>4) > 0 );
}

/* 
   NOTES

A. INVALID UTF-8 SEQUENCES, such as below, are correctly discarded:
   1. Bytes that don't begin with UTF's magic (10*, 110*, 1110*, or 11110*).
   2. A byte with the correct magic bits, but all 0s for data. (E.g., 11110000).
   3. Incorrect usage of continuation bytes (10*) 
      a. After 110*, there must be 1 continuation byte.
      b. After 1110*, there must be 2 continuation bytes.
      c. After 11110*, there must be 3 continuation bytes.
      d. Continuation bytes (10*) not preceeded by one of the above are invalid.
   4. Bytes C0 and C1. (They would encode ASCII as two bytes).
   5. U+D800 to U+DFFF are reserved for UTF-16's surrogate halves.
   6. Leading byte of F4 and character decodes beyond Unicode's limit. (>0x10FFFF)
   7. Leading byte of F5 to FD. (Always greater than 0x10FFFF).
   8. Leading byte of FE or FF. (Undefined in UTF-8 to allow for UTF-16 BOM).
   9. End of file before a complete character is read.
  10. U+80 to U+95, although valid Unicode, are skipped as control characters.

B. HOWEVER, IT COULD BE BETTER. Some valid UTF-8 sequences are
   actually undefined code points in Unicode and shouldn't be printed.
   Similarly, for a `strings` program like this, we would want to
   check Unicode's syntactic tables so we can ignore non-printable
   characters. These have been left out intentionally as they would
   require updating with every new release of the Unicode standard.

C. SOME TESTS:
   1a. Values beyond Unicode (>= 0x110000) should show nothing:
       echo -n $'\xf4\x90\x80\x80' | ./utf8strings  | hd

   1b. Characters <= 0x10FFFF should show something:
       echo -n $'\xf4\x8f\xbf\xbf' | ./utf8strings  | hd

   2a. UTF-16 surrogate halves should not be shown:
       echo -n $'\xED\xA0\x80' | ./utf8strings | hd

   2b. Characters between U+D000 to U+D7FF should be shown:
       echo -n $'\xED\x9F\xBF' | ./utf8strings | hd

 */
