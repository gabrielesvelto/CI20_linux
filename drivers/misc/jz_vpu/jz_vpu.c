/*
 * linux/drivers/mmc/host/jz4780_mmc.c - Ingenic VPU driver
 *
 * Copyright (C) 2012 Ingenic Semiconductor Co., Ltd.
 * Author: Large Dipper <ykli@ingenic.com>.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */


#include <linux/module.h>
#include <linux/errno.h>
#include <linux/interrupt.h>
#include <linux/slab.h>
#include <linux/delay.h>
#include <linux/miscdevice.h>
#include <linux/spinlock.h>
#include <linux/clk.h>
#include <linux/syscalls.h>
#include <linux/platform_device.h>
#include <linux/dma-mapping.h>
#include <uapi/asm-generic/mman-common.h>

#include <dt-bindings/soc/cpm.h>
#include <dt-bindings/soc/base.h>
#include <dt-bindings/clock/jzcpm_pwc.h>
#include <dt-bindings/soc/libdmmu.h>

#include "jz_vpu.h"


static struct of_device_id jz4780_of_match[];

/*#define VPU_DBG_MEM*/
#ifdef	VPU_DBG_MEM
#define	pr_dbg_mem(msg...)	do {printk(msg);} while(0)
#else
#define	pr_dbg_mem(msg...)	do{;}while(0)
#endif
struct jz_vpu {
	struct device		*dev;

	struct mutex		lock;		//lock vpu device node
	struct mutex		mutex;		//lock VPU for user using
	struct mutex		mem_lock;	//lock memory operation

	spinlock_t spin_lock;

	int			irq;
	void __iomem		*iomem;
	struct miscdevice	mdev;
	struct clk		*clk;
	/*struct clk		*clk_gate;*/

	struct wakeup_source vpu_wake_lock;
	struct completion	done;
	int			use_count;
	pid_t			owner_pid;
	unsigned int		status;
	void*                   cpm_pwc;
	unsigned int		release_all_mem;

	struct list_head	mem_list;

};

struct kernel_space_node {
	unsigned long vaddr;
	struct list_head list;
};

struct user_space_node {
	unsigned long vaddr;
	unsigned long page_num;
	struct list_head list;
	struct list_head mem_list;
	unsigned int dmmu_map_flag;		//0:unmap, 1:map
};


unsigned int jwsun_debug;
unsigned long jz_tcsm_start;

static int vpu_reset(struct jz_vpu *vpu)
{
	int timeout = 0xffffff;
	unsigned int srbc = cpm_inl(CPM_SRBC);

	cpm_set_bit(30, CPM_SRBC);
	while (!(cpm_inl(CPM_SRBC) & (1 << 29)) && --timeout);

	if (timeout == 0) {
		dev_warn(vpu->dev, "[%d:%d] wait stop ack timeout\n",
			 current->tgid, current->pid);
		cpm_outl(srbc, CPM_SRBC);
		return -1;
	} else {
		cpm_outl(srbc | (1 << 31), CPM_SRBC);
		cpm_outl(srbc, CPM_SRBC);
	}

	return 0;
}

void dump_vpu_cpm(void)
{
	printk("clk_gr1 (BIT2)  = 0x%08x\n", *((volatile unsigned int *)0xB0000028));
	printk("lpcr    (BIT30) = 0x%08x\n", *((volatile unsigned int *)0xB0000004));
	printk("cgu_vpu         = 0x%08x\n", *((volatile unsigned int *)0xB0000030));

	printk("opcr (reg)      = 0x%08x\n", *((volatile unsigned int *)0xB0000024));
	printk("opcr (cpm_inl)  = 0x%08x\n", (cpm_inl(CPM_OPCR)));

	printk("srbc (reg)      = 0x%08x\n",*((volatile unsigned int *)0xB00000c4));
	printk("srbc (cpm_inl)	= 0x%08x\n",cpm_inl(CPM_SRBC));

	printk("mpll (reg)      = 0x%08x\n",*((volatile unsigned int *)0xB0000014));

}
void dump_vpu_vpu_regs(void)
{
	printk("sch_GLBC  = 0x%08x\n",*((volatile unsigned int *)0xB3200000));
	printk("sch_TLBA  = 0x%08x\n",*((volatile unsigned int *)0xB3200030));
	printk("sch_STAT  = 0x%08x\n",*((volatile unsigned int *)0xB3200034));
	printk("sch_CFG   = 0x%08x\n",*((volatile unsigned int *)0xB3200060));
	printk("sch_MAP   = 0x%08x\n",*((volatile unsigned int *)0xB3200064));

	printk("aux_CTRL  = 0x%08x\n",*((volatile unsigned int *)0xB32a0000));

	printk("vdma_TRIG = 0x%08x\n",*((volatile unsigned int *)0xB3210008));
	printk("vdma_STAT = 0x%08x\n",*((volatile unsigned int *)0xB321000C));

	printk("sde_STAT  = 0x%08x\n",*((volatile unsigned int *)0xB3290000));
	printk("sde_CFG0  = 0x%08x\n",*((volatile unsigned int *)0xB3290014));
	printk("sde_BSADDR= 0x%08x\n",*((volatile unsigned int *)0xB329001C));
}
static int vpu_on(struct jz_vpu *vpu)
{
	int ret;
	if (cpm_inl(CPM_OPCR) & OPCR_IDLE)
		return -EBUSY;

	ret = clk_enable(vpu->clk);
	if (ret) {
		dev_err(vpu->dev, "[%d:%d]VPU clock enable error,errno:%d\n",
				current->tgid, current->pid, ret);
		return -EIO;
	}
	/*ret = clk_enable(vpu->clk_gate);
	if (ret) {
		dev_err(vpu->dev, "[%d:%d]VPU clock gate enable error,errno:%d\n",
				current->tgid, current->pid, ret);
		return -EIO;
	}*/
	cpm_pwc_enable(vpu->cpm_pwc);

	__asm__ __volatile__ (
			"mfc0  $2, $16,  7   \n\t"
			"ori   $2, $2, 0x340 \n\t"
			"andi  $2, $2, 0x3ff \n\t"
			"mtc0  $2, $16,  7  \n\t"
			"nop                  \n\t");
	enable_irq(vpu->irq);
	__pm_stay_awake(&vpu->vpu_wake_lock);
	dev_dbg(vpu->dev, "[%d:%d] on\n", current->tgid, current->pid);
	/*dump_vpu_cpm();*/
	/*dump_vpu_vpu_regs();*/

	return 0;
}

