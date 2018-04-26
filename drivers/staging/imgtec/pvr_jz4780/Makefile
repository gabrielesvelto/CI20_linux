obj-$(CONFIG_POWERVR_JZ4780) += jz4780.o
jz4780-y += sysconfig.o sysutils.o
ccflags-y += -include $(srctree)/drivers/staging/imgtec/config_kernel.h \
 -I$(src) \
 -I$(srctree)/drivers/staging/imgtec \
 -I$(srctree)/drivers/staging/imgtec/sgx \
 -I$(srctree)/drivers/staging/imgtec/sgx/hwdefs
