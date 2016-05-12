obj-$(CONFIG_POWERVR_CI20) += ci20.o
ci20-y += sysconfig.o sysutils.o
ccflags-y += -include $(srctree)/drivers/staging/imgtec/config_kernel.h \
 -I$(src) \
 -I$(srctree)/drivers/staging/imgtec \
 -I$(srctree)/drivers/staging/imgtec/sgx \
 -I$(srctree)/drivers/staging/imgtec/sgx/hwdefs