static long vpu_off(struct jz_vpu *vpu)
{
	disable_irq_nosync(vpu->irq);

	__asm__ __volatile__ (
			"mfc0  $2, $16,  7   \n\t"
			"andi  $2, $2, 0xbf \n\t"
			"mtc0  $2, $16,  7  \n\t"
			"nop                  \n\t");

	cpm_clear_bit(31,CPM_OPCR);
	clk_disable(vpu->clk);
	/*clk_disable(vpu->clk_gate);*/
	cpm_pwc_disable(vpu->cpm_pwc);
	/* Clear completion use_count here to avoid a unhandled irq after vpu off */
	vpu->done.done = 0;
	__pm_relax(&vpu->vpu_wake_lock);
	dev_dbg(vpu->dev, "[%d:%d] off\n", current->tgid, current->pid);
	/*dump_vpu_cpm();*/
	/*dump_vpu_vpu_regs();*/

	return 0;
}

static int vpu_open(struct inode *inode, struct file *filp)
{
	struct miscdevice *dev = filp->private_data;
	struct jz_vpu *vpu = container_of(dev, struct jz_vpu, mdev);
	int ret = 0;

	dev_dbg(vpu->dev, "[%d:%d] open\n", current->tgid, current->pid);

	mutex_lock(&vpu->lock);

	if (vpu->use_count == 0) {
		ret = vpu_on(vpu);
		if (ret) {
			dev_err(vpu->dev, "[%d:%d]VPU on error\n",
					current->tgid, current->pid);
			goto ERR;
		}
		vpu->use_count++;
		INIT_LIST_HEAD(&vpu->mem_list);
		vpu->release_all_mem = RELEASE_YES;
	} else {
		dev_dbg(vpu->dev, "[%d:%d] already on\n",
			current->tgid, current->pid);
		ret = -EBUSY;
		goto ERR;
	}
	mutex_unlock(&vpu->lock);
	return 0;
ERR:
	mutex_unlock(&vpu->lock);
	return ret;
}

static int vpu_alloc_one_page(struct user_space_node *us_node)
{
	int ret;
	unsigned long vaddr = 0;
	struct kernel_space_node *ks_node;

	ks_node = kzalloc(sizeof(struct kernel_space_node), GFP_KERNEL);
	if (!ks_node) {
		printk("Error, alloc memory for kernel_space_node\n");
		ret = -ENOMEM;
		goto ERR0;
	}

	//vaddr = __get_free_page(GFP_KERNEL | __GFP_ZERO);
	vaddr = __get_free_page(GFP_ATOMIC | __GFP_ZERO);
	if (vaddr) {
		SetPageReserved(virt_to_page((void *)vaddr));
		ks_node->vaddr = vaddr;
		INIT_LIST_HEAD(&ks_node->list);
		list_add(&ks_node->list, &us_node->list);
	} else {
		printk("%s %d,no memory for alloc, and return\n",
				__func__, __LINE__);
		ret = -ENOMEM;
		goto ERR1;
	}

	return 0;
ERR1:
	kfree(ks_node);
ERR0:
	return ret;
}

static void vpu_free_user_pages(struct user_space_node *us_node)
{
	struct list_head *pos, *next;
	struct kernel_space_node *ks_node;

	list_for_each_safe(pos, next, &us_node->list) {
		ks_node = list_entry(pos, struct kernel_space_node, list);
		ClearPageReserved(virt_to_page((void *)ks_node->vaddr));
		free_page(ks_node->vaddr);
		__list_del_entry(&ks_node->list);
		kfree(ks_node);
	}
}

static int vpu_alloc_pages_for_user(struct user_space_node *us_node)
{
	int ret;
	unsigned long page_num = us_node->page_num;

	while (page_num >= 1) {
		ret = vpu_alloc_one_page(us_node);
		if (ret < 0) {
			printk("%s %d, alloc one page for VPU error\n",
					__func__, __LINE__);
			goto ERR_MEM;
		}
		page_num -= 1;
	}
	return 0;

ERR_MEM:
	vpu_free_user_pages(us_node);

	return ret;
}

