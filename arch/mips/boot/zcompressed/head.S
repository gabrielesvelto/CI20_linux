/*
 *  linux/arch/mips/boot/compressed/head.S
 *
 *  Copyright (C) 2005-2008 Ingenic Semiconductor Inc.
 */

#include <asm/asm.h>
#include <asm/cacheops.h>
#include <asm/cachectl.h>
#include <asm/regdef.h>

#define IndexInvalidate_I       0x00
#define IndexWriteBack_D        0x01

	.set noreorder
	LEAF(startup)
startup:

	move	s0, a0		/* Save the boot loader transfered args */
	move	s1, a1
	move	s2, a2
	move	s3, a3

	la	a0, _edata
	la	a1, _end
1:	sw	zero, 0(a0)	/* Clear BSS section */
	bne	a1, a0, 1b
	addu	a0, 4

	la	sp, (.stack + 8192)

	la	a0, __image_begin
	la	a1, IMAGESIZE
	la	a2, LOADADDR
	la	ra, 1f
	la	k0, decompress_kernel
	jr	k0
	nop
1:

	move	a0, s0
	move	a1, s1
	move	a2, s2
	move	a3, s3

	li	k0, KERNEL_ENTRY

	jr	k0
	nop

2:
	b 2b

	END(startup)

	.comm .stack,4096*2,4
