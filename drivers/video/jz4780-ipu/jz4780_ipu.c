/* kernel/drivers/video/jz4780/input/jz4780_ipu.c
 *
 * Copyright (c) 2012 Ingenic Semiconductor Co., Ltd.
 *              http://www.ingenic.com/
 *
 * Input file for Ingenic IPU driver
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

/*#define DEBUG*/
#include <linux/module.h>
#include <linux/kernel.h>

#include <linux/errno.h>
#include <linux/clk.h>
#include <linux/mutex.h>
#include <linux/poll.h>
#include <linux/wait.h>
#include <linux/fs.h>
#include <linux/irq.h>
#include <linux/mm.h>
#include <linux/fb.h>
#include <linux/ctype.h>
#include <linux/dma-mapping.h>
#include <linux/interrupt.h>
#include <linux/platform_device.h>
#include <linux/io.h>
#include <linux/memory.h>
#include <linux/suspend.h>
#include <linux/miscdevice.h>
#include <linux/proc_fs.h>
#include <linux/delay.h>
#include <dt-bindings/clock/jzcpm_pwc.h>
#include <dt-bindings/soc/jz4780/libdmmu.h>
#include <dt-bindings/soc/jz4780/cpm.h>
#include <dt-bindings/soc/jz4780/base.h>
#include <dt-bindings/soc/jz4780/libdmmu.h>

#include "regs_ipu.h"
#include "jz4780_ipu.h"

#define HARB0_PRIOR (0xB3000000)
#define REG_HARB_PRIOR *(volatile unsigned int *)(HARB0_PRIOR)
/*#define IPU_DEBUG*/

#ifdef PHYS
#undef PHYS
#endif

#define PHYS(x) (x)

static int ipu0_nodirect;
static int ipu1_nodirect;
static int ipu0_direct;
static int ipu1_direct;
static struct mutex ipu_lock;
static struct of_device_id jz4780_of_match[];

static struct ipu_proc_info *get_ipu_procinfo(struct jz_ipu *ipu, struct file *filp);

struct ipu_reg_struct jz47_ipu_regs_name[] = {
	{"IPU_FM_CTRL",	        IPU_FM_CTRL},
	{"IPU_STATUS",	        IPU_STATUS},
	{"IPU_D_FMT",	        IPU_D_FMT},
	{"IPU_Y_ADDR",	        IPU_Y_ADDR},
	{"IPU_U_ADDR",	        IPU_U_ADDR},
	{"IPU_V_ADDR",	        IPU_V_ADDR},
	{"IPU_IN_FM_GS",        IPU_IN_FM_GS},
	{"IPU_Y_STRIDE",        IPU_Y_STRIDE},
	{"IPU_UV_STRIDE",       IPU_UV_STRIDE},
	{"IPU_OUT_ADDR",        IPU_OUT_ADDR},
	{"IPU_OUT_GS",	        IPU_OUT_GS},
	{"IPU_OUT_STRIDE",      IPU_OUT_STRIDE},
	{"RSZ_COEF_INDEX",      IPU_RSZ_COEF_INDEX},
	{"IPU_CSC_C0_COEF",     IPU_CSC_C0_COEF},
	{"IPU_CSC_C1_COEF",     IPU_CSC_C1_COEF},
	{"IPU_CSC_C2_COEF",     IPU_CSC_C2_COEF},
	{"IPU_CSC_C3_COEF",     IPU_CSC_C3_COEF},
	{"HRSZ_LUT_BASE",       HRSZ_LUT_BASE}, /* write only */
	{"VRSZ_LUT_BASE",       VRSZ_LUT_BASE}, /* write only */
	{"IPU_CSC_OFFSET_PARA", IPU_CSC_OFFSET_PARA},
	{"IPU_SRC_TLB_ADDR",    IPU_SRC_TLB_ADDR},
	{"IPU_DEST_TLB_ADDR",   IPU_DEST_TLB_ADDR},
	{"IPU_ADDR_CTRL",       IPU_ADDR_CTRL},
	{"IPU_Y_ADDR_N",        IPU_Y_ADDR_N},
	{"IPU_U_ADDR_N",        IPU_U_ADDR_N},
	{"IPU_V_ADDR_N",        IPU_V_ADDR_N},
	{"IPU_OUT_ADDR_N",      IPU_OUT_ADDR_N},
	{"IPU_SRC_TLB_ADDR_N",  IPU_SRC_TLB_ADDR_N},
	{"IPU_DEST_TLB_ADDR_N", IPU_DEST_TLB_ADDR_N},
	{"IPU_TRIG",            IPU_TRIG},
	{"IPU_FM_XYOFT",        IPU_FM_XYOFT},
	{"IPU_GLB_CTRL",        IPU_GLB_CTRL},
	{"IPU_OSD_CTRL",        IPU_OSD_CTRL},
};

void dump_cpm(void)
{
	printk("clk_gr0 (BIT29)  = 0x%08x\n", *((volatile unsigned int *)0xB0000020));
	printk("lpcr    (BIT30) = 0x%08x\n", *((volatile unsigned int *)0xB0000054));
	/*printk("cgu_vpu         = 0x%08x\n", *((volatile unsigned int *)0xB0000030));*/
	printk("cgu_lcd0  = 0x%08x\n", *((volatile unsigned int *)0xB0000064));
	printk("cgu_lcd1  = 0x%08x\n", *((volatile unsigned int *)0xB0000054));

	printk("opcr (reg)      = 0x%08x\n", *((volatile unsigned int *)0xB0000024));
	printk("opcr (cpm_inl)  = 0x%08x\n", (cpm_inl(CPM_OPCR)));

	printk("srbc (reg)      = 0x%08x\n",*((volatile unsigned int *)0xB00000c4));
	printk("srbc (cpm_inl)	= 0x%08x\n",cpm_inl(CPM_SRBC));

	printk("mpll (reg)      = 0x%08x\n",*((volatile unsigned int *)0xB0000014));
	printk("cpm_pwc0 (ipu_bit9,lcd_bit10)= 0x%08x\n",*((volatile unsigned int *)0xB00000b8));

}

/* max timeout 100ms */
static inline int jz47_ipu_wait_frame_end_flag(struct jz_ipu *ipu)
{
	unsigned long long clock_start;
	unsigned long long clock_now;
	unsigned int ipu_frame_end;

	clock_start = sched_clock();
	while ( 1 ) {
		ipu_frame_end = (reg_read(ipu, IPU_STATUS) & OUT_END);
		if (ipu_frame_end)
			break;
		clock_now = sched_clock();
		if ((clock_now - clock_start) > (30 * 1000000)) { /* timeout: 30ms */
			dev_err(ipu->dev, "jz47_ipu_wait_frame_end_flag() timeout....\n");
			return -1;	/* wait the end flag */
		}
	}

	return 0;
}

static void bit_set(struct jz_ipu *ipu, int offset, unsigned int bit)
{
	unsigned int tmp = 0;

	tmp = reg_read(ipu, offset);
	tmp |= bit;
	reg_write(ipu, offset, tmp);
}

static void bit_clr(struct jz_ipu *ipu, int offset, unsigned int bit)
{
	unsigned int tmp = 0;

	tmp = reg_read(ipu, offset);
	tmp &= ~(bit);
	reg_write(ipu, offset, tmp);
}

static void set_hrsz_lut_weigth_cube(struct jz_ipu *ipu, struct ipu_proc_info *ipu_proc)
{
	int i;
	unsigned int tmp;
	int *oft_table;
	struct ipu_table *table = &ipu_proc->table;

	oft_table = &table->hoft[1];
	tmp = 1 << START_N_SFT;
	reg_write(ipu, HRSZ_LUT_BASE, tmp);
	for (i=0;i<ipu_proc->img.hcoef_real_heiht;i++) {
		tmp = ((table->cube_hcoef[i][0] & W_COEF_20_MSK)<<W_COEF_20_SFT) |
			((table->cube_hcoef[i][1] & W_COEF_31_MSK)<<W_COEF_31_SFT);
		reg_write(ipu, HRSZ_LUT_BASE, tmp);
		tmp = ((table->cube_hcoef[i][2] & W_COEF_20_MSK)<<W_COEF_20_SFT) |
			(((table->cube_hcoef[i][3]) & W_COEF_31_MSK)<<W_COEF_31_SFT) |
			((oft_table[i] & HRSZ_OFT_MSK) << HRSZ_OFT_SFT);
		reg_write(ipu, HRSZ_LUT_BASE, tmp);
	}
}

static void set_vrsz_lut_weigth_cube(struct jz_ipu *ipu, struct ipu_proc_info *ipu_proc)
{
	int i;
	unsigned int tmp;
	int *oft_table;
	struct ipu_table *table = &ipu_proc->table;

	oft_table = &table->voft[1];
	tmp = 1 << START_N_SFT;
	reg_write(ipu, VRSZ_LUT_BASE, tmp);
	for (i=0;i<ipu_proc->img.vcoef_real_heiht;i++) {
		tmp = ((table->cube_vcoef[i][0] & W_COEF_20_MSK)<<W_COEF_20_SFT) |
			((table->cube_vcoef[i][1] & W_COEF_31_MSK)<<W_COEF_31_SFT);
		reg_write(ipu, VRSZ_LUT_BASE, tmp);
		tmp = ((table->cube_vcoef[i][2] & W_COEF_20_MSK)<<W_COEF_20_SFT) |
			((table->cube_vcoef[i][3] & W_COEF_31_MSK)<<W_COEF_31_SFT) |
			((oft_table[i] & VRSZ_OFT_MSK) << VRSZ_OFT_SFT);
		reg_write(ipu, VRSZ_LUT_BASE, tmp);
	}
}