static int vpu_remap_pages_to_userspace(struct user_space_node *us_node)
{
	struct vm_area_struct *vma;
	struct list_head *pos, *next;
	struct kernel_space_node *ks_node;
	unsigned long start;
	int ret;

	start = us_node->vaddr;

	down_write(&current->mm->mmap_sem);

	vma = find_vma(current->mm, us_node->vaddr);

	list_for_each_safe(pos, next, &us_node->list) {
		ks_node = list_entry(pos, struct kernel_space_node, list);
		ret = io_remap_pfn_range(vma, start,
				(ks_node->vaddr & 0x1fffffff) >> PAGE_SHIFT,
				PAGE_SIZE, vma->vm_page_prot);
		if (ret) {
			printk("%s %d, remap_pfn_range error\n", __func__, __LINE__);
			goto ERR;
		}
		start += PAGE_SIZE;
	}

	up_write(&current->mm->mmap_sem);

	return 0;
ERR:
	up_write(&current->mm->mmap_sem);
	return -EAGAIN;
}

static unsigned long vpu_request_mem(struct jz_vpu *vpu, unsigned long arg)
{
	int ret;
	unsigned long v_addr;
	unsigned long len = arg;
	unsigned long flags, prot;
	struct user_space_node *us_node;

	mutex_lock(&vpu->mem_lock);
	us_node = kzalloc(sizeof(struct user_space_node), GFP_KERNEL);
	if (!us_node) {
		printk("%s %d, alloc memory for user_space_node error\n",
				__func__, __LINE__);
		goto ERR0;
	}
#if 0
	flags = MAP_SHARED | MAP_ANONYMOUS;
	flags &= ~(MAP_EXECUTABLE | MAP_DENYWRITE);
#else
	flags = MAP_SHARED;
#endif
	prot = PROT_READ | PROT_WRITE;
	v_addr = vm_mmap(NULL, 0, len, prot, flags, 0);
	if (IS_ERR_VALUE(v_addr)) {
		printk("%s %d, vm_mmap_gpoff error\n", __func__, __LINE__);
		goto ERR1;
	}

	us_node->page_num = (arg + PAGE_SIZE - 1) / PAGE_SIZE;
	us_node->vaddr = v_addr;
	us_node->dmmu_map_flag = UNMAP;
	INIT_LIST_HEAD(&us_node->list);
	INIT_LIST_HEAD(&us_node->mem_list);

	ret = vpu_alloc_pages_for_user(us_node);
	if (ret) {
		printk("%s %d, alloc pages for user error\n",
				__func__, __LINE__);
		goto ERR2;
	}

	ret = vpu_remap_pages_to_userspace(us_node);
	if (ret) {
		printk("%s %d, remap kernel memory to userspace error\n",
				__func__, __LINE__);
		goto ERR3;
	}

#if 0
	pr_dbg_mem("\t\tv_addr:0x%08lx(userspace)\tsize:%ld\n",
			us_node->vaddr, us_node->page_num * PAGE_SIZE);
#endif
	list_add(&us_node->mem_list, &vpu->mem_list);
	mutex_unlock(&vpu->mem_lock);

	return us_node->vaddr;
ERR3:
	vpu_free_user_pages(us_node);
ERR2:
	vm_munmap(us_node->vaddr, us_node->page_num * PAGE_SIZE);
ERR1:
	kfree(us_node);
ERR0:
	mutex_unlock(&vpu->mem_lock);
	return 0;
}

static int vpu_release_mem(struct jz_vpu *vpu, unsigned long arg)
{
	struct list_head *pos, *next;
	struct user_space_node *us_node;

	mutex_lock(&vpu->mem_lock);
	list_for_each_safe(pos, next, &vpu->mem_list) {
		us_node = list_entry(pos, struct user_space_node, mem_list);
		if (us_node->vaddr == arg) {
			if (us_node->dmmu_map_flag == UNMAP) {
				vm_munmap(us_node->vaddr, us_node->page_num * PAGE_SIZE);
				vpu_free_user_pages(us_node);
				__list_del_entry(&us_node->mem_list);
				kfree(us_node);
				mutex_unlock(&vpu->mem_lock);
				return 0;
			} else {
				dev_err(vpu->dev, "[%d:%d] VPU memory have not"
						" been unmaped, addr:0x%lx\n",
						current->tgid, current->pid, arg);
				mutex_unlock(&vpu->mem_lock);
				return -EPERM;
			}
		}
	}
	if (list_empty(&vpu->mem_list)) {

		vpu->release_all_mem = RELEASE_YES;
	}
	dev_warn(vpu->dev, "[%d:%d]Exception: VPU release the memory had"
			" been release again. Please check your code,"
			" addr:0x%lx\n", current->tgid, current->pid, arg);
	mutex_unlock(&vpu->mem_lock);

	return 0;
}

