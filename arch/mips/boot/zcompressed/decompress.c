/*
 * linux/arch/mips/boot/compressed/misc.c
 *
 * This is a collection of several routines from gzip-1.0.3
 * adapted for Linux.
 *
 * malloc by Hannu Savolainen 1993 and Matthias Urlichs 1994
 *
 * Adapted for JZSOC by Peter Wei, 2008
 *
 */

#include "cache.h"
#include "dbg.h"

#define size_t	int
#define NULL 0

typedef unsigned char  uch;
typedef unsigned short ush;
typedef unsigned long  ulg;

/* Window size must be at least 32k,
 * and a power of two
 */
#define WSIZE		0x8000
#define HEAP_SIZE	0x10000

char *input_data;
int input_len;

static uch *inbuf;		/* input buffer */
static uch window[WSIZE];	/* Sliding window buffer */

static unsigned insize = 0;	/* valid bytes in inbuf */
static unsigned inptr = 0;	/* index of next byte to be processed in inbuf */
static unsigned outcnt = 0;	/* bytes in output buffer */
static long bytes_out = 0;
static uch *output_data;
static unsigned long output_ptr = 0;
static unsigned long free_mem_ptr;
static unsigned long free_mem_end_ptr;

extern unsigned char _end[];

/******************************************************************************
 * gzip declarations
 */
#define OF(args)  args
#define STATIC static

#define get_byte() (inptr < insize ? inbuf[inptr++] : fill_inbuf())
#define memzero(s, n) memset ((s), 0, (n))

static int  fill_inbuf(void);
static void flush_window(void);
static void* memset(void* s, int c, size_t n);
static void* memcpy(void* __dest, __const void* __src, size_t __n);

#include "../../../../lib/inflate.c"

/*****************************************************************************/

static void* memset(void* s, int c, size_t n)
{
	int i;
	char *ss = (char*)s;

	for (i=0; i < n; i++)
		ss[i] = c;

	return s;
}

static void* memcpy(void* __dest, __const void* __src, size_t __n)
{
	int i = 0;
	unsigned char *d = (unsigned char *)__dest, *s = (unsigned char *)__src;

	for (i = __n >> 3; i > 0; i--) {
		*d++ = *s++;
		*d++ = *s++;
		*d++ = *s++;
		*d++ = *s++;
		*d++ = *s++;
		*d++ = *s++;
		*d++ = *s++;
		*d++ = *s++;
	}

	if (__n & 1 << 2) {
		*d++ = *s++;
		*d++ = *s++;
		*d++ = *s++;
		*d++ = *s++;
	}

	if (__n & 1 << 1) {
		*d++ = *s++;
		*d++ = *s++;
	}

	if (__n & 1)
		*d++ = *s++;

	return __dest;
}

/* ===========================================================================
 * Fill the input buffer. This is called only when the buffer is empty
 * and at least one byte is really needed.
 */
static int fill_inbuf(void)
{
	if (insize != 0) {
		error("ran out of input data\n");
	}

	inbuf = input_data;
	insize = input_len;
	inptr = 1;

	return inbuf[0];
}

/* ===========================================================================
 * Write the output window window[0..outcnt-1] and update crc and bytes_out.
 * (Used for the decompressed data only.)
 */
static void flush_window(void)
{
	ulg c = crc; /* temporary variable */
	unsigned n;
	uch *in, *out, ch;

	in = window;
	out = &output_data[output_ptr];
	for (n = 0; n < outcnt; n++) {
		ch = *out++ = *in++;
		c = crc_32_tab[((int)c ^ ch) & 0xff] ^ (c >> 8);
	}
	crc = c;
	bytes_out += (ulg)outcnt;
	output_ptr += (ulg)outcnt;
	outcnt = 0;
}

void decompress_kernel(unsigned int imageaddr, unsigned int imagesize, unsigned int loadaddr)
{
	input_data = (char *)imageaddr;
	input_len = imagesize;
	output_ptr = 0;
	output_data = (uch *)loadaddr;
	free_mem_ptr = (unsigned long)_end;
	free_mem_end_ptr = free_mem_ptr + HEAP_SIZE;

	makecrc();
	puts("Uncompressing Linux...\n");
	gunzip();
	cache_flush_all();
	puts("Ok, booting the kernel.\n");
}