static void set_vrsz_lut_coef_line(struct jz_ipu *ipu, struct ipu_proc_info *ipu_proc)
{
	int i;
	unsigned int tmp;
	int *oft_table, *coef_table;
	struct ipu_table *table = &ipu_proc->table;
	struct ipu_img_param *img = &ipu_proc->img;

	oft_table = &table->voft[1];
	coef_table = &table->vcoef[1];

	tmp = 1 << START_N_SFT;
	reg_write(ipu, VRSZ_LUT_BASE, tmp);
	for (i = 0; i < img->vcoef_real_heiht; i++) {
		tmp = ((coef_table[i]&W_COEF0_MSK)<< W_COEF0_SFT) |
			((oft_table[i]&V_OFT_MSK) << V_CONF_SFT);
		reg_write(ipu, VRSZ_LUT_BASE, tmp);
	}
}

static void set_hrsz_lut_coef_line(struct jz_ipu *ipu, struct ipu_proc_info *ipu_proc)
{
	int i;
	unsigned int tmp;
	int *oft_table, *coef_table;
	struct ipu_table *table = &ipu_proc->table;
	struct ipu_img_param *img = &ipu_proc->img;

	oft_table = &table->hoft[1];
	coef_table = &table->hcoef[1];

	tmp = (1 << START_N_SFT);
	reg_write(ipu, HRSZ_LUT_BASE, tmp);
	for (i = 0; i < img->hcoef_real_heiht; i++) {
		tmp = ((coef_table[i]&W_COEF0_MSK)<< W_COEF0_SFT) |
			((oft_table[i]&H_OFT_MSK) << H_CONF_SFT);
		reg_write(ipu, HRSZ_LUT_BASE, tmp);
	}
}

static void set_gs_regs(struct jz_ipu *ipu,int Wdiff,int Hdiff,int outW,int outH)
{
	unsigned int tmp;
	unsigned int tmp1;
	struct ipu_proc_info *ipu_proc = NULL;

	dev_dbg(ipu->dev, "%s %d", __func__, __LINE__);
	ipu_proc = get_ipu_procinfo(ipu, ipu->cur_proc);
	if (!ipu_proc) {
		dev_err(ipu->dev, "get_ipu_procinfo failed! %d", __LINE__);
		return;
	}

	dev_dbg(ipu->dev, "outW = %d, outH = %d\n",  outW, outH);
	tmp1 =ipu_proc->img.in_width - Wdiff;
	dev_dbg(ipu->dev, "ipu_proc->img.in_width = %d, ipu_proc->img.in_height = %d, tmp1=%d \n",ipu_proc->img.in_width,ipu_proc->img.in_height,tmp1);
	tmp = IN_FM_W(tmp1) | IN_FM_H((ipu_proc->img.in_height - Hdiff) & ~0x1);
	reg_write(ipu, IPU_IN_FM_GS, tmp);
	printk("ipu_reg: %s: \t0x%08x\r\n", "IPU_IN_FM_GS", reg_read(ipu, IPU_IN_FM_GS));

	tmp = OUT_FM_W(outW) | OUT_FM_H(outH);
	dev_dbg(ipu->dev, "write to OUT_FM_GS = 0x%x\n",tmp);
	reg_write(ipu, IPU_OUT_GS, tmp);

}

static void set_csc_param(struct jz_ipu *ipu, unsigned int in_fmt, unsigned int out_fmt)
{
	if ((in_fmt != IN_FMT_YUV444) && (out_fmt != OUT_FMT_YUV422)) {
		__enable_csc_mode();
		reg_write(ipu, IPU_CSC_C0_COEF, YUV_CSC_C0);
		//if (in_fmt == IN_FMT_YUV420_B) {
		if (0) { // JZ4780 V-Dcoder have fixed U V buffer issues, so no need this work around. Wolfgang, 2013-03-07
			// interchange C1 with C4, C2 with C3 for IPU Block format
			reg_write(ipu, IPU_CSC_C1_COEF, YUV_CSC_C4);
			reg_write(ipu, IPU_CSC_C2_COEF, YUV_CSC_C3);
			reg_write(ipu, IPU_CSC_C3_COEF, YUV_CSC_C2);
			reg_write(ipu, IPU_CSC_C4_COEF, YUV_CSC_C1);
		} else {
			reg_write(ipu, IPU_CSC_C1_COEF, YUV_CSC_C1);
			reg_write(ipu, IPU_CSC_C2_COEF, YUV_CSC_C2);
			reg_write(ipu, IPU_CSC_C3_COEF, YUV_CSC_C3);
			reg_write(ipu, IPU_CSC_C4_COEF, YUV_CSC_C4);
		}
		reg_write(ipu, IPU_CSC_OFFSET_PARA, YUV_CSC_OFFSET_PARA);
	} else {
		__disable_csc_mode();
		reg_write(ipu, IPU_CSC_OFFSET_PARA, 0x0);
	}
}

static void enable_ctrl_regs(struct jz_ipu *ipu, struct ipu_proc_info *ipu_proc)
{
	unsigned int tmp = 0;

	if (ipu_proc->img.output_mode & IPU_OUTPUT_TO_LCD_FG1) {
		/* No need to make destination TLB base and address ready */
		tmp = 0xffffffd7;
	} else {
		tmp = 0xffffffff;
	}
	reg_write(ipu, IPU_ADDR_CTRL, tmp);
	tmp = reg_read(ipu, IPU_ADDR_CTRL);
}

static void set_yuv_stride(struct jz_ipu *ipu, struct ipu_proc_info *ipu_proc)
{
	unsigned int tmp;
	struct ipu_img_param *img = &ipu_proc->img;

	reg_write(ipu, IPU_Y_STRIDE,  img->stride.y);
	tmp = U_STRIDE(img->stride.u) | V_STRIDE(img->stride.v);
	reg_write(ipu, IPU_UV_STRIDE, tmp);
}

static void config_osd_regs(struct jz_ipu *ipu)
{
	unsigned int tmp;

	tmp = GLB_ALPHA(0xff) | MOD_OSD(0x3) | OSD_PM;
	reg_write(ipu, IPU_OSD_CTRL, tmp);
}

static void stop_ipu_to_lcd(struct jz_ipu *ipu)
{
	unsigned int tmp;

	tmp = reg_read(ipu, IPU_TRIG);
	tmp |= IPU_STOP_LCD;
	reg_write(ipu, IPU_TRIG, tmp);

	msleep(60);
	tmp = reg_read(ipu, IPU_ADDR_CTRL);
	if (tmp)
		__start_ipu();

	tmp = reg_read(ipu, IPU_TRIG);
	tmp |= IPU_STOP_LCD;
	reg_write(ipu, IPU_TRIG, tmp);

	jz47_ipu_wait_frame_end_flag(ipu);
}

static void dump_img(struct jz_ipu *ipu)
{
	struct ipu_img_param *img = NULL;
	struct ipu_proc_info *ipu_proc = NULL;
	if (ipu == NULL) {
		dev_err(ipu->dev, "ipu is NULL\n");
		return;
	}

	ipu_proc = get_ipu_procinfo(ipu, ipu->cur_proc);
	if (!ipu_proc) {
		dev_err(ipu->dev, "get_ipu_procinfo failed! %d", __LINE__);
		return;
	}

	img = &ipu_proc->img;
	printk("ipu_cmd = %x\n", img->ipu_cmd);
	printk("lcdc_id = %d\n", img->lcdc_id);
	printk("output_mode[%#x]\r\n", (unsigned int) img->output_mode);
	printk("in_width[%#x]\r\n", (unsigned int) img->in_width);
	printk("in_height[%#x]\r\n", (unsigned int) img->in_height);
	printk("in_bpp[%#x]\r\n", (unsigned int) img->in_bpp);
	printk("in_fmt[%#x]\n", (unsigned int) img->in_fmt);
	printk("out_fmt[%#x]\n",(unsigned int) img->out_fmt);
	printk("out_x[%#x]\n",(unsigned int) img->out_x);
	printk("out_y[%#x]\n",(unsigned int) img->out_y);
	printk("out_width[%#x]\r\n", (unsigned int) img->out_width);
	printk("out_height[%#x]\r\n", (unsigned int) img->out_height);
	printk("y_buf_v[%#x]\r\n", (unsigned int) img->y_buf_v);
	printk("u_buf_v[%#x]\r\n", (unsigned int) img->u_buf_v);
	printk("v_buf_v[%#x]\r\n", (unsigned int) img->v_buf_v);
	printk("y_buf_p[%#x]\r\n", (unsigned int) img->y_buf_p);
	printk("u_buf_p[%#x]\r\n", (unsigned int) img->u_buf_p);
	printk("v_buf_p[%#x]\r\n", (unsigned int) img->v_buf_p);
	printk("out_buf_v[%#x]\r\n", (unsigned int) img->out_buf_v);
	printk("out_buf_p[%#x]\r\n", (unsigned int) img->out_buf_p);
	printk("src_page_mapping[%#x]\r\n", (unsigned int)img->src_page_mapping);
	printk("dst_page_mapping[%#x]\r\n", (unsigned int)img->dst_page_mapping);
	printk("y_t_addr[%#x]\r\n", (unsigned int) img->y_t_addr);
	printk("u_t_addr[%#x]\r\n", (unsigned int) img->u_t_addr);
	printk("v_t_addr[%#x]\r\n", (unsigned int) img->v_t_addr);
	printk("out_t_addr[%#x]\r\n", (unsigned int) img->out_t_addr);
	printk("stride.y[%#x]\r\n", (unsigned int) img->stride.y);
	printk("stride.u[%#x]\r\n", (unsigned int) img->stride.u);
	printk("stride.v[%#x]\r\n", (unsigned int) img->stride.v);
	printk("Wdiff[%#x]\r\n", (unsigned int) img->Wdiff);
	printk("Hdiff[%#x]\r\n", (unsigned int) img->Hdiff);
	printk("zoom_mode[%#x]\r\n", (unsigned int) img->zoom_mode);
	printk("hcoef_real_heiht[%#x]\r\n", (unsigned int) img->hcoef_real_heiht);
	printk("vcoef_real_heiht[%#x]\r\n", (unsigned int) img->vcoef_real_heiht);
	printk("hoft_table[%#x]\r\n", (unsigned int) img->hoft_table);
	printk("voft_table[%#x]\r\n", (unsigned int) img->voft_table);
	printk("hcoef_table[%#x]\r\n", (unsigned int) img->hcoef_table);
	printk("voft_table[%#x]\r\n", (unsigned int) img->voft_table);
	printk("hcoef_table[%#x]\r\n", (unsigned int) img->hcoef_table);
	printk("vcoef_table[%#x]\r\n", (unsigned int) img->vcoef_table);
	printk("cube_hcoef_table[%#x]\r\n", (unsigned int) img->cube_hcoef_table);
	printk("cube_vcoef_table[%#x]\r\n", (unsigned int) img->cube_vcoef_table);
	return ;
}