static int vpu_release_all_mem(struct jz_vpu *vpu, unsigned int release)
{
	struct list_head *pos, *next;
	struct user_space_node *us_node;

	pr_dbg_mem("###########%s %d,,pid:%d,tgid:%d,comm:%s, release mem\n",
			__func__, __LINE__,current->pid,current->tgid,current->comm);
	mutex_lock(&vpu->mem_lock);

	if (list_empty(&vpu->mem_list)) {
		dev_warn(vpu->dev, "[%d:%d] VPU memory have been release already!!\n",
			current->tgid, current->pid);
		mutex_unlock(&vpu->mem_lock);
		return 0;
	}
	if (vpu->release_all_mem != RELEASE_YES) {
		dev_err(vpu->dev, "[%d:%d] VPU memory have not been all unmaped\n",
			current->tgid, current->pid);
		mutex_unlock(&vpu->mem_lock);
		return -EPERM;
	}

	list_for_each_safe(pos, next, &vpu->mem_list) {
		us_node = list_entry(pos, struct user_space_node, mem_list);
		pr_dbg_mem("%s %d, [%d:%d] Now Release memory v_addr:0x%x\n",
				__func__, __LINE__, current->tgid,
				current->pid, us_node->vaddr);
		if (release == 0)
			vm_munmap(us_node->vaddr, us_node->page_num * PAGE_SIZE);
		vpu_free_user_pages(us_node);
		__list_del_entry(&us_node->mem_list);
		kfree(us_node);
	}

	vpu->release_all_mem = RELEASE_YES;

	mutex_unlock(&vpu->mem_lock);

	return 0;
}

static int vpu_dmmu_unmap_all(struct jz_vpu *vpu)
{
	int ret;
	mutex_lock(&vpu->mem_lock);
	ret = dmmu_unmap_all(vpu->mdev.this_device);
	vpu->release_all_mem = RELEASE_YES;
	mutex_unlock(&vpu->mem_lock);

	return ret;
}

static int vpu_release_resource(struct jz_vpu *vpu)
{
	int ret = 0;

	pr_dbg_mem("#########%s %d,close vpu, pid:%d,tgid:%d,comm:%s\n",
			__func__, __LINE__,current->pid,current->tgid,current->comm);
#if 0
	dump_stack();
#endif

	mutex_lock(&vpu->lock);

	if (vpu->use_count > 0)
		vpu->use_count--;
	else {
		dev_warn(vpu->dev, "[%d:%d] VPU close already\n",
				current->tgid, current->pid);
		mutex_unlock(&vpu->lock);
		return 0;
	}

	if (vpu->use_count == 0) {
		vpu_off(vpu);

		ret = vpu_dmmu_unmap_all(vpu);
		if (ret) {
			dev_err(vpu->dev, "[%d:%d] VPU dmmu unmap all error\n",
					current->tgid, current->pid);
			mutex_unlock(&vpu->lock);
			WARN_ON(1);
			return ret;
		}

		ret = vpu_release_all_mem(vpu, 1);
		if (ret) {
			dev_err(vpu->dev, "[%d:%d] VPU release all memory error\n",
					current->tgid, current->pid);
			mutex_unlock(&vpu->lock);
			WARN_ON(1);
			return ret;
		}

		if (mutex_is_locked(&vpu->mutex)) {
			dev_warn(vpu->dev, "[%d:%d]Exception: VPU has been locked,"
					" and USER have not release, so it is need"
					" to release after close VPU\n",
				 current->tgid, current->pid);
			mutex_unlock(&vpu->mutex);
		}
	} else {
		dev_err(vpu->dev, "[%d:%d] VPU device is not permited close"
				" or open more times. Please check you code!\n",
				current->tgid, current->pid);
		mutex_unlock(&vpu->lock);

		return -EPERM;
	}

	dev_dbg(vpu->dev, "[%d:%d] close\n", current->tgid, current->pid);

	mutex_unlock(&vpu->lock);

	return 0;
}

static int vpu_release(struct inode *inode, struct file *filp)
{
	struct miscdevice *dev = filp->private_data;
	struct jz_vpu *vpu = container_of(dev, struct jz_vpu, mdev);
#if 0
	int dec_count = MAX_LOCK_DEPTH;

	spin_lock(&vpu->lock);
	vpu->use_count--;

	if (vpu->use_count == 0) {
		vpu_off(vpu);
		while (mutex_is_locked(&vpu->mutex) && --dec_count) {
			dev_warn(vpu->dev, "[%d:%d] mutex locked, dec by 1\n",
				 current->tgid, current->pid);
			mutex_unlock(&vpu->mutex);
		}
		if (!dec_count) {
			dev_err(vpu->dev, "[%d:%d] lock depth > %d\n",
				current->tgid, current->pid, MAX_LOCK_DEPTH);
			WARN_ON(1);
		}
	} else
		dev_dbg(vpu->dev, "[%d:%d] someone else used, leave on\n",
			current->tgid, current->pid);

	spin_unlock(&vpu->lock);
#endif
	dev_dbg(vpu->dev, "[%d:%d] close\n", current->tgid, current->pid);
	return vpu_release_resource(vpu);
	//return 0;
}

#if 1
static int vpu_lock(struct jz_vpu *vpu)
{
	int ret = 0;

	if (vpu->owner_pid == current->pid) {
		dev_err(vpu->dev, "[%d:%d] dead lock(vpu_driver)\n",
				current->tgid, current->pid);
		ret = -EINVAL;
	}

	if (mutex_lock_interruptible(&vpu->mutex) != 0) {
		dev_err(vpu->dev, "[%d:%d] lock vpu_driver error!"
				"the task is been interruptible\n",
				current->tgid, current->pid);
		ret = -EIO;
	}

	vpu->owner_pid = current->pid;

	return ret;
}

