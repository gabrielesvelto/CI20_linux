#
# linux/arch/mips/boot/compressed/Makefile
#
# create a compressed zImage from the original vmlinux
#

targets		:= zImage uImage vmlinuz vmlinux.bin.gz head.o decompress.o piggy.o dummy.o

# Disable Function Tracer
KBUILD_CFLAGS := $(shell echo $(KBUILD_CFLAGS) | sed -e "s/-pg//")
KBUILD_CFLAGS := $(filter-out -fstack-protector, $(KBUILD_CFLAGS))

OBJS 		:= $(obj)/head.o $(obj)/decompress.o $(obj)/cache.o $(obj)/dbg.o

ifdef CONFIG_DEBUG_ZBOOT
OBJS += $(obj)/uart-16550.o
endif

LD_ARGS 	:= -T $(srctree)/$(src)/ld.script -Ttext 0x81F00000 -Bstatic -melf32ltsmip
OBJCOPY_ARGS 	:= -O elf32-tradlittlemips

ENTRY 		:= $(srctree)/$(src)/entry
FILESIZE 	:= $(srctree)/$(src)/filesize

drop-sections	= .reginfo .mdebug .comment .note .pdr .options .MIPS.options
strip-flags	= $(addprefix --remove-section=,$(drop-sections))

LOADADDR=0x80010000

$(obj)/vmlinux.bin.gz: vmlinux
	rm -f $(obj)/vmlinux.bin.gz
	$(OBJCOPY) -O binary $(strip-flags) vmlinux $(obj)/vmlinux.bin
	gzip -v9f $(obj)/vmlinux.bin

$(obj)/head.o: $(obj)/head.S $(obj)/vmlinux.bin.gz vmlinux
	$(CC) $(KBUILD_AFLAGS) $(a_flags) \
	-DIMAGESIZE=$(shell sh $(FILESIZE) $(obj)/vmlinux.bin.gz) \
	-DKERNEL_ENTRY=$(shell sh $(ENTRY) $(NM) vmlinux ) \
	-DLOADADDR=0x80010000 \
	-I./include \
	-c -o $(obj)/head.o $<

$(obj)/vmlinuz: $(OBJS) $(obj)/ld.script $(obj)/vmlinux.bin.gz $(obj)/dummy.o
	$(OBJCOPY) \
		--add-section=.image=$(obj)/vmlinux.bin.gz \
		--set-section-flags=.image=contents,alloc,load,readonly,data \
		$(obj)/dummy.o $(obj)/piggy.o
	$(LD) $(LD_ARGS) -o $@ $(OBJS) $(obj)/piggy.o
	$(OBJCOPY) $(OBJCOPY_ARGS) $@ $@ -R .comment -R .stab -R .stabstr -R .initrd -R .sysmap

zImage: $(obj)/vmlinuz
	$(OBJCOPY) -O binary $(obj)/vmlinuz $(obj)/zImage
	@rm -f $(obj)/vmlinuz

vmlinux.bin: $(VMLINUX)
	$(OBJCOPY) -O binary $(strip-flags) $(VMLINUX) $(obj)/vmlinux.bin

uImage: $(VMLINUX) vmlinux.bin
	rm -f $(obj)/vmlinux.bin.gz
	gzip -9 $(obj)/vmlinux.bin
	mkimage -A mips -O linux -T kernel -C gzip \
		-a $(LOADADDR) -e $(shell sh ./$(obj)/tools/entry $(NM) $(VMLINUX) ) \
		-n 'Linux-$(KERNELRELEASE)' \
		-d $(obj)/vmlinux.bin.gz $(obj)/uImage
