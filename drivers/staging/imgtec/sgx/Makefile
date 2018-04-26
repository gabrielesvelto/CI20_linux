ccflags-y += -include $(srctree)/drivers/staging/imgtec/config_kernel.h \
 -I$(srctree)/drivers/staging/imgtec \
 -I$(srctree)/$(src) \
 -I$(srctree)/$(src)/hwdefs
ccflags-$(CONFIG_X86) += -mno-soft-float

ccflags-$(CONFIG_POWERVR_JZ4780) += -I$(srctree)/drivers/staging/imgtec/pvr_jz4780

obj-$(CONFIG_POWERVR_SGX) += pvrsrvkm.o
obj-$(CONFIG_POWERVR_SGX_PDUMP) += dbgdrv.o

pvrsrvkm-y += bridged_pvr_bridge.o
pvrsrvkm-y += bridged_sgx_bridge.o
pvrsrvkm-y += bridged_support.o
pvrsrvkm-y += buffer_manager.o
pvrsrvkm-y += devicemem.o
pvrsrvkm-y += event.o
pvrsrvkm-y += handle.o
pvrsrvkm-y += hash.o
pvrsrvkm-y += lists.o
pvrsrvkm-y += mem.o
pvrsrvkm-y += mem_debug.o
pvrsrvkm-y += metrics.o
pvrsrvkm-y += mmap.o
pvrsrvkm-y += mm.o
pvrsrvkm-y += mmu.o
pvrsrvkm-y += module.o
pvrsrvkm-y += mutex.o
pvrsrvkm-y += mutils.o
pvrsrvkm-y += osfunc.o
pvrsrvkm-y += osfunc_common.o
pvrsrvkm-y += osperproc.o
pvrsrvkm-y += pb.o
pvrsrvkm-y += pdump.o
pvrsrvkm-y += pdump_common.o
pvrsrvkm-y += perproc.o
pvrsrvkm-y += power.o
pvrsrvkm-y += proc.o
pvrsrvkm-y += pvr_bridge_k.o
pvrsrvkm-y += pvr_debug.o
pvrsrvkm-y += pvrsrv.o
pvrsrvkm-y += ra.o
pvrsrvkm-y += refcount.o
pvrsrvkm-y += resman.o
pvrsrvkm-y += sgxinit.o
pvrsrvkm-y += sgxkick.o
pvrsrvkm-y += sgxpower.o
pvrsrvkm-y += sgxreset.o
pvrsrvkm-y += sgxtransfer.o
pvrsrvkm-y += sgxutils.o

# PDump
dbgdrv-y += dbgdriv_handle.o
dbgdrv-y += dbgdriv.o
dbgdrv-y += hotkey.o
dbgdrv-y += ioctl.o
dbgdrv-y += hostfunc.o
dbgdrv-y += main.o
ccflags-$(CONFIG_POWERVR_SGX_PDUMP) += -DPDUMP

pvrsrvkm-$(CONFIG_ION) += ion.o

pvrsrvkm-$(CONFIG_DRM) += pvr_drm.o
ccflags-$(CONFIG_DRM) += -I$(srctree)/include/drm

pvrsrvkm-$(CONFIG_DMA_SHARED_BUFFER) += dmabuf.o pvr_linux_fence.o

# GPU tracing in systrace
pvrsrvkm-$(CONFIG_GPU_TRACEPOINTS) += systrace.o
ccflags-$(CONFIG_GPU_TRACEPOINTS) += -DSUPPORT_PVRSRV_ANDROID_SYSTRACE

# Android native synchronisation
pvrsrvkm-$(CONFIG_SYNC) += pvr_sync.o

# Timed trace support
pvrsrvkm-$(CONFIG_POWERVR_SGX_TTRACE) += ttrace.o