static int vpu_unlock(struct jz_vpu *vpu)
{
	int ret = 0;

#if 1
	mutex_unlock(&vpu->mutex);
	vpu->owner_pid = 0;
#else
	if (vpu->owner_pid == current->pid) {
		mutex_unlock(&vpu->mutex);
		vpu->owner_pid = 0;
	} else {
		dev_warn(vpu->dev, "[%d:%d] vpu unlock by else thread:%d\n",
				current->tgid, current->pid, current->pid);
		ret = -EINVAL;
	}

#endif
	return ret;
}
#endif

static int vpu_dmmu_map(struct jz_vpu *vpu, unsigned long arg)
{
	struct miscdevice *dev = &vpu->mdev;
	struct vpu_dmmu_map_info di;
	struct user_space_node *us_node;
	struct list_head *pos, *next;
	int ret = 0;
	int map_over = 0;
	if (copy_from_user(&di, (void *)arg, sizeof(di))) {
		printk("%s %d, cmd_vpu_dmmu_map, pid:%d,comm:%s\n", __func__, __LINE__,
				current->pid,current->comm);
		return 0;
	}
#if 0
	printk("%s %d, cmd_vpu_dmmu_map, addr:0x%x, len:%d, pid:%d comm:%s\n",
			__func__, __LINE__,
			di.addr, di.len,current->pid,current->comm);
#endif

	mutex_lock(&vpu->mem_lock);
	list_for_each_safe(pos, next, &vpu->mem_list) {
		us_node = list_entry(pos, struct user_space_node, mem_list);
		if (us_node->vaddr == di.addr) {
			if (us_node->dmmu_map_flag == UNMAP) {
				ret = dmmu_map(dev->this_device,di.addr,di.len);
				if (!ret) {
					printk("dmmu_map error,addr:0x%lx\n",
							us_node->vaddr);
					return ret;
				}
				map_over = 1;
				us_node->dmmu_map_flag = MAPED;
				vpu->release_all_mem = RELEASE_NO;
				mutex_unlock(&vpu->mem_lock);
				return ret;
			} else {
				printk("Error:This addr:0x%x has been mapped\n", di.addr);
				mutex_unlock(&vpu->mem_lock);
				return 0;
			}
		}
	}
	if (map_over == 0)
		printk("Error:This addr:0x%x is not belone mem_list\n", di.addr);


	mutex_unlock(&vpu->mem_lock);

	return ret;
}

static int vpu_dmmu_unmap(struct jz_vpu *vpu, unsigned long arg)
{
	struct miscdevice *dev = &vpu->mdev;
	struct user_space_node *us_node;
	struct vpu_dmmu_map_info di;
	struct list_head *pos, *next;
	int ret = 0;
	if (copy_from_user(&di, (void *)arg, sizeof(di))) {
		ret = -EFAULT;
		printk("%s %d, cmd_vpu_dmmu_unmap, pid:%d,comm:%s\n", __func__, __LINE__,
				current->pid,current->comm);
		return ret;
	}
#if 0
	printk("%s %d, cmd_vpu_dmmu_unmap, addr:0x%x, len:%d,pid:%d,comm:%s\n",
			__func__, __LINE__,di.addr, di.len,current->pid,current->comm);
#endif
	mutex_lock(&vpu->mem_lock);
	list_for_each_safe(pos, next, &vpu->mem_list) {
		us_node = list_entry(pos, struct user_space_node, mem_list);
		if (us_node->vaddr == di.addr) {
			if (us_node->dmmu_map_flag == MAPED) {
				ret = dmmu_unmap(dev->this_device,di.addr,di.len);
				if (ret) {
					printk("dmmu_unmap error,addr:0x%lx\n",
							us_node->vaddr);
					return ret;
				}
				us_node->dmmu_map_flag = UNMAP;
				mutex_unlock(&vpu->mem_lock);

				return 0;
			} else {
				printk("Error:This addr:0x%x has been unmapped\n", di.addr);
				mutex_unlock(&vpu->mem_lock);
				ret = -EFAULT;
				return ret;
			}
		}
	}

	mutex_unlock(&vpu->mem_lock);

	return ret;
}

