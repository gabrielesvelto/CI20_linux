#ifndef _DBG_H
#define _DBG_H

/* Diagnostic functions needed by lib/inflate.c */
#ifdef DEBUG
#  define Assert(cond,msg) { if (!(cond)) error(msg); }
#  define Trace(x) fprintf x
#  define Tracev(x) { fprintf x ; }
#  define Tracevv(x) { fprintf x ; }
#  define Tracec(c,x) { if (c) fprintf x ; }
#  define Tracecv(c,x) { if (c) fprintf x ; }
#else
#  define Assert(cond,msg)
#  define Trace(x)
#  define Tracev(x)
#  define Tracevv(x)
#  define Tracec(c,x)
#  define Tracecv(c,x)
#endif

extern void puts(const char *s);
extern void puthex(unsigned long long val);
extern int printf(char *fmt,...);
extern void error(char *x);

#endif