static int jz47_dump_ipu_regs(struct jz_ipu *ipu, int num)
{
	int i, total;
	int *hoft_table, *voft_table;
	int *hcoef_table, *vcoef_table;
	int hcoef_real_heiht, vcoef_real_heiht;
	struct ipu_img_param *img = NULL;
	struct ipu_proc_info *ipu_proc = NULL;
	struct ipu_table *table = NULL;

	if (ipu == NULL) {
		dev_err(ipu->dev, "ipu is NULL!\n");
		return -1;
	}
	dev_dbg(ipu->dev, "enter jz47_dump_ipu_regs\n");
	ipu_proc = get_ipu_procinfo(ipu, ipu->cur_proc);
	if (!ipu_proc) {
		dev_err(ipu->dev, "get_ipu_procinfo failed! %d", __LINE__);
		return -1;
	}

	table = &ipu_proc->table;
	img = &ipu_proc->img;
	hoft_table = table->hoft;
	hcoef_table= table->hcoef;
	hcoef_real_heiht = img->hcoef_real_heiht;
	voft_table = table->voft;
	vcoef_table= table->vcoef;
	vcoef_real_heiht = img->vcoef_real_heiht;

	if (num == -1) {
		total = sizeof(jz47_ipu_regs_name) / sizeof(struct ipu_reg_struct);
		for (i = 0; i < total; i++) {
			printk("ipu_reg: %s: \t0x%08x\r\n", jz47_ipu_regs_name[i].name,
					reg_read(ipu, jz47_ipu_regs_name[i].addr));
		}
	}
	if (num == -2) {
		printk(" //bi-cube resize\nint cube_hcoef_table[H_OFT_LUT][4] = {");
		for ( i = 0 ; i < hcoef_real_heiht ; i++)
			printk("\t\t\t{0x%02x,0x%02x, 0x%02x, 0x%02x}, \n",
					table->cube_hcoef[i][0], table->cube_hcoef[i][1],
					table->cube_hcoef[i][2], table->cube_hcoef[i][3]);
		printk("};");

		printk(" int cube_vcoef_table[V_OFT_LUT][4] = {");
		for ( i = 0 ; i < vcoef_real_heiht ; i++)
			printk("\t\t\t{0x%02x,0x%02x, 0x%02x, 0x%02x}, \n",
					table->cube_vcoef[i][0], table->cube_vcoef[i][1],
					table->cube_vcoef[i][2], table->cube_vcoef[i][3]);
		printk("};");
	} else if (num == -3) {
		printk("hcoef_real_heiht=%d\n", hcoef_real_heiht);
		for (i = 0; i < IPU_LUT_LEN; i++) {
			printk("ipu_H_LUT(%02d): hcoef, hoft: %05d %02d\n",
					i, hcoef_table[i], hoft_table[i]);
		}
		printk("vcoef_real_veiht=%d\n", vcoef_real_heiht);
		for (i = 0; i < IPU_LUT_LEN; i++) {
			printk("ipu_V_LUT(%02d): vcoef, voft: %05d %02d\n",
					i, vcoef_table[i], voft_table[i]);
		}
	}

	return 1;
}

static int ipu_dump_regs(struct jz_ipu *ipu)
{
	int ret = 0;
	if (ipu == NULL) {
		dev_err(ipu->dev, "ipu is NULL\n");
		return -1;
	}

	printk("ipu->base: %p\n", ipu->iomem);
	dump_img(ipu);
	ret = jz47_dump_ipu_regs(ipu, -1);
	ret = jz47_dump_ipu_regs(ipu, -2);
	ret = jz47_dump_ipu_regs(ipu, -3);

	return ret;
}

static struct ipu_proc_info *get_ipu_procinfo(struct jz_ipu *ipu, struct file *filp)
{
	struct ipu_proc_info *ipu_proc = NULL;

	list_for_each_entry(ipu_proc, &ipu->process_list, list) {
		if (ipu->cur_output_mode == IPU_OUTPUT_TO_LCD_FG1) {
			if (ipu_proc)
				return ipu_proc;
		} else {
			if (ipu_proc) {
				if (ipu_proc->ipu_filp == filp)
					return ipu_proc;
			}
		}
	}
	dev_err(ipu->dev, "not find related pid ipu process info!!! pid: %d, tgid: %d", current->pid, current->tgid);

	return NULL;
}

static int create_proc_info(struct jz_ipu *ipu, struct file *filp)
{
	struct ipu_proc_info *ipu_proc = NULL;

	dev_dbg(ipu->dev, "%s %d %p", __func__, __LINE__, filp);
	dev_dbg(ipu->dev, "filp: %p", filp);
	ipu_proc = kzalloc(sizeof(struct ipu_proc_info), GFP_KERNEL);
	if (!ipu_proc) {
		dev_err(ipu->dev, "kzalloc struct ipu_proc_info failed!");
		return -1;
	}
	ipu_proc->pid = current->pid;
	ipu_proc->ipu_filp = filp;
	list_add_tail(&ipu_proc->list, &ipu->process_list);
	ipu->proc_num++;
	dev_dbg(ipu->dev, "+++++++++++++++++++++++proc_num: %d", ipu->proc_num);

	return 0;
}

static int destroy_proc_info(struct jz_ipu *ipu, struct file *filp)
{
	struct ipu_proc_info *ipu_proc = NULL;

	dev_dbg(ipu->dev, "destroy_proc_info pid: %d, tgid: %d", current->pid, current->tgid);
	ipu_proc = get_ipu_procinfo(ipu, filp);
	if (!ipu_proc) {
		dev_err(ipu->dev, "get_ipu_procinfo failed! %d, %d", current->pid, current->tgid);
		return -1;
	}

	list_del_init(&ipu_proc->list);
	kfree(ipu_proc);
	ipu->proc_num--;

	return 0;
}

#if 0
static int destroy_allproc_info(struct jz_ipu *ipu)
{
	int i, record_proc_num;
	struct ipu_proc_info *ipu_proc = NULL;
	struct list_head *head = NULL;

	dev_dbg(ipu->dev, "destroy_allproc_info pid: %d, tgid: %d", current->pid, current->tgid);
	record_proc_num = ipu->proc_num;
	for (i = 0; i < record_proc_num; i++) {
		head = &ipu->process_list;
		list_for_each_entry(ipu_proc, head, list) {
			if (ipu_proc) {
				list_del_init(&ipu_proc->list);
				kfree(ipu_proc);
				ipu->proc_num--;
				break;
			}
		}
	}
	dev_dbg(ipu->dev, "ipu->proc_num: %d++++++", ipu->proc_num);

	return 0;
}
#endif

static unsigned int hal_infmt_is_packaged(int hal_fmt)
{
	unsigned int is_packaged = 0;

	/* hardware/libhardware/include/hardware/hardware.h */
	switch (hal_fmt) {
	case HAL_PIXEL_FORMAT_YCbCr_422_SP:
	case HAL_PIXEL_FORMAT_YCbCr_420_SP:
	case HAL_PIXEL_FORMAT_YCbCr_422_P:
	case HAL_PIXEL_FORMAT_YCbCr_420_P:
	case HAL_PIXEL_FORMAT_JZ_YUV_420_P:
	case HAL_PIXEL_FORMAT_YCbCr_420_B:
	case HAL_PIXEL_FORMAT_JZ_YUV_420_B:
		is_packaged = 0;
		break;
	case HAL_PIXEL_FORMAT_RGBA_5551:
	case HAL_PIXEL_FORMAT_RGBA_8888:
	case HAL_PIXEL_FORMAT_RGBX_8888:
	case HAL_PIXEL_FORMAT_RGB_888:
	case HAL_PIXEL_FORMAT_BGRA_8888:
	case HAL_PIXEL_FORMAT_BGRX_8888:
	case HAL_PIXEL_FORMAT_RGB_565:
	case HAL_PIXEL_FORMAT_YCbCr_422_I:
	case HAL_PIXEL_FORMAT_YCbCr_420_I:
	default:
		is_packaged = 1;
		break;
	}

	return is_packaged;
}