static long vpu_ioctl(struct file *filp, unsigned int cmd, unsigned long arg)
{
	struct miscdevice *dev = filp->private_data;
	struct jz_vpu *vpu = container_of(dev, struct jz_vpu, mdev);
	struct flush_cache_info info;
	unsigned int status = 0;
	volatile unsigned long flags;
	int ret = 0;

	switch (cmd) {
	case CMD_VPU_OPEN:
		pr_dbg_mem("###########%s %d,[%d:%d],comm:%s, open vpu\n",
				__func__, __LINE__,current->tgid,current->pid,current->comm);
		break;
	case CMD_VPU_CLOSE:
		pr_dbg_mem("###########%s %d,[%d:%d],comm:%s, close vpu\n",
				__func__, __LINE__,current->tgid,current->pid,current->comm);
		return vpu_release_resource(vpu);
	case WAIT_COMPLETE:
		pr_dbg_mem("###########%s %d,[%d:%d],comm:%s, wait complete\n",
				__func__, __LINE__,current->tgid,current->pid,current->comm);
		ret = wait_for_completion_interruptible_timeout(
			&vpu->done, msecs_to_jiffies(200));
		if (ret > 0) {
		        status = vpu->status;
		} else {
			dev_warn(vpu->dev, "[%d:%d] wait_for_completion timeout\n",
				 current->tgid, current->pid);
			status = vpu_readl(vpu,REG_VPU_STAT);
			/*dump_vpu_vpu_regs();*/
			if (vpu_reset(vpu) < 0) {
				printk("Reset vpu error\n");
				status = 0;
			}
			vpu->done.done = 0;
		}
		if (copy_to_user((void *)arg, &status, sizeof(status)))
			ret = -EFAULT;
		break;

	case LOCK:
		pr_dbg_mem("###########%s %d,[%d:%d],comm:%s, lock vpu\n",
				__func__, __LINE__,current->tgid,current->pid,current->comm);
		ret = vpu_lock(vpu);
		if (ret) {
			dev_err(vpu->dev, "[%d:%d] lock vpu error!\n",
				current->tgid, current->pid);
		} else
			dev_dbg(vpu->dev, "[%d:%d] lock vpu_driver\n",
					current->tgid, current->pid);
#if 0
		if (vpu->owner_pid == current->pid) {
			dev_err(vpu->dev, "[%d:%d] dead lock\n",
				current->tgid, current->pid);
			ret = -EINVAL;
			break;
		}

		if (mutex_lock_interruptible(&vpu->mutex) != 0) {
			dev_err(vpu->dev, "[%d:%d] lock error!\n",
				current->tgid, current->pid);
			ret = -EIO;
			break;
		}
		vpu->owner_pid = current->pid;
		dev_dbg(vpu->dev, "[%d:%d] lock\n", current->tgid, current->pid);
#endif
		break;
	case UNLOCK:
		pr_dbg_mem("###########%s %d,[%d:%d],comm:%s, unlock vpu\n",
				__func__, __LINE__,current->tgid,current->pid,current->comm);
		ret= vpu_unlock(vpu);
		if (ret) {
			dev_err(vpu->dev, "[%d:%d] lock vpu error!\n",
				current->tgid, current->pid);
		} else
			dev_dbg(vpu->dev, "[%d:%d] unlock vpu_driver\n",
					current->tgid, current->pid);
#if 0
		mutex_unlock(&vpu->mutex);
		vpu->owner_pid = 0;
		dev_dbg(vpu->dev, "[%d:%d] unlock\n", current->tgid, current->pid);
#endif
		break;

	case FLUSH_CACHE:
		pr_dbg_mem("###########%s %d,[%d:%d],comm:%s, cache flush\n",
				__func__, __LINE__,current->tgid,current->pid,current->comm);
		if (copy_from_user(&info, (void *)arg, sizeof(info))) {
			ret = -EFAULT;
			break;
		}

		dma_cache_sync(NULL, (void *)info.addr, info.len, info.dir);
		dev_dbg(vpu->dev, "[%d:%d] flush cache\n", current->tgid, current->pid);
		break;

	case CMD_VPU_RESET:
		pr_dbg_mem("###########%s %d,[%d:%d],comm:%s, reset vpu\n",
				__func__, __LINE__,current->tgid,current->pid,current->comm);
		spin_lock_irqsave(&vpu->spin_lock, flags);

		cpm_set_bit(30, CPM_SRBC);
		while(!(REG_CPM_VPU_SWRST & CPM_VPU_ACK))
			;

		REG_CPM_VPU_SWRST = ((REG_CPM_VPU_SWRST | CPM_VPU_SR) & ~CPM_VPU_STP);
		REG_CPM_VPU_SWRST = (REG_CPM_VPU_SWRST & ~CPM_VPU_SR & ~CPM_VPU_STP);

		spin_unlock_irqrestore(&vpu->spin_lock, flags);
		break;
	case CMD_VPU_REQUEST_MEM:
		pr_dbg_mem("###########%s %d,[%d:%d],comm:%s, request mem for vpu\n",
				__func__, __LINE__,current->tgid,current->pid,current->comm);
		return vpu_request_mem(vpu, arg);
	case CMD_VPU_RELEASE_MEM:
		pr_dbg_mem("###########%s %d,[%d:%d],comm:%s, release mem for vpu\n",
				__func__, __LINE__,current->tgid,current->pid,current->comm);
		return vpu_release_mem(vpu, arg);
	case CMD_VPU_RELEASE_ALL_MEM:
		pr_dbg_mem("###########%s %d,[%d:%d],comm:%s, release all mem for vpu\n",
				__func__, __LINE__,current->tgid,current->pid,current->comm);
		return vpu_release_all_mem(vpu, 0);
	case CMD_VPU_DMMU_MAP:
		pr_dbg_mem("###########%s %d,[%d:%d],comm:%s, dmmu map for vpu\n",
				__func__, __LINE__,current->tgid,current->pid,current->comm);
		return vpu_dmmu_map(vpu, arg);
	case CMD_VPU_DMMU_UNMAP:
		pr_dbg_mem("###########%s %d,[%d:%d],comm:%s, dmmu unmap for vpu\n",
				__func__, __LINE__,current->tgid,current->pid,current->comm);
#if 1
		spin_lock_irqsave(&vpu->spin_lock, flags);
		REG_CPM_VPU_SWRST |= CPM_VPU_STP;
		while(!(REG_CPM_VPU_SWRST & CPM_VPU_ACK))
			;
		REG_CPM_VPU_SWRST = ((REG_CPM_VPU_SWRST | CPM_VPU_SR) & ~CPM_VPU_STP);
		REG_CPM_VPU_SWRST = (REG_CPM_VPU_SWRST & ~CPM_VPU_SR & ~CPM_VPU_STP);

		spin_unlock_irqrestore(&vpu->spin_lock, flags);
#endif
		return vpu_dmmu_unmap(vpu, arg);
	case CMD_VPU_DMMU_UNMAP_ALL:
		pr_dbg_mem("###########%s %d,[%d:%d],comm:%s, dmmu unmap all for vpu\n",
				__func__, __LINE__,current->tgid,current->pid,current->comm);
#if 1
		spin_lock_irqsave(&vpu->spin_lock, flags);
		REG_CPM_VPU_SWRST |= CPM_VPU_STP;
		while(!(REG_CPM_VPU_SWRST & CPM_VPU_ACK))
			;
		REG_CPM_VPU_SWRST = ((REG_CPM_VPU_SWRST | CPM_VPU_SR) & ~CPM_VPU_STP);
		REG_CPM_VPU_SWRST = (REG_CPM_VPU_SWRST & ~CPM_VPU_SR & ~CPM_VPU_STP);

		spin_unlock_irqrestore(&vpu->spin_lock, flags);
#endif
		vpu_dmmu_unmap_all(vpu);
		break;
	default:
		break;
	}

	return ret;
}

