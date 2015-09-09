ccflags-y = -DLINUX -D__linux__ -DANDROID -DPVR_BUILD_DIR="\"xb4780_android\""
ccflags-y += -Idrivers/gpu/pvr -Idrivers/gpu/pvr/xb4780 -Idrivers/video/jz4780-fb

ccflags-y += \
	-DSUPPORT_SGX \
	-DTRANSFER_QUEUE \
	-DSGX_DYNAMIC_TIMING_INFO \
	-DSUPPORT_HW_RECOVERY \
	-DLDM_PLATFORM \
	-DSUPPORT_SGX_NEW_STATUS_VALS \
	-DSUPPORT_SGX_HWPERF \
	-DSUPPORT_PVRSRV_DEVICE_CLASS \
	-DSYS_USING_INTERRUPTS \
	-DPVR_SECURE_HANDLES \
	-DPVR_LINUX_USING_WORKQUEUES \
	-DPVR_LINUX_MISR_USING_PRIVATE_WORKQUEUE \
	-DPVR_LINUX_TIMERS_USING_WORKQUEUES \
	-DSYS_CUSTOM_POWERLOCK_WRAP \
	-DSUPPORT_MEMINFO_IDS \
	-DDISPLAY_CONTROLLER=dc_xblfb \
	-DPVR_LDM_PLATFORM_PRE_REGISTERED \
	-DPVRSRV_MODNAME="\"pvrsrvkm\"" \
	-DPVR_LDM_DRIVER_REGISTRATION_NAME="\"pvrsrvkm\"" \
	-DPVR_LDM_PLATFORM_PRE_REGISTERED_DEV="\"pvrsrvkm\"" \
	-DSUPPORT_DC_CMDCOMPLETE_WHEN_NO_LONGER_DISPLAYED \
	-DSUPPORT_LARGE_GENERAL_HEAP \
	-DSYS_SUPPORTS_SGX_IDLE_CALLBACK \
	-DPVRSRV_NEW_PVR_DPF \
	-DPVR_ANDROID_NATIVE_WINDOW_HAS_SYNC \
	-DSUPPORT_LISR_MISR_SYNC \
	-DSUPPORT_ION_HEADER="\"linux/ion.h\"" \
	-DSUPPORT_ION_PRIV_HEADER="\"../drivers/gpu/ion/ion_priv.h\"" \
	-Idrivers/gpu/pvr/sgx

# optional page pool feature
#ccflags-y += \
#	-DPVR_LINUX_MEM_AREA_POOL_MAX_PAGES=5400 \
#	-DPVR_LINUX_MEM_AREA_USE_VMAP \
#	-DPVR_LINUX_MEM_AREA_POOL_ALLOW_SHRINK

ccflags-$(CONFIG_PVR_SGXCORE_540) += \
	-DSGX540 -DSUPPORT_SGX540 \
	-DSGX_CORE_REV=130 \
	-DSGX_FEATURE_SYSTEM_CACHE

ccflags-$(CONFIG_PVR_BUILD_RELEASE) += \
	-DPVR_BUILD_TYPE="\"release\"" -DRELEASE

ccflags-$(CONFIG_PVR_BUILD_DEBUG) += \
	-DPVR_BUILD_TYPE="\"debug\"" -DDEBUG

ccflags-$(CONFIG_PVR_NEED_PVR_DPF) += -DPVRSRV_NEED_PVR_DPF -DPVRSRV_NEED_PVR_TRACE
ccflags-$(CONFIG_PVR_NEED_PVR_ASSERT) += -DPVRSRV_NEED_PVR_ASSERT

ccflags-$(CONFIG_PVR_DEBUG_MEMORY) += \
	-DDEBUG_LINUX_MEMORY_ALLOCATIONS \
	-DDEBUG_LINUX_MEM_AREAS \
	-DDEBUG_LINUX_MMAP_AREAS

ccflags-$(CONFIG_PVR_DEBUG_BRIDGE_KM) += -DDEBUG_BRIDGE_KM
ccflags-$(CONFIG_PVR_DEBUG_TRACE_BRIDGE_KM) += -DDEBUG_TRACE_BRIDGE_KM
ccflags-$(CONFIG_PVR_DEBUG_BRIDGE_KM_DISPATCH_TABLE) += -DDEBUG_BRIDGE_KM_DISPATCH_TABLE

ccflags-$(CONFIG_PVR_PERCONTEXT_PB) += -DSUPPORT_PERCONTEXT_PB
ccflags-$(CONFIG_PVR_SGX_LOW_LATENCY_SCHEDULING) += -DSUPPORT_SGX_LOW_LATENCY_SCHEDULING
ccflags-$(CONFIG_PVR_ACTIVE_POWER_MANAGEMENT) += -DSUPPORT_ACTIVE_POWER_MANAGEMENT
ccflags-$(CONFIG_PVR_ACTIVE_POWER_MANAGEMENT) += \
	-DSYS_SGX_ACTIVE_POWER_LATENCY_MS=CONFIG_PVR_ACTIVE_POWER_LATENCY_MS \
	-DSYS_SGX_ACTIVE_POWER_POWER_DOWN_LATENCY_MS=CONFIG_PVR_ACTIVE_POWER_POWER_DOWN_LATENCY_MS

ccflags-$(CONFIG_PVR_USSE_EDM_STATUS_DEBUG) += -DPVRSRV_USSE_EDM_STATUS_DEBUG
ccflags-$(CONFIG_PVR_DUMP_MK_TRACE) += -DPVRSRV_DUMP_MK_TRACE

ccflags-$(CONFIG_PVR_PDUMP) += \
	-DPDUMP -DSUPPORT_DBGDRV_EVENT_OBJECTS -DSUPPORT_PDUMP_MULTI_PROCESS

pvrsrvkm-y := \
	osfunc.o \
	mutils.o \
	mmap.o \
	module.o \
	pdump.o \
	proc.o \
	pvr_bridge_k.o \
	pvr_debug.o \
	pvr_sync.o \
	mm.o \
	mutex.o \
	event.o \
	osperproc.o \
	buffer_manager.o \
	devicemem.o \
	deviceclass.o \
	handle.o \
	hash.o \
	metrics.o \
	pvrsrv.o \
	queue.o \
	ra.o \
	resman.o \
	power.o \
	mem.o \
	pdump_common.o \
	bridged_support.o \
	bridged_pvr_bridge.o \
	perproc.o \
	lists.o \
	xb4780/sysconfig.o \
	xb4780/sysutils.o \
	sgx/bridged_sgx_bridge.o \
	sgx/sgxinit.o \
	sgx/sgxpower.o \
	sgx/sgxreset.o \
	sgx/sgxutils.o \
	sgx/sgxkick.o \
	sgx/sgxtransfer.o \
	sgx/mmu.o \
	sgx/pb.o

pvrsrvkm-$(CONFIG_ION_XBURST) += ion.o
ccflags-$(CONFIG_ION_XBURST) += -DSUPPORT_ION

xblfb-y := \
	xblfb/xblfb_displayclass.o \
	xblfb/xblfb_linux.o

obj-$(CONFIG_PVR_SGX) += pvrsrvkm.o
obj-$(CONFIG_PVR_SGX) += xblfb.o