/* pixel format definitions to ipu pixel format */
static unsigned int hal_to_ipu_infmt(int hal_fmt)
{
	unsigned int ipu_fmt = IN_FMT_YUV420;

	/* hardware/libhardware/include/hardware/hardware.h */
	switch ( hal_fmt ) {
	case HAL_PIXEL_FORMAT_YCbCr_422_SP:
		ipu_fmt = IN_FMT_YUV422;
		break;
	case HAL_PIXEL_FORMAT_YCbCr_420_SP:
		ipu_fmt = IN_FMT_YUV420;
		break;
	case HAL_PIXEL_FORMAT_YCbCr_422_P:
		ipu_fmt = IN_FMT_YUV422;
		break;
	case HAL_PIXEL_FORMAT_YCbCr_422_I:
		ipu_fmt = IN_FMT_YUV422;
		break;
	case HAL_PIXEL_FORMAT_YCbCr_420_I:
		ipu_fmt = IN_FMT_YUV420;
		break;
	case HAL_PIXEL_FORMAT_YCbCr_420_B:
	case HAL_PIXEL_FORMAT_JZ_YUV_420_B:
		ipu_fmt = IN_FMT_YUV420_B;
		break;
	case HAL_PIXEL_FORMAT_RGBA_5551:
		ipu_fmt = IN_FMT_RGB_555;
		break;
	case HAL_PIXEL_FORMAT_RGBA_8888:
	case HAL_PIXEL_FORMAT_RGBX_8888:
	case HAL_PIXEL_FORMAT_RGB_888:
	case HAL_PIXEL_FORMAT_BGRA_8888:
	case HAL_PIXEL_FORMAT_BGRX_8888:
		ipu_fmt = IN_FMT_RGB_888;
		break;
	case HAL_PIXEL_FORMAT_RGB_565:
		ipu_fmt = IN_FMT_RGB_565;
		break;
	case HAL_PIXEL_FORMAT_YCbCr_420_P:
	case HAL_PIXEL_FORMAT_JZ_YUV_420_P:
	default:
		ipu_fmt = IN_FMT_YUV420;
		break;
	}

	return ipu_fmt;
}

/* pixel format definitions to ipu pixel format */
static unsigned int hal_to_ipu_outfmt(int hal_fmt)
{
	unsigned int ipu_fmt = OUT_FMT_RGB888;

	/* hardware/libhardware/include/hardware/hardware.h */
	switch ( hal_fmt ) {
		case HAL_PIXEL_FORMAT_RGBA_8888:
		case HAL_PIXEL_FORMAT_RGBX_8888:
		case HAL_PIXEL_FORMAT_RGB_888:
		case HAL_PIXEL_FORMAT_BGRA_8888:
		case HAL_PIXEL_FORMAT_BGRX_8888:
			ipu_fmt = OUT_FMT_RGB888;
			break;
		case HAL_PIXEL_FORMAT_RGB_565:
			ipu_fmt = OUT_FMT_RGB565;
			break;
		case HAL_PIXEL_FORMAT_RGBA_5551:
			ipu_fmt = OUT_FMT_RGB555;
			break;
		case HAL_PIXEL_FORMAT_YCbCr_422_I:
			ipu_fmt = OUT_FMT_YUV422;
			break;
	}

	return ipu_fmt;
}

static void copy_ipu_tabel_from_user(struct jz_ipu *ipu, struct ipu_img_param *imgp, struct file *filp)
{
	struct ipu_proc_info *ipu_proc = NULL;
	struct ipu_table *table = NULL;

	dev_dbg(ipu->dev, "%s %d", __func__, __LINE__);
	ipu_proc = get_ipu_procinfo(ipu, filp);
	if (!ipu_proc) {
		dev_err(ipu->dev, "get_ipu_procinfo failed! %d, %d", current->pid, current->tgid);
		return;
	}

	table = &ipu_proc->table;

	copy_from_user((void *)table->hoft,
			(void *)imgp->hoft_table,
			sizeof(table->hoft));
	copy_from_user((void *)table->voft,
			(void *)imgp->voft_table,
			sizeof(table->voft));
	copy_from_user((void *)table->pic_enhance,
			(void *)imgp->pic_enhance_table,
			sizeof(table->pic_enhance));

	if (imgp->zoom_mode != ZOOM_MODE_BILINEAR) {
		copy_from_user((void *)table->cube_hcoef,
				(void *)imgp->cube_hcoef_table,
				sizeof(table->cube_hcoef));
		copy_from_user((void *)table->cube_vcoef,
				(void *)imgp->cube_vcoef_table,
				sizeof(table->cube_vcoef));
	} else {
		copy_from_user((void *)table->hcoef,
				(void *)imgp->hcoef_table,
				sizeof(table->hcoef));
		copy_from_user((void *)table->vcoef,
				(void *)imgp->vcoef_table,
				sizeof(table->vcoef));
	}
}

static int jz47_set_ipu_resize(struct jz_ipu *ipu, struct ipu_proc_info *ipu_proc)
{
	unsigned int tmp;
	struct ipu_img_param *img;

	if (ipu == NULL) {
		dev_err(ipu->dev, "ipu is NULL\n");
		return -1;
	}
	dev_dbg(ipu->dev, "enter jz47_set_ipu_resize\n");

	img = &ipu_proc->img;

	__disable_hrsz();
	__disable_vrsz();

	if (img->out_width != img->in_width) {
		__enable_hrsz();
	}
	if (img->out_height != img->in_height) {
		__enable_vrsz();
	}

	tmp = ((img->vcoef_real_heiht - 1) << VE_IDX_SFT) |
		((img->hcoef_real_heiht - 1) << HE_IDX_SFT);
	reg_write(ipu, IPU_RSZ_COEF_INDEX, tmp);

	if (img->zoom_mode != ZOOM_MODE_BILINEAR) {
		__sel_zoom_mode();

		/* set_hrsz_lut_weigth_cube */
		set_hrsz_lut_weigth_cube(ipu, ipu_proc);

		/* set_vrsz_lut_weigth_cube */
		set_vrsz_lut_weigth_cube(ipu, ipu_proc);
	} else {
		__disable_zoom_mode();

		/* set_vrsz_lut_coef_line */
		set_vrsz_lut_coef_line(ipu, ipu_proc);

		/* set_hrsz_lut_coef_line */
		set_hrsz_lut_coef_line(ipu, ipu_proc);
	}

	return 0;
}

static void jz47_set_xy_offset(struct jz_ipu *ipu, struct ipu_proc_info *ipu_proc)
{
	unsigned int tmp;
	struct ipu_img_param *img = NULL;

	if (ipu == NULL) {
		dev_err(ipu->dev, "ipu is NULL\n");
		return;
	}
	img = &ipu_proc->img;

	tmp = SCREEN_XOFT(img->out_x) | SCREEN_YOFT(img->out_y);
	reg_write(ipu, IPU_FM_XYOFT, tmp);
}

/* get RGB order */
static unsigned int get_out_fmt_rgb_order(int hal_out_fmt)
{
	unsigned int order = RGB_OUT_OFT_RGB;

	switch (hal_out_fmt) {
		case HAL_PIXEL_FORMAT_RGBA_8888:
		case HAL_PIXEL_FORMAT_RGBX_8888:
		case HAL_PIXEL_FORMAT_RGB_888:
		case HAL_PIXEL_FORMAT_RGB_565:
		case HAL_PIXEL_FORMAT_BGRA_8888:
		case HAL_PIXEL_FORMAT_BGRX_8888:
		case HAL_PIXEL_FORMAT_RGBA_5551:
		default :
			order = RGB_OUT_OFT_RGB;
			break;
	}

	return order;
}

static int jz47_set_ipu_csc_cfg(struct jz_ipu *ipu, int outW,
								int outH, int Wdiff, int Hdiff)
{
	unsigned int out_rgb_order;
	unsigned int in_fmt, out_fmt, tmp;
	unsigned int in_fmt_tmp, out_fmt_tmp;
	struct ipu_proc_info *ipu_proc = NULL;
	struct ipu_img_param *img = NULL;

	if (ipu == NULL) {
		dev_err(ipu->dev, "ipu is NULL\n");
		return -1;
	}
	dev_dbg(ipu->dev, "enter jz47_set_ipu_csc_cfg\n");

	ipu_proc = get_ipu_procinfo(ipu, ipu->cur_proc);
	if (!ipu_proc) {
		dev_err(ipu->dev, "get_ipu_procinfo failed! %d, %d", current->pid, current->tgid);
		return -1;
	}

	img = &ipu_proc->img;

	in_fmt = hal_to_ipu_infmt(img->in_fmt);
	out_fmt = hal_to_ipu_outfmt(img->out_fmt);
	/* set RGB order */
	out_rgb_order = get_out_fmt_rgb_order(img->out_fmt);

	switch (in_fmt) {
		case IN_FMT_YUV420:
		case IN_FMT_YUV420_B:
			Hdiff = (Hdiff + 1) & ~1;
			Wdiff = (Wdiff + 1) & ~1;
			break;
		case IN_FMT_YUV422:
			Wdiff = (Wdiff + 1) & ~1;
			break;
		case IN_FMT_YUV444:
		case IN_FMT_YUV411:
			break;
		default:
			printk("Error: 111 Input data format isn't support\n");
			return -1;
	}

	switch (out_fmt) {
		case OUT_FMT_RGB888:
			outW = outW << 2;
			break;
		case OUT_FMT_RGB555:
			outW = outW << 1;
			break;
		case OUT_FMT_RGB565:
			outW = outW << 1;
			break;
	}

	// Set GS register
	set_gs_regs(ipu,Wdiff,Hdiff,outW,outH);

	// Set out stride
	if (img->stride.out != 0) {
		reg_write(ipu, IPU_OUT_STRIDE, img->stride.out);
	} else {
		switch (img->output_mode & IPU_OUTPUT_MODE_MASK) {
			case IPU_OUTPUT_TO_LCD_FG1:
				break;
			case IPU_OUTPUT_TO_LCD_FB0:
			case IPU_OUTPUT_TO_LCD_FB1:
				tmp = img->fb_w * img->in_bpp >> 3;
				reg_write(ipu, IPU_OUT_STRIDE, tmp);
				break;
			case IPU_OUTPUT_TO_FRAMEBUFFER:
			default:
				outW = img->out_width;
				switch (out_fmt) {
					default:
					case OUT_FMT_RGB888:
						outW = outW << 2;
						break;
					case OUT_FMT_RGB555:
					case OUT_FMT_RGB565:
						outW = outW << 1;
						break;
				}
				reg_write(ipu, IPU_OUT_STRIDE, outW);
				break;
		}
	}

	in_fmt_tmp = in_fmt;
	out_fmt_tmp = out_fmt;
	if ( in_fmt == IN_FMT_YUV422 ) {
		//printk("*** jz47xx ipu driver: IN_FMT_YUV422 use IN_OFT_Y1VY0U\n");
		in_fmt_tmp |= IN_OFT_VY1UY0;
	}
	if (out_fmt == OUT_FMT_YUV422) {
		//printk("*** outformat use YUV_PKG_OUT_OFT_Y0UY1V\n");
		out_fmt_tmp |= YUV_PKG_OUT_OFT_VY1UY0;
	}

	// set Format
	tmp = in_fmt_tmp | out_fmt_tmp | out_rgb_order;
	reg_write(ipu, IPU_D_FMT, tmp);

	// set CSC parameter
	set_csc_param(ipu, in_fmt, out_fmt);

	return 0;
}

