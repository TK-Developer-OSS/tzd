/* TzD ctype (libc2d mode, C locale).  The argument must be EOF or an
   unsigned char value; anything else traps (no out-of-table read).  */
#ifndef _TZD_CTYPE_H
#define _TZD_CTYPE_H
int isalnum(int c)  __asm__("tzd_isalnum");
int isalpha(int c)  __asm__("tzd_isalpha");
int iscntrl(int c)  __asm__("tzd_iscntrl");
int isdigit(int c)  __asm__("tzd_isdigit");
int isgraph(int c)  __asm__("tzd_isgraph");
int islower(int c)  __asm__("tzd_islower");
int isprint(int c)  __asm__("tzd_isprint");
int ispunct(int c)  __asm__("tzd_ispunct");
int isspace(int c)  __asm__("tzd_isspace");
int isupper(int c)  __asm__("tzd_isupper");
int isxdigit(int c) __asm__("tzd_isxdigit");
int tolower(int c)  __asm__("tzd_tolower");
int toupper(int c)  __asm__("tzd_toupper");
#endif
