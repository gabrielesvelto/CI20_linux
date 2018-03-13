/*
 * MIPS-specific debug support for pre-boot environment
 *
 * NOTE: putc() is board specific, if your board have a 16550 compatible uart,
 * please select SYS_SUPPORTS_ZBOOT_UART16550 for your machine. othewise, you
 * need to implement your own putc().
 */
#include <linux/compiler.h>
#include <linux/types.h>
#include <stdarg.h>

void __weak putc(char c)
{
}

void puts(const char *s) {
	char c;
	while ((c = *s++) != '\0') {
		putc(c);
		if (c == '\n')
			putc('\r');
	}
}

void puthex(unsigned long long val) {

	unsigned char buf[10];
	int i;
	for (i = 7; i >= 0; i--) {
		buf[i] = "0123456789ABCDEF"[val & 0x0F];
		val >>= 4;
	}
	buf[8] = '\0';
	puts(buf);
}

void error(char *x) {
	puts("\n\n");
	puts(x);
	puts("\n\n -- System halted");

	while(1);	/* Halt */
}

static int hex2asc(int n) {
	n &= 15;
	if (n > 9) {
		return ('a' - 10) + n;
	} else {
		return '0' + n;
	}
}

int printf(char *fmt,...) {
	va_list ap;
	char scratch[16];
	va_start(ap,fmt);

	for(;;) {
		switch (*fmt) {
		case 0:
			va_end(ap);
			return 0;
		case '%':
			switch (fmt[1]) {
				case 'p':
				case 'X':
				case 'x':
				{
					unsigned n = va_arg(ap, unsigned);
					char *p = scratch + 15;
					*p = 0;
					do {
						*--p = hex2asc(n);
						n = n >> 4;
					} while(n != 0);
					while(p > (scratch + 7)) *--p = '0';
					while (*p) putc(*p++);
					fmt += 2;
					continue;
				}
				case 'd':
				{
					int n = va_arg(ap, int);
					char *p = scratch + 15;
					*p = 0;
					if (n < 0) {
						putc('-');
						n = -n;
					}
					do {
						*--p = (n % 10) + '0';
						n /= 10;
					} while(n != 0);
					while (*p) putc(*p++);
					fmt += 2;
					continue;
				}
				case 's':
				{
					char *s = va_arg(ap, char*);
					if(s == 0) s = "(null)";
					while (*s) putc(*s++);
					fmt += 2;
					continue;
				}
			}
			putc(*fmt++);
			break;
		case '\n':
			putc('\r');
		default:
			putc(*fmt++);
		}
	}
}