static int jz47_set_ipu_stride(struct jz_ipu *ipu, struct ipu_proc_info *ipu_proc)
{
	int in_fmt;
	int out_fmt;
	unsigned int tmp;
	struct ipu_img_param *img = NULL;

	if (ipu == NULL) {
		dev_err(ipu->dev, "ipu is NULL\n");
		return -1;
	}
	dev_dbg(ipu->dev, "Enter jz47_set_ipu_stride\n");

	img = &ipu_proc->img;

	in_fmt = hal_to_ipu_infmt(img->in_fmt);
	out_fmt = hal_to_ipu_outfmt(img->out_fmt);

	if (img->stride.y == 0) {	/* set default stride */
		if (in_fmt == IN_FMT_YUV420_B) {
			reg_write(ipu, IPU_Y_STRIDE, img->in_width*16);
		} else if (reg_read(ipu, IPU_FM_CTRL) & SPKG_SEL) {
			reg_write(ipu, IPU_Y_STRIDE, img->in_width*2);
		} else {
			reg_write(ipu, IPU_Y_STRIDE, img->in_width);
		}

		switch (in_fmt) {
			case IN_FMT_YUV420:
			case IN_FMT_YUV422:
				tmp = U_STRIDE(img->in_width/2) | V_STRIDE(img->in_width/2);
				reg_write(ipu, IPU_UV_STRIDE, tmp);
				break;
			case IN_FMT_YUV420_B:
				tmp = U_STRIDE(8*img->in_width) | V_STRIDE(8*img->in_width);
				reg_write(ipu, IPU_UV_STRIDE, tmp);
				break;
			case IN_FMT_YUV444:
				tmp = U_STRIDE(img->in_width) | V_STRIDE(img->in_width);
				reg_write(ipu, IPU_UV_STRIDE, tmp);
				break;
			case IN_FMT_YUV411:
				tmp = U_STRIDE(img->in_width/4) | V_STRIDE(img->in_width/4);
				reg_write(ipu, IPU_UV_STRIDE, tmp);
				break;
			default:
				dev_err(ipu->dev, "Error: 222 Input data format isn't support\n");
				return -1;
		}
	} else {
		reg_write(ipu, IPU_Y_STRIDE, img->stride.y);
		tmp = U_STRIDE(img->stride.u) | V_STRIDE(img->stride.v);
		reg_write(ipu, IPU_UV_STRIDE, tmp);
	}

	return 0;
}

static int jz47_ipu_init(struct jz_ipu *ipu, struct ipu_img_param *imgp)
{
	int ret, in_fmt, out_fmt;
	int outW, outH, Wdiff, Hdiff;
	struct ipu_proc_info *ipu_proc = NULL;

	dev_dbg(ipu->dev, "enter jz47_ipu_init\n");

	ipu_proc = get_ipu_procinfo(ipu, ipu->cur_proc);
	if (!ipu_proc) {
		dev_err(ipu->dev, "get_ipu_procinfo failed! %d, %d", current->pid, current->tgid);
		return -1;
	}

	if (imgp->output_mode & IPU_OUTPUT_TO_LCD_FG1) {
		ipu->cur_output_mode = IPU_OUTPUT_TO_LCD_FG1;
		__enable_lcdc_mode();
	}

	if (imgp->output_mode & IPU_OUTPUT_BLOCK_MODE) {
		ipu->cur_output_mode = IPU_OUTPUT_BLOCK_MODE;
		__disable_lcdc_mode();
	}

	dev_dbg(ipu->dev, "<-----outW: %d, outH: %d\n", imgp->out_width, imgp->out_height);
	outW = imgp->out_width;
	outH = imgp->out_height;
	Wdiff = imgp->Wdiff;
	Hdiff = imgp->Hdiff;

	dev_dbg(ipu->dev, "outW=%d, outH=%d, Wdiff=%d, Hdiff=%d", outW, outH, Wdiff, Hdiff);

	/* set src and dst format */
	in_fmt = hal_to_ipu_infmt(imgp->in_fmt);
	out_fmt = hal_to_ipu_outfmt(imgp->out_fmt);

	if (hal_infmt_is_packaged(imgp->in_fmt)) {
		__enable_pkg_mode();
	} else {
		//if (out_fmt != OUT_FMT_YUV422) {
		__disable_pkg_mode();
		//}
	}

	ret = jz47_set_ipu_resize(ipu, ipu_proc);
	if (ret != 0) {
		dev_err(ipu->dev, "jz47_set_ipu_resize error : out!\n");
		return ret;
	}
	if (imgp->output_mode & IPU_OUTPUT_TO_LCD_FG1) {
		jz47_set_xy_offset(ipu, ipu_proc);
	}
	ret = jz47_set_ipu_csc_cfg(ipu, outW, outH, Wdiff, Hdiff);
	if (ret != 0) {
		dev_err(ipu->dev, "jz47_set_ipu_csc_cfg error : out!\n");
		return ret;
	}
	ret = jz47_set_ipu_stride(ipu, ipu_proc);
	if (ret != 0) {
		dev_err(ipu->dev, "jz47_set_ipu_stride error : out!\n");
		return ret;
	}

	if (out_fmt == OUT_FMT_YUV422) {
		__disable_csc_mode();
	}
	if (imgp->stlb_base) {
		reg_write(ipu, IPU_SRC_TLB_ADDR, imgp->stlb_base);
	}
	if (imgp->dtlb_base) {
		reg_write(ipu, IPU_DEST_TLB_ADDR, imgp->dtlb_base);
	}

	if (in_fmt == IN_FMT_YUV420_B) {
		__enable_blk_mode();
	}
	config_osd_regs(ipu);

	enable_ctrl_regs(ipu, ipu_proc);

	/* check AHB0 priority
	 * if CPU_PRIORITY > 2, IPU_TO_LCDC maybe underrun when playback 1080P video.
	 * so make sure all priority is 0.
	 */
	if (imgp->output_mode & IPU_OUTPUT_TO_LCD_FG1) {
		unsigned int prio;
		prio = REG_HARB_PRIOR;
		if (prio != 0) {
			ipu->g_reg_harb_prior = prio;
			REG_HARB_PRIOR = 0;
			dev_info(ipu->dev,"jz4780_ipu.c 111 save REG_HARB_PRIOR=%#X\n", ipu->g_reg_harb_prior);
		}
		else {
			//ipu->g_reg_harb_prior = 0;
		}
	}
	return ret;
}

static int ipu_init(struct jz_ipu *ipu, struct ipu_img_param *imgp, struct file *filp)
{
	int ret = 0;
	struct ipu_img_param *img = NULL;
	struct ipu_proc_info *ipu_proc = NULL;

	if (!ipu || !imgp) {
		dev_err(ipu->dev, "ipu is NULL\n");
		return -1;
	}

#ifdef IPU_DEBUG
	dump_cpm();
#endif

	dev_dbg(ipu->dev, "enter ipu_init, %d\n", current->pid);

	mutex_lock(&ipu->lock);
	ipu->cur_proc = filp;
	ipu_proc = get_ipu_procinfo(ipu, ipu->cur_proc);
	if (!ipu_proc) {
		dev_err(ipu->dev, "get_ipu_procinfo failed! %d, %d", current->pid, current->tgid);
		mutex_unlock(&ipu->lock);
		return -1;
	}

	img = &ipu_proc->img;
	dev_dbg(ipu->dev, "--->outW: %d, outH: %d\n", imgp->out_width, imgp->out_height);
	memcpy(img, imgp, sizeof(struct ipu_img_param));
	//*img = *imgp; /* use the new parameter */

	dev_dbg(ipu->dev, "ipu->inited = %d", ipu->inited);
	if (!ipu->inited) {
		clk_enable(ipu->clk);
	}

	//__reset_ipu();
	ret = jz47_ipu_init(ipu, img);
	if (ret < 0) {
		dev_err(ipu->dev, "jz47_ipu_init failed\n");
		clk_disable(ipu->clk);
		mutex_unlock(&ipu->lock);
		return ret;
	}

	ipu->inited = 1;

	mutex_unlock(&ipu->lock);

	return ret;
}