static int vpu_mmap(struct file *file, struct vm_area_struct *vma)
{
	if( PFN_PHYS(vma->vm_pgoff) < 0x13200000 || PFN_PHYS(vma->vm_pgoff) >= 0x13300000){
		printk("phy addr err ,range is 0x13200000 - 13300000");
		return -EAGAIN;
	}
	vma->vm_flags |= VM_IO;
	vma->vm_page_prot = pgprot_noncached(vma->vm_page_prot);

	if (io_remap_pfn_range(vma,vma->vm_start,
			       vma->vm_pgoff,
			       vma->vm_end - vma->vm_start,
			       vma->vm_page_prot))
		return -EAGAIN;

	return 0;
}

static irqreturn_t vpu_interrupt(int irq, void *dev)
{
	struct jz_vpu *vpu = dev;
	unsigned int vpu_stat;
	//	vpu->status = 0;

	vpu_stat = vpu_readl(vpu,REG_VPU_STAT);
#define CLEAR_VPU_BIT(vpu,offset,bm)				\
	do {							\
		unsigned int stat;				\
		stat = vpu_readl(vpu,offset);		\
		vpu_writel(vpu,offset,stat & ~(bm));	\
	} while(0)

	if(vpu_stat & VPU_STAT_SLDERR) {
		dev_err(vpu->dev, "SHLD error!\n");
		CLEAR_VPU_BIT(vpu,REG_VPU_GLBC,
			      (VPU_INTE_ACFGERR |
			       VPU_INTE_TLBERR |
			       VPU_INTE_BSERR |
			       VPU_INTE_ENDF
			       ));

	} else if(vpu_stat & VPU_STAT_TLBERR) {
		dev_err(vpu->dev, "TLB error!\n");
		CLEAR_VPU_BIT(vpu,REG_VPU_GLBC,
			      (VPU_INTE_ACFGERR |
			       VPU_INTE_TLBERR |
			       VPU_INTE_BSERR |
			       VPU_INTE_ENDF
			       ));

	} else if(vpu_stat & VPU_STAT_BSERR) {
		dev_err(vpu->dev, "BS error!\n");
		CLEAR_VPU_BIT(vpu,REG_VPU_GLBC,
			      (VPU_INTE_ACFGERR |
			       VPU_INTE_TLBERR |
			       VPU_INTE_BSERR |
			       VPU_INTE_ENDF
			       ));

	} else if(vpu_stat & VPU_STAT_ACFGERR) {
		dev_err(vpu->dev, "ACFG error!\n");
		CLEAR_VPU_BIT(vpu,REG_VPU_GLBC,
			      (VPU_INTE_ACFGERR |
			       VPU_INTE_TLBERR |
			       VPU_INTE_BSERR |
			       VPU_INTE_ENDF
			       ));

	} else if(vpu_stat & VPU_STAT_TIMEOUT) {
		dev_err(vpu->dev, "TIMEOUT error!\n");
		CLEAR_VPU_BIT(vpu,REG_VPU_GLBC,
			      (VPU_INTE_ACFGERR |
			       VPU_INTE_TLBERR |
			       VPU_INTE_BSERR |
			       VPU_INTE_ENDF
			       ));

	} else if(vpu_stat & VPU_STAT_ENDF) {
		if(vpu_stat & VPU_STAT_JPGEND) {
			dev_dbg(vpu->dev, "JPG successfully done!\n");
			CLEAR_VPU_BIT(vpu,REG_VPU_JPGC_STAT,JPGC_STAT_ENDF);
		} else {
			dev_dbg(vpu->dev, "SCH successfully done!\n");
			CLEAR_VPU_BIT(vpu,REG_VPU_SDE_STAT,SDE_STAT_BSEND);
			CLEAR_VPU_BIT(vpu,REG_VPU_DBLK_STAT,DBLK_STAT_DOEND);
		}

	} else {
		if(vpu_readl(vpu,REG_VPU_AUX_STAT) & AUX_STAT_MIRQP) {
			dev_dbg(vpu->dev, "AUX successfully done!\n");
			CLEAR_VPU_BIT(vpu,REG_VPU_AUX_STAT,AUX_STAT_MIRQP);
		} else {
			dev_dbg(vpu->dev, "illegal interrupt happened!\n");
			return IRQ_HANDLED;
		}
	}

	vpu->status = vpu_stat;
	complete(&vpu->done);

	return IRQ_HANDLED;
}

