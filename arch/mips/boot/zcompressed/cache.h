#ifndef _CACHE_H_
#define _CACHE_H_

/*
 * Descriptor for a cache
 */
struct cache_desc {
	unsigned int waysize;	/* Bytes per way */
	unsigned short sets;	/* Number of lines per set */
	unsigned char ways;	/* Number of ways */
	unsigned char linesz;	/* Size of line in bytes */
	unsigned char waybit;	/* Bits to select in a cache set */
	unsigned char flags;	/* Flags describing cache properties */
};

struct cpuinfo_mips {
	struct cache_desc	icache; /* Primary I-cache */
	struct cache_desc	dcache; /* Primary D or combined I/D cache */
	struct cache_desc	scache; /* Secondary cache */
	struct cache_desc	tcache; /* Tertiary/split secondary cache */
};

#define IndexInvalidate_I       0x00
#define IndexWriteBack_D        0x01

#define INDEX_BASE 0x80000000

#define SYNC __asm__ __volatile__("sync \n" ::)

#define cache32_unroll32(base,op)					\
	__asm__ __volatile__(						\
	"	.set push					\n"	\
	"	.set noreorder					\n"	\
	"	.set mips3					\n"	\
	"	cache %1, 0x000(%0); cache %1, 0x020(%0)	\n"	\
	"	cache %1, 0x040(%0); cache %1, 0x060(%0)	\n"	\
	"	cache %1, 0x080(%0); cache %1, 0x0a0(%0)	\n"	\
	"	cache %1, 0x0c0(%0); cache %1, 0x0e0(%0)	\n"	\
	"	cache %1, 0x100(%0); cache %1, 0x120(%0)	\n"	\
	"	cache %1, 0x140(%0); cache %1, 0x160(%0)	\n"	\
	"	cache %1, 0x180(%0); cache %1, 0x1a0(%0)	\n"	\
	"	cache %1, 0x1c0(%0); cache %1, 0x1e0(%0)	\n"	\
	"	cache %1, 0x200(%0); cache %1, 0x220(%0)	\n"	\
	"	cache %1, 0x240(%0); cache %1, 0x260(%0)	\n"	\
	"	cache %1, 0x280(%0); cache %1, 0x2a0(%0)	\n"	\
	"	cache %1, 0x2c0(%0); cache %1, 0x2e0(%0)	\n"	\
	"	cache %1, 0x300(%0); cache %1, 0x320(%0)	\n"	\
	"	cache %1, 0x340(%0); cache %1, 0x360(%0)	\n"	\
	"	cache %1, 0x380(%0); cache %1, 0x3a0(%0)	\n"	\
	"	cache %1, 0x3c0(%0); cache %1, 0x3e0(%0)	\n"	\
	"	.set pop					\n"	\
		:							\
		: "r" (base),						\
		  "i" (op));

#define BUILD_BLAST_CACHE(desc, indexop, lsize)					\
static inline void blast_##desc##lsize(void) {					\
	unsigned long start = INDEX_BASE;					\
	unsigned long end = start + cpu_data.desc.waysize;			\
	unsigned long ws_inc = 1UL << cpu_data.desc.waybit;			\
	unsigned long ws_end = cpu_data.desc.ways << cpu_data.desc.waybit;	\
	unsigned long ws, addr;							\
										\
	for (ws = 0; ws < ws_end; ws += ws_inc)					\
		for (addr = start; addr < end; addr += lsize * 32)		\
			cache##lsize##_unroll32(addr|ws, indexop);		\
}

/* Each platform should provide necessary cache
 * configuration parameters through this structure
 */
extern struct cpuinfo_mips cpu_data;

/* This function is responsible for flushing
 * both data and instruction caches.
 * It's implementation is platform dependant,
 * take a look at cache.c
 */
extern void cache_flush_all(void);

#endif