static int ipu_start(struct jz_ipu *ipu)
{
	pid_t cur_pid;
	unsigned long irq_flags;
	struct ipu_img_param *img = NULL;
	struct ipu_proc_info *ipu_proc = NULL;

	if (ipu == NULL) {
		dev_err(ipu->dev, "ipu is NULL\n");
		mutex_unlock(&ipu->run_lock);
		return -1;
	}

	cur_pid = current->pid;
	dev_dbg(ipu->dev, "enter ipu_start %d\n", current->pid);

	ipu_proc = get_ipu_procinfo(ipu, ipu->cur_proc);
	if (!ipu_proc) {
		dev_err(ipu->dev, "get_ipu_procinfo failed, %d!", current->pid);
		return -1;
	}

	img = &ipu_proc->img;
#ifdef IPU_DEBUG
	ipu_dump_regs(ipu);
#endif
	//printk("+++++++++=ipu->base: %p name=%s\n", ipu->iomem,ipu->name);
	if (img->output_mode & IPU_OUTPUT_BLOCK_MODE) {
		/* Wait for current frame to finished */
		spin_lock_irqsave(&ipu->update_lock, irq_flags);
		ipu->frame_requested++;
		spin_unlock_irqrestore(&ipu->update_lock, irq_flags);
	}

	__clear_ipu_out_end();
	if (img->output_mode & IPU_OUTPUT_BLOCK_MODE) {
		__ipu_enable_irq();
	}

	/* start ipu */
	__start_ipu();
#if 0
	/* This is just used in ipu direct test! */
	unsigned int tmp;
	int i;
	for (i = 0; i < 10; i++) {
	  tmp = reg_read(ipu, IPU_STATUS);
	  if (tmp & 0x1) {
		  printk("out end ~~~~~~~~~~~~~~~>\n");
	  } else {
		  printk("not end ~~~~~~\n");
	  }
	}
#endif

	if (img->output_mode & IPU_OUTPUT_BLOCK_MODE) {
		/* Wait for current frame to finished */
		if (ipu->frame_requested != ipu->frame_done)
			wait_event_interruptible_timeout(
					ipu->frame_wq, ipu->frame_done == ipu->frame_requested, HZ/10 + 1); /* HZ = 100 */
	}

	dev_dbg(ipu->dev, "exit ipu_start %d\n", current->pid);
	if (img->output_mode & IPU_OUTPUT_BLOCK_MODE)
		mutex_unlock(&ipu->run_lock);

	return 0;
}

static int ipu_setbuffer(struct jz_ipu *ipu, struct ipu_img_param *imgp, struct file *filp)
{
	unsigned int py_buf, pu_buf, pv_buf;
	unsigned int py_buf_v, pu_buf_v, pv_buf_v;
	unsigned int out_buf;
	unsigned int spage_map, dpage_map;
	unsigned int in_fmt;
	unsigned int tmp;

	struct file *tmp_filp = NULL;
	struct ipu_img_param *img = NULL;
	struct ipu_proc_info *ipu_proc = NULL;

	if (ipu == NULL) {
		dev_err(ipu->dev, "ipu is NULL\n");
		return -1;
	}
	dev_dbg(ipu->dev, "enter ipu_setbuffer %d\n", current->pid);

	spin_lock(&ipu->suspend_lock);
	if (ipu->suspend_entered == 1) {
		spin_unlock(&ipu->suspend_lock);
		return -1;
	}
	spin_unlock(&ipu->suspend_lock);
	mutex_lock(&ipu->run_lock);
	if (ipu->cur_proc == filp)
		tmp_filp = ipu->cur_proc;
	else
		tmp_filp = filp;

	ipu_proc = get_ipu_procinfo(ipu, tmp_filp);
	if (!ipu_proc) {
		dev_err(ipu->dev, "get_ipu_procinfo failed! %d, %d", current->pid, current->tgid);
		mutex_unlock(&ipu->run_lock);
		return -1;
	}
	if (ipu->cur_output_mode != IPU_OUTPUT_TO_LCD_FG1)
		if ((ipu->cur_proc != filp) || !ipu->inited)
			ipu_init(ipu, &ipu_proc->img, filp);

	img = &ipu_proc->img;
	if (imgp) {
		unsigned int old_bpp = ipu_proc->img.in_bpp;
		*img = *imgp;
		img->in_bpp = old_bpp;
	}

	spage_map = img->src_page_mapping;
	dpage_map = img->dst_page_mapping;

	py_buf = ((unsigned int) img->y_buf_p);
	pu_buf = ((unsigned int) img->u_buf_p);
	pv_buf = ((unsigned int) img->v_buf_p);

	py_buf_v = (unsigned int) img->y_buf_v;
	pu_buf_v = (unsigned int) img->u_buf_v;
	pv_buf_v = (unsigned int) img->v_buf_v;

	in_fmt = hal_to_ipu_infmt(img->in_fmt);

	dev_dbg(ipu->dev, "py_buf=0x%08x, pu_buf=0x%08x, pv_buf=0x%08x, py_t_buf=0x%08x, pu_t_buf=0x%08x, pv_t_buf=0x%08x",
			py_buf, pu_buf, pv_buf, py_buf_v, pu_buf_v, pv_buf_v);
	dev_dbg(ipu->dev, "reg_read(IPU_V_BASE + IPU_FM_CTRL)=%08x, spage_map=%x, dpage_map=%x",
			reg_read(ipu, IPU_FM_CTRL), spage_map, dpage_map);

	if (spage_map != 0) {
		dev_dbg(ipu->dev, "spage_map != 0\n");

		if (py_buf_v == 0) {
			//printk("Can not found source map table, use no map now!\r\n");
			spage_map = 0;
			__disable_spage_map();
		} else {
			dev_dbg(ipu->dev, "we force spage_map to 0\n");

			py_buf = py_buf_v;
			pu_buf = pu_buf_v;
			pv_buf = pv_buf_v;

			__enable_spage_map();
		}
	} else {
			__disable_spage_map();
	}

	reg_write(ipu, IPU_Y_ADDR, py_buf);
	reg_write(ipu, IPU_U_ADDR, pu_buf);
	reg_write(ipu, IPU_V_ADDR, pv_buf);

	set_yuv_stride(ipu, ipu_proc);

	dev_dbg(ipu->dev, "dpage_map = %d\n", dpage_map);
	dev_dbg(ipu->dev, "img->out_buf_v = %x, img->out_buf_p = %x\n",
			(unsigned int)img->out_buf_v, (unsigned int)img->out_buf_p);
	/* set out put */
	if ((dpage_map != 0) && !(img->output_mode & IPU_OUTPUT_TO_LCD_FG1)) {
		if (PHYS((unsigned int) img->out_buf_v) == 0) {
			dev_err(ipu->dev, " Can not found destination map table, use no map now!\r\n");
			dpage_map = 0;
			__disable_dpage_map();

			if (PHYS((unsigned int) img->out_buf_p) == 0) {
				dev_err(ipu->dev, "Can not found the destination buf[%#x]\r\n",
						(unsigned int)img->out_buf_p);
				mutex_unlock(&ipu->run_lock);
				return (-1);
			} else {
				tmp = PHYS((unsigned int)img->out_buf_p);
				reg_write(ipu, IPU_OUT_ADDR, tmp);
			}
		} else {
			tmp = PHYS((unsigned int)img->out_buf_v); /* for test */
			reg_write(ipu, IPU_OUT_ADDR, tmp);  /* for test */
			__enable_dpage_map();
		}
	} else {
		dpage_map = 0;
		__disable_dpage_map();

		if (!(img->output_mode & IPU_OUTPUT_TO_LCD_FG1)) {
			if (PHYS((unsigned int)img->out_buf_p) == 0) {
				dev_err(ipu->dev, "Can not found the destination buf[%#x]\r\n",
						(unsigned int)img->out_buf_p);
				mutex_unlock(&ipu->run_lock);
				return (-1);
			} else {
				dev_dbg(ipu->dev, "img->out_buf_p=0x%x",img->out_buf_p);
				out_buf = img->out_buf_p;
				reg_write(ipu, IPU_OUT_ADDR, PHYS(out_buf));
			}
		}
	}

	tmp = reg_read(ipu, IPU_ADDR_CTRL);
	if (img->output_mode & IPU_OUTPUT_TO_LCD_FG1) {
		tmp &= ~D_RY;
		tmp |= (Y_RY | U_RY | V_RY | PTS_RY);
	} else {
		tmp |= (Y_RY | U_RY | V_RY | D_RY | PTS_RY | PTD_RY);
	}
	reg_write(ipu, IPU_ADDR_CTRL, tmp); /* enable address reset */

	if (img->output_mode & IPU_OUTPUT_TO_LCD_FG1) {
		/* start ipu */
		__start_ipu();
		mutex_unlock(&ipu->run_lock);
	}

	return 0;
}

static int ipu_stop(struct jz_ipu *ipu)
{
	unsigned int tmp;

	dev_dbg(ipu->dev, "enter ipu_stop\n");
	mutex_lock(&ipu->run_lock);
	if (!ipu) {
		dev_err(ipu->dev, "ipu is NULL!\n");
		mutex_unlock(&ipu->run_lock);
		return -1;
	}
	dev_dbg(ipu->dev, "ipu_stop %s %d", __func__, current->pid);

	spin_lock(&ipu->suspend_lock);
	if(ipu->suspend_entered) {
		spin_unlock(&ipu->suspend_lock);
		mutex_unlock(&ipu->run_lock);
		return 0;
	}
	spin_unlock(&ipu->suspend_lock);

	if (IPU_OUTPUT_TO_LCD_FG1 == ipu->cur_output_mode) {
		stop_ipu_to_lcd(ipu);
	} else {
		tmp = IPU_STOP;
		reg_write(ipu, IPU_TRIG, tmp);
	}
	mutex_unlock(&ipu->run_lock);

	return 0;
}