static struct file_operations vpu_misc_fops = {
	.open		= vpu_open,
	.release	= vpu_release,
	.unlocked_ioctl	= vpu_ioctl,
	.mmap		= vpu_mmap,
};

static int vpu_probe(struct platform_device *pdev)
{
	int ret;
	struct resource			*res;
	struct jz_vpu *vpu;

	if (!pdev->dev.of_node) {
		printk("device-tree data is missing[%s,%d]\n",__func__,__LINE__);
		return -ENXIO;
	}

	vpu = kzalloc(sizeof(struct jz_vpu), GFP_KERNEL);
	if (!vpu)
		ret = -ENOMEM;


	vpu->irq = platform_get_irq(pdev,0);
	if(vpu->irq < 0) {
		dev_err(&pdev->dev, "get irq failed\n");
		ret = vpu->irq;
		goto err_get_mem;
	}

	ret = request_irq(vpu->irq, vpu_interrupt,
			  IRQF_DISABLED, "jz4780-vpu", vpu);
	if (ret) {
		dev_err(&pdev->dev, "irq request failed\n");
		goto err_request_irq;
	}
	disable_irq_nosync(vpu->irq);


	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!res) {
		dev_err(&pdev->dev, "No iomem resource\n");
		ret = -ENXIO;
		goto err_get_mem;
	}

	vpu->iomem = ioremap(res->start, resource_size(res));

	if (!vpu->iomem) {
		dev_err(&pdev->dev, "ioremap failed\n");
		ret = -ENXIO;
		goto err_get_mem;
	}

	vpu->clk = clk_get(&pdev->dev,"vpu_clk");
	if (IS_ERR(vpu->clk)) {
		ret = PTR_ERR(vpu->clk);
		goto err_get_clk_cgu;
	}

	clk_prepare_enable(vpu->clk);
	/*
	 * for jz4775, when vpu freq is set over 300M, the decode process
	 * of vpu may be error some times which can led to graphic abnomal
	 */

#if defined(CONFIG_SOC_4775)
	clk_set_rate(vpu->clk,250000000);
#else
	clk_set_rate(vpu->clk,300000000);
#endif

	vpu->dev = &pdev->dev;
	vpu->mdev.minor = MISC_DYNAMIC_MINOR;
	vpu->mdev.name =  "jz-vpu";
	vpu->mdev.fops = &vpu_misc_fops;

	mutex_init(&vpu->lock);
	mutex_init(&vpu->mem_lock);

	ret = misc_register(&vpu->mdev);
	if (ret < 0) {
		dev_err(&pdev->dev, "misc_register failed\n");
		goto err_registe_misc;
	}
	platform_set_drvdata(pdev, vpu);

	wakeup_source_init(&vpu->vpu_wake_lock, "vpu_wake_lock");

	mutex_init(&vpu->mutex);

	init_completion(&vpu->done);

	vpu->cpm_pwc = cpm_pwc_get(PWC_VPU);
	if(!vpu->cpm_pwc) {
		dev_err(&pdev->dev, "get %s fail!\n",PWC_VPU);
		goto err_request_power;
	}


	spin_lock_init(&vpu->spin_lock);

	return 0;
err_request_power:
err_request_irq:
	misc_deregister(&vpu->mdev);
err_registe_misc:
	clk_put(vpu->clk);
err_get_clk_cgu:
	iounmap(vpu->iomem);
err_get_mem:
	kfree(vpu);
	return ret;
}

static int vpu_remove(struct platform_device *dev)
{
	struct jz_vpu *vpu = platform_get_drvdata(dev);
	if(vpu->cpm_pwc)
		cpm_pwc_put(vpu->cpm_pwc);
	misc_deregister(&vpu->mdev);
	wakeup_source_trash(&vpu->vpu_wake_lock);
	clk_put(vpu->clk);
	/*clk_put(vpu->clk_gate);*/
	free_irq(vpu->irq, vpu);
	iounmap(vpu->iomem);
	kfree(vpu);

	return 0;
}
static struct of_device_id jz4780_of_match[] = {
		{ .compatible = "ingenic,jz4780-vpu", },
		{ },
};
MODULE_DEVICE_TABLE(of, jz4780_of_match);

static struct platform_driver jz_vpu_driver = {
	.probe		= vpu_probe,
	.remove		= vpu_remove,
	.driver		= {
		.name	= "jz4780-vpu",
		.of_match_table = jz4780_of_match,
	},
};

static int __init vpu_init(void)
{
	return platform_driver_register(&jz_vpu_driver);
}

static void __exit vpu_exit(void)
{
	platform_driver_unregister(&jz_vpu_driver);
}

module_init(vpu_init);
module_exit(vpu_exit);

MODULE_DESCRIPTION("JZ4780 VPU driver");
MODULE_AUTHOR("Large Dipper <ykli@ingenic.cn>");
MODULE_LICENSE("GPL v2");
MODULE_VERSION("20120925");