static int ipu_shut(struct jz_ipu *ipu)
{
	struct ipu_proc_info *ipu_proc = NULL;

	dev_dbg(ipu->dev, "enter ipu_shut %d \n", current->pid);
	if (ipu == NULL) {
		dev_err(ipu->dev, "ipu is NULL");
		return -1;
	}

	ipu_proc = get_ipu_procinfo(ipu, ipu->cur_proc);
	if (!ipu_proc) {
		dev_err(ipu->dev, "get_ipu_procinfo failed! %d, %d", current->pid, current->tgid);
		ipu->inited = 0;

		return -1;
	}

	return 0;
}

static int ipu_set_bypass(struct jz_ipu *ipu)
{
	int ret = 0;

	mutex_lock(&ipu_lock);
	if (!strcmp(ipu->name, "ipu0"))
		ret = !ipu0_direct ? 0 : -EFAULT;
	else if (!strcmp(ipu->name, "ipu1"))
		ret = !ipu1_direct ? 0 : -EFAULT;
    else {
		dev_err(ipu->dev, "%s is unknown ipu device!\n", ipu->name);
		dev_err(ipu->dev, "ipu0_direct: %d, ipu1_direct: %d", ipu0_direct, ipu1_direct);
		ret = -EFAULT;
	}
	dev_dbg(ipu->dev, "+ipu0_direct: %d, ipu1_direct: %d", ipu0_direct, ipu1_direct);
	dev_dbg(ipu->dev, "+ipu0_nodirect: %d, ipu1_nodirect: %d", ipu0_nodirect, ipu1_nodirect);

	mutex_unlock(&ipu_lock);

	return ret;
}

static int ipu_get_bypass_state(struct jz_ipu *ipu)
{
	if (!strcmp(ipu->name, "ipu0")) {
		return 0;
	}
	if (!strcmp(ipu->name, "ipu1")) {
		return 0;
	}

	return 0;
}


static int ipu_enable_clk(struct jz_ipu *ipu,unsigned int value)
{
	int lcdid = 0;
	unsigned int temp;
	mutex_lock(&ipu_lock);
	if (!strcmp(ipu->name, "ipu0")){
		lcdid = 1;
	}else if (!strcmp(ipu->name, "ipu1")){
		lcdid = 0;
	}

	if(value) {
		temp = *((volatile unsigned int *)(0xb3050104 + lcdid*0x50000));
		temp |=(1<<15);
		*((volatile unsigned int *)(0xb3050104 + lcdid*0x50000)) = temp;
#ifdef IPU_DEBUG
		dump_cpm();
#endif
	} else {
		temp = *((volatile unsigned int *)(0xb3050104 + lcdid*0x50000));
		temp &= ~(1<<15);
		*((volatile unsigned int *)(0xb3050104 + lcdid*0x50000)) = temp;
#ifdef IPU_DEBUG
		dump_cpm();
#endif
	}
	mutex_unlock(&ipu_lock);
	return 0;
}

#if 0
extern int jzfb_ipu_enable_clk(int id, unsigned int value);
extern int jzfb_ipu0_to_buf(int id, unsigned int value);
static int ipu0_to_buf(struct jz_ipu *ipu,unsigned int value)
{
	int lcdid = 0,ret;
	mutex_lock(&ipu_lock);
	if (!strcmp(ipu->name, "ipu0")){
		lcdid = 1;
	}else if (!strcmp(ipu->name, "ipu1")){
		lcdid = 0;
	}
	ret = jzfb_ipu0_to_buf(lcdid, value);
	mutex_unlock(&ipu_lock);
	return ret;
}
#endif

static int ipu_clr_bypass(struct jz_ipu *ipu)
{
	mutex_lock(&ipu_lock);
	if (IPU_OUTPUT_TO_LCD_FG1 == ipu->cur_output_mode) {
		if (!strcmp(ipu->name, "ipu1"))
			ipu1_direct = 0;
		else if (!strcmp(ipu->name, "ipu0"))
			ipu0_direct = 0;
		else {
			dev_err(ipu->dev, "direct unknown ipu device!!!");
			mutex_unlock(&ipu_lock);
			return -EFAULT;
		}
	} else {
		if (!strcmp(ipu->name, "ipu0")) {
			if (ipu0_nodirect)
				ipu0_nodirect--;
		} else if (!strcmp(ipu->name, "ipu1")) {
			if (ipu1_nodirect)
				ipu1_nodirect--;
		} else {
			dev_err(ipu->dev, "nodirect unknown ipu device!!!");
			mutex_unlock(&ipu_lock);
			return -EFAULT;
		}
	}

	dev_dbg(ipu->dev, "ipu1_direct: %d, ipu0_direct: %d", ipu1_direct, ipu0_direct);
	dev_dbg(ipu->dev, "++++ipu0_nodirect: %d, ipu1_nodirect: %d", ipu0_nodirect, ipu1_nodirect);

	mutex_unlock(&ipu_lock);

	return 0;
}

static int is_ipu_available(struct jz_ipu *ipu, struct ipu_img_param *img)
{
	int ret = 0;

	mutex_lock(&ipu_lock);
	if (img->output_mode & IPU_OUTPUT_TO_LCD_FG1) {
		ipu->cur_output_mode = IPU_OUTPUT_TO_LCD_FG1;
		if (!strcmp(ipu->name, "ipu1"))
			ret = (!ipu1_direct && !ipu1_nodirect) ? (ipu1_direct = 1) : -EFAULT;
	    else if (!strcmp(ipu->name, "ipu0"))
			ret = (!ipu0_direct && !ipu0_nodirect) ? (ipu0_direct = 1) : -EFAULT;
	    else {
			dev_err(ipu->dev, "direct unknown ipu device!");
			ret = -EFAULT;
		}
	}

	if (img->output_mode & IPU_OUTPUT_BLOCK_MODE) {
		ipu->cur_output_mode = IPU_OUTPUT_BLOCK_MODE;
		if (!strcmp(ipu->name, "ipu1"))
			ret = !ipu0_nodirect ? (ipu1_nodirect++) : -EFAULT;
	    else if (!strcmp(ipu->name, "ipu0"))
			ret = !ipu1_nodirect ? (ipu0_nodirect++) : -EFAULT;
	    else {
			dev_err(ipu->dev, "nodirect unknown ipu device!");
			ret = -EFAULT;
		}
	}
	dev_info(ipu->dev, "ipu0_nodirect: %d, ipu1_nodirect: %d, ipu1_direct: %d, ipu0_direct: %d",
			 ipu0_nodirect, ipu1_nodirect, ipu1_direct, ipu0_direct);
	mutex_unlock(&ipu_lock);

	return ret;
}

static long ipu_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
	int ret = 0;
	struct ipu_img_param img;
	void __user *argp = (void __user *)arg;
	struct miscdevice *dev = file->private_data;
	struct jz_ipu *ipu= container_of(dev, struct jz_ipu, misc_dev);
	unsigned int value;
	struct flush_cache_info info;

	if (_IOC_TYPE(cmd) != JZIPU_IOC_MAGIC) {
		dev_err(ipu->dev, "invalid cmd!\n");
		return -EFAULT;
	}

	switch (cmd) {
	case IOCTL_IPU_SHUT:
		ret = ipu_shut(ipu);
		break;
	case IOCTL_IPU_INIT:
		if (copy_from_user(&img, argp, sizeof(struct ipu_img_param))) {
			dev_err(ipu->dev, "copy_from_user error!!!\n");
			return -EFAULT;
		}
		if (ipu->cur_proc != file) {
			if ((ret = is_ipu_available(ipu, &img)) < 0) {
				dev_err(ipu->dev, "%s is busy now! please obey the using rules!", ipu->name);
				return -EFAULT;
			}
		}
		copy_ipu_tabel_from_user(ipu, &img, file);
		ret = ipu_init(ipu, &img, file);
		break;
	case IOCTL_IPU_SET_BUFF:
		if (copy_from_user(&img, argp, sizeof(struct ipu_img_param))) {
			dev_err(ipu->dev, "copy_from_user error!!!\n");
			return -EFAULT;
		}
		ret = ipu_setbuffer(ipu, &img, file);
		break;
	case IOCTL_IPU_START:
		ret = ipu_start(ipu);
		break;
	case IOCTL_IPU_STOP:
		ret = ipu_stop(ipu);
		break;
	case IOCTL_IPU_DUMP_REGS:
		ret = ipu_dump_regs(ipu);
		break;
	case IOCTL_IPU_SET_BYPASS:
		ret = ipu_set_bypass(ipu);
		break;
	case IOCTL_IPU_GET_BYPASS_STATE:
		ret = ipu_get_bypass_state(ipu);
		if (copy_to_user(argp, &ret, sizeof(int))) {
			dev_err(ipu->dev, "copy_to_user error!!!\n");
			return -EFAULT;
		}
		break;
	case IOCTL_IPU_CLR_BYPASS:
		ret = ipu_clr_bypass(ipu);
		break;
	case IOCTL_IPU_ENABLE_CLK:
		if (copy_from_user(&value, argp, sizeof(int))) {
			dev_info(ipu->dev, "Enable IPU clock data error\n");
			return -EFAULT;
		}
		ret=ipu_enable_clk(ipu,value);
		break;
#if 0
	case IOCTL_IPU0_TO_BUF:
		if (copy_from_user(&value, argp, sizeof(int)))
			return -EFAULT;
		ret=ipu0_to_buf(ipu,value);
		break;
#endif
	case IOCTL_IPU_FLUSH_CACHE:
		if (copy_from_user(&info, (void *)arg, sizeof(info))) {
			ret = -EFAULT;
			break;
		}
		dma_cache_sync(NULL, (void *)info.addr, info.len, info.dir);
		dev_dbg(ipu->dev, "[%d:%d] flush cache\n", current->tgid, current->pid);
		ret =0;
		break;
	case IOCTL_IPU_DMMU_MAP:
		{
			struct ipu_dmmu_map_info di;
			if (copy_from_user(&di, (void *)arg, sizeof(di))) {
				ret = -EFAULT;
				break;
			}
			return dmmu_map(dev->this_device,di.addr,di.len);
		}
		break;
	case IOCTL_IPU_DMMU_UNMAP:
		{
			struct ipu_dmmu_map_info di;
			if (copy_from_user(&di, (void *)arg, sizeof(di))) {
				ret = -EFAULT;
				break;
			}
			return dmmu_unmap(dev->this_device,di.addr,di.len);
		}
		break;
	case IOCTL_IPU_DMMU_UNMAP_ALL:
		dmmu_unmap_all(dev->this_device);
		break;
	default:
		dev_err(ipu->dev, "invalid command: 0x%08x\n", cmd);
		return -EINVAL;
	}

	return ret;
}

static int ipu_open(struct inode *inode, struct file *filp)
{
	int ret = 0;
	struct miscdevice *dev = filp->private_data;
	struct jz_ipu *ipu = container_of(dev, struct jz_ipu, misc_dev);

	ipu->open_cnt++;
	dev_dbg(ipu->dev,"+++++Enter ipu_open open_cnt: %d pid: %d, tgid: %d filp: %p\n",
			 ipu->open_cnt, current->pid, current->tgid, filp);

	mutex_lock(&ipu->lock);
	/* create process struct */
	dev_dbg(ipu->dev, "%s %d", __func__, __LINE__);
	ret = create_proc_info(ipu, filp);
	if (ret < 0) {
		dev_err(ipu->dev, "create_proc_info failed!");
		mutex_unlock(&ipu->lock);
		return -1;
	}
	mutex_unlock(&ipu->lock);

	return 0;
}

static int ipu_release(struct inode *inode, struct file *filp)
{
	int ret = 0;
	/* check AHB0 priority */
	struct ipu_img_param *img = NULL;
	struct ipu_proc_info *ipu_proc = NULL;

	struct miscdevice *dev = filp->private_data;
	struct jz_ipu *ipu = container_of(dev,struct jz_ipu,misc_dev);

	mutex_lock(&ipu->lock);
	ipu->open_cnt--;
	ipu_clr_bypass(ipu);

	ipu_proc = get_ipu_procinfo(ipu, filp);
	if (ipu_proc != NULL) {
		img = &ipu_proc->img;
		if (img && (img->output_mode & IPU_OUTPUT_TO_LCD_FG1)) {
			if ( ipu->g_reg_harb_prior != 0 ) {
				REG_HARB_PRIOR = ipu->g_reg_harb_prior;
				dev_info(ipu->dev, "jz4780_ipu.c 222 restore REG_HARB_PRIOR=%#X\n", ipu->g_reg_harb_prior);
				ipu->g_reg_harb_prior = 0;
			}
		}
	}
	ipu_stop(ipu);

	dev_dbg(ipu->dev,"=====Enter ipu_release open_cnt: %d %d\n", ipu->open_cnt, ipu->proc_num);
	dev_dbg(ipu->dev, "pid: %d, tgid: %d", current->pid, current->tgid);

	ret = destroy_proc_info(ipu, filp);
	if (ret < 0) {
		dev_err(ipu->dev, "%s failed!, %d", __func__, __LINE__);
		mutex_unlock(&ipu->lock);
		return -EFAULT;
	}

	if (ipu->proc_num == 0 && ipu->inited) {
		clk_disable(ipu->clk);
		ipu->inited = 0;
	}

	mutex_unlock(&ipu->lock);

	return 0;
}

static struct file_operations ipu_ops = {
	.owner     	=    THIS_MODULE,
	.open      	=    ipu_open,
	.release   	=    ipu_release,
	.unlocked_ioctl =    ipu_ioctl,
};

static irqreturn_t ipu_irq_handler(int irq, void *data)
{
	struct jz_ipu *ipu = (struct jz_ipu *)data;
	unsigned long irq_flags;
	unsigned int dummy_read;

	dummy_read = reg_read(ipu, IPU_STATUS); /* avoid irq looping or disable_irq*/
	__ipu_disable_irq(); // failed
	dev_dbg(ipu->dev, "ipu_irq_handler---------->2");

	if (ipu->cur_output_mode == IPU_OUTPUT_BLOCK_MODE) {
		spin_lock_irqsave(&ipu->update_lock, irq_flags);
	}

	if (ipu->cur_output_mode == IPU_OUTPUT_BLOCK_MODE) {
		ipu->frame_done = ipu->frame_requested;
		spin_unlock_irqrestore(&ipu->update_lock, irq_flags);
		wake_up(&ipu->frame_wq);
	}

	return IRQ_HANDLED;
}

static int ipu_probe(struct platform_device *pdev)
{
	int ret = 0;
	struct jz_ipu *ipu;

	if (!pdev->dev.of_node) {
		printk("device-tree data is missing[%s,%d]\n",__func__,__LINE__);
		return -ENXIO;
	}

	ipu = (struct jz_ipu *)kzalloc(sizeof(struct jz_ipu), GFP_KERNEL);
	if (!ipu) {
		dev_err(&pdev->dev, "alloc ipu mem_region failed!\n");
		return -ENOMEM;
	}

	if (pdev->id == 0)
		sprintf(ipu->name,"ipu%d", 1);
	else
		sprintf(ipu->name,"ipu%d", 0);

	//sprintf(ipu->name,"ipu%d",pdev->id);
	ipu->misc_dev.minor     = MISC_DYNAMIC_MINOR;
	ipu->misc_dev.name      = ipu->name;
	ipu->misc_dev.fops      = &ipu_ops;
	ipu->dev = &pdev->dev;
	ipu->suspend_entered = 0;

	mutex_init(&ipu_lock);
	mutex_init(&ipu->lock);
	mutex_init(&ipu->run_lock);
	spin_lock_init(&ipu->suspend_lock);

	spin_lock_init(&ipu->update_lock);
	init_waitqueue_head(&ipu->frame_wq);
	INIT_LIST_HEAD(&ipu->process_list);

	ipu->res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!ipu->res) {
		dev_err(&pdev->dev, "failed to get dev resources: %d\n", ret);
		ret = -EINVAL;
		goto err_get_res;
	}
	ipu->res = request_mem_region(ipu->res->start,
			ipu->res->end - ipu->res->start + 1, pdev->name);
	if (!ipu->res) {
		dev_err(&pdev->dev, "failed to request regs memory region");
		ret = -EINVAL;
		goto err_get_res;
	}
	ipu->iomem = ioremap(ipu->res->start, resource_size(ipu->res));
	if (!ipu->iomem) {
		dev_err(&pdev->dev, "failed to remap regs memory region: %d\n", ret);
		ret = -EINVAL;
		goto err_ioremap;
	}

	ipu->irq = platform_get_irq(pdev, 0);
	if (request_irq(ipu->irq, ipu_irq_handler, IRQF_SHARED,
				ipu->name, ipu)) {
		dev_err(&pdev->dev, "request irq failed\n");
		ret = -EINVAL;
		goto err_req_irq;
	}

	ipu->clk = clk_get(ipu->dev,"ipu_clk");
	if(IS_ERR(ipu->clk)) {
		dev_err(&pdev->dev, "ipu clk get failed!\n");
		ret = -ENODEV;
		goto err_get_clk;
	}
	clk_prepare_enable(ipu->clk);

	dev_set_drvdata(&pdev->dev, ipu);
	//dev_info(&pdev->dev, "----------%p\n", ipu);

	ret = misc_register(&ipu->misc_dev);
	if (ret < 0) {
		dev_err(&pdev->dev, "register misc device failed!\n");
		goto err_set_drvdata;
	}

	return 0;

err_set_drvdata:
	clk_put(ipu->clk);
err_get_clk:
	free_irq(ipu->irq, ipu);
err_req_irq:
	iounmap(ipu->iomem);
err_ioremap:
err_get_res:
	return ret;
}

static int ipu_remove(struct platform_device *pdev)
{
	struct jz_ipu *ipu = dev_get_drvdata(&pdev->dev);
	struct resource *res = ipu->res;

	ipu = dev_get_drvdata(&pdev->dev);

	free_irq(ipu->irq, ipu);
	iounmap(ipu->iomem);

	release_mem_region(res->start, res->end - res->start + 1);

	free_irq(ipu->irq, ipu);

	return 0;
}

static struct of_device_id jz4780_of_match[] = {
		{ .compatible = "ingenic,jz4780-ipu0", },
		{ },
};

MODULE_DEVICE_TABLE(of, jz4780_of_match);

static struct platform_driver jz_ipu_driver = {
	.probe 	= ipu_probe,
	.remove = ipu_remove,
	.driver = {
		.name = "jz-ipu",
		.of_match_table = jz4780_of_match,
	},
};

static int __init ipu_setup(void)
{
	platform_driver_register(&jz_ipu_driver);

	return 0;
}

static void __exit ipu_cleanup(void)
{
	platform_driver_unregister(&jz_ipu_driver);
}

module_init(ipu_setup);
module_exit(ipu_cleanup);


MODULE_DESCRIPTION("Jz4780 IPU driver");
MODULE_AUTHOR("Sean Tang <ctang@ingenic.cn>");
MODULE_LICENSE("GPL");
