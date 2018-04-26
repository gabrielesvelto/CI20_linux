/* arch/mips/xburst/soc-m200/common/libdmmu.c
 * This driver is used by VPU driver.
 *
 * Copyright (C) 2015 Ingenic Semiconductor Co., Ltd.
 *	http://www.ingenic.com
 * Author:	Yan Zhengting <zhengting.yan@ingenic.com>
 * Modify by:	Sun Jiwei <jiwei.sun@ingenic.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 *
 */

#include <linux/kernel.h>
#include <linux/list.h>
#include <linux/platform_device.h>
#include <linux/fs.h>
#include <linux/file.h>
#include <linux/mm.h>
#include <linux/list.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/proc_fs.h>
#include <linux/debugfs.h>
#include <linux/mempolicy.h>
#include <linux/mm_types.h>
#include <asm/io.h>
#include <asm/uaccess.h>
#include <asm/current.h>
#include <asm/cacheflush.h>
#include <asm/tlbflush.h>
#include <asm/io.h>
#include <linux/vmalloc.h>
#include <linux/sched.h>
#include <dt-bindings/soc/jz4780/jz_proc.h>
#include <dt-bindings/soc/jz4780/libdmmu.h>

/*#define DEBUG*/

#define MAP_COUNT		0x10
#define MAP_CONUT_MASK		0xff0

#define DMMU_PTE_VLD 		0x01
#define DMMU_PMD_VLD 		0x01

#define KSEG0_LOW_LIMIT		0x80000000
#define KSEG1_HEIGH_LIMIT	0xC0000000

LIST_HEAD(handle_list);
static unsigned long reserved_pte = 0;
static unsigned long res_pte_paddr;

static unsigned int * my_tlb_base_v;
static unsigned int * my_tlb_base_p;

struct pmd_node {
	unsigned int count;
	unsigned long index;
	unsigned long page;
	struct list_head list;
};

struct map_node {
	struct device *dev;
	unsigned long start;
	unsigned long len;
	struct list_head list;
};

struct dmmu_handle {
	pid_t tgid;
	unsigned long pdg;
	struct mutex lock;
	struct list_head list;
	struct list_head pmd_list;
	struct list_head map_list;
};

static struct proc_dir_entry *proc_jz_root;
struct proc_dir_entry * jz_proc_mkdir(char *s)
{
	struct proc_dir_entry *p;
	if(!proc_jz_root) {
		proc_jz_root = proc_mkdir("jz", 0);
		if(!proc_jz_root)
			return NULL;
	}
	p = proc_mkdir(s,proc_jz_root);
	return p;
}

struct proc_dir_entry * get_jz_proc_root(void)
{
	if(!proc_jz_root) {
		proc_jz_root = proc_mkdir("jz", 0);
		if(!proc_jz_root)
			return NULL;
	}
	return proc_jz_root;
}

static struct map_node *check_map(struct dmmu_handle *h,unsigned long vaddr,unsigned long len)
{
	struct list_head *pos, *next;
	struct map_node *n;
	list_for_each_safe(pos, next, &h->map_list) {
		n = list_entry(pos, struct map_node, list);
		if(vaddr == n->start && len == n->len)
			return n;
	}
	return NULL;
}

static void handle_add_map(struct device *dev,struct dmmu_handle *h,unsigned long vaddr,unsigned long len)
{
	struct map_node *n = kmalloc(sizeof(*n),GFP_KERNEL);
	n->dev = dev;
	n->start = vaddr;
	n->len = len;
	INIT_LIST_HEAD(&n->list);
	list_add(&n->list,&h->map_list);
}

static unsigned int get_pfn(unsigned int vaddr)
{
	pgd_t *pgdir;
	pmd_t *pmdir;
	pte_t *pte;
	pgdir = pgd_offset(current->mm, vaddr);
	if(pgd_none(*pgdir) || pgd_bad(*pgdir))
		return 0;
	pmdir = pmd_offset((pud_t *)pgdir, vaddr);
	if(pmd_none(*pmdir) || pmd_bad(*pmdir))
		return 0;
	pte = pte_offset(pmdir,vaddr);
	if (pte_present(*pte)) {
		return pte_pfn(*pte) << PAGE_SHIFT;
	}
	return 0;
}

static unsigned long dmmu_v2pfn(unsigned long vaddr)
{
	if(vaddr < KSEG0_LOW_LIMIT)
		return get_pfn(vaddr);

	if(vaddr >= KSEG0_LOW_LIMIT && vaddr < KSEG1_HEIGH_LIMIT)
		return virt_to_phys((void *)vaddr);

	panic("dmmu_v2pfn error!");
	return 0;
}

static unsigned long unmap_node(struct pmd_node *n,unsigned long vaddr,unsigned long end,int check)
{
	unsigned int *pte = (unsigned int *)n->page;
	int index = ((vaddr & 0x3ff000) >> 12);
	int free = !check || (--n->count == 0);
	if(vaddr && end) {
		while(index < 1024 && vaddr < end) {
			if(pte[index] & MAP_CONUT_MASK) {
				pte[index] -= MAP_COUNT;
			} else {
				ClearPageReserved(virt_to_page((void *)vaddr));
				pte[index] = reserved_pte;
			}
			*(my_tlb_base_v + (vaddr>>12)) = pte[index];
			index++;
			vaddr += 4096;
		}
	}

	if(free) {
		ClearPageReserved(virt_to_page((void *)n->page));
		free_page(n->page);
		list_del(&n->list);
		kfree(n);
		return vaddr+(1024-index)*4096;
	}

	return vaddr;
}

static unsigned long map_node(struct pmd_node *n,unsigned int vaddr,unsigned int end)
{
	unsigned int *pte = (unsigned int *)n->page;
	int index = ((vaddr & 0x3ff000) >> 12);

	while(index < 1024 && vaddr < end) {
		if(pte[index] == reserved_pte) {
			SetPageReserved(virt_to_page((void *)vaddr));
			pte[index] = dmmu_v2pfn(vaddr) | DMMU_PTE_VLD;
		} else {
			pte[index] += MAP_COUNT;
		}
		*(my_tlb_base_v + (vaddr>>12)) = pte[index];
		index++;
		vaddr += 4096;
	}
	n->count++;
	return vaddr;
}

static struct pmd_node *find_node(struct dmmu_handle *h,unsigned int vaddr)
{
	struct list_head *pos, *next;
	struct pmd_node *n;

	list_for_each_safe(pos, next, &h->pmd_list) {
		n = list_entry(pos, struct pmd_node, list);
		if(n->index == (vaddr & 0xffc00000))
			return n;
	}
	return NULL;
}

static struct pmd_node *add_node(struct dmmu_handle *h,unsigned int vaddr)
{
	int i;
	unsigned long *pte;
	unsigned long *pgd = (unsigned long *)h->pdg;
	struct pmd_node *n = kmalloc(sizeof(*n),GFP_KERNEL);
	INIT_LIST_HEAD(&n->list);
	n->count = 0;
	n->index = vaddr & 0xffc00000;
	n->page = __get_free_page(GFP_KERNEL);
	SetPageReserved(virt_to_page((void *)n->page));

	pte = (unsigned long *)n->page;
	for(i=0;i<1024;i++)
		pte[i] = reserved_pte;

	list_add(&n->list, &h->pmd_list);

	pgd[vaddr>>22] = dmmu_v2pfn(n->page) | DMMU_PMD_VLD;
	return n;
}

static struct dmmu_handle *find_handle(void)
{
	struct list_head *pos, *next;
	struct dmmu_handle *h;

	list_for_each_safe(pos, next, &handle_list) {
		h = list_entry(pos, struct dmmu_handle, list);
		if(h->tgid == current->tgid)
			return h;
	}
	return NULL;
}

static struct dmmu_handle *create_handle(void)
{
	struct dmmu_handle *h;
	unsigned int pgd_index;
	unsigned long *pgd;

	h = kmalloc(sizeof(struct dmmu_handle),GFP_KERNEL);
	if(!h)
		return NULL;

	h->tgid = current->tgid;
	h->pdg = __get_free_page(GFP_KERNEL);
	if(!h->pdg) {
		pr_err("%s %d, Get free page for PGD error\n",
				__func__, __LINE__);
		kfree(h);
		return NULL;
	}
	SetPageReserved(virt_to_page((void *)h->pdg));

	pgd = (unsigned long *)h->pdg;

	for (pgd_index=0; pgd_index < PTRS_PER_PGD; pgd_index++)
		pgd[pgd_index] = res_pte_paddr;

	INIT_LIST_HEAD(&h->list);
	INIT_LIST_HEAD(&h->pmd_list);
	INIT_LIST_HEAD(&h->map_list);
	list_add(&h->list, &handle_list);

	mutex_init(&h->lock);
	return h;
}

static int dmmu_make_present(unsigned long addr,unsigned long end)
{
#if 0
	unsigned long i;
	for(i = addr; i < end; i += 4096) {
		*(volatile unsigned char *)(i) = 0;
	}
	*(volatile unsigned char *)(end - 1) = 0;
	return 0;
#else
	int ret, len, write;
	struct vm_area_struct * vma;
	unsigned long vm_page_prot;

	vma = find_vma(current->mm, addr);
	if (!vma) {
		printk("dmmu_make_present error. addr=%lx len=%lx\n",addr,end-addr);
		return -1;
	}

	if(vma->vm_flags & VM_PFNMAP)
		return 0;

	write = (vma->vm_flags & VM_WRITE) != 0;
	BUG_ON(addr >= end);
	BUG_ON(end > vma->vm_end);

	vm_page_prot = pgprot_val(vma->vm_page_prot);
	vma->vm_page_prot = __pgprot(vm_page_prot | _PAGE_VALID| _PAGE_ACCESSED | _PAGE_PRESENT);

	len = DIV_ROUND_UP(end, PAGE_SIZE) - addr/PAGE_SIZE;
	ret = get_user_pages(current, current->mm, addr,
			len, write, 0, NULL, NULL);
	vma->vm_page_prot = __pgprot(vm_page_prot);
	if (ret < 0) {
		printk("dmmu_make_present get_user_pages error(%d). addr=%lx len=%lx\n",0-ret,addr,end-addr);
		return ret;
	}
	return ret == len ? 0 : -1;
#endif
}

static void dmmu_cache_wback(struct dmmu_handle *h)
{
	struct list_head *pos, *next;
	struct pmd_node *n;

	dma_cache_wback(h->pdg,PAGE_SIZE);

	list_for_each_safe(pos, next, &h->pmd_list) {
		n = list_entry(pos, struct pmd_node, list);
		dma_cache_wback(n->page,PAGE_SIZE);
	}
}

static void dmmu_dump_handle(struct seq_file *m, void *v, struct dmmu_handle *h);

unsigned long dmmu_map(struct device *dev,unsigned long vaddr,unsigned long len)
{
	int end = vaddr + len;
	struct dmmu_handle *h;
	struct pmd_node *node;

	h = find_handle();
	if(!h)
		h = create_handle();
	if(!h)
		return 0;

	mutex_lock(&h->lock);
#ifdef DEBUG
	printk("(pid %d)dmmu_map %lx %lx================================================\n",h->tgid,vaddr,len);
#endif
	if(check_map(h,vaddr,len))
	{
		mutex_unlock(&h->lock);
		return dmmu_v2pfn(h->pdg);
	}

	if(dmmu_make_present(vaddr,vaddr+len))
	{
		mutex_unlock(&h->lock);
		return 0;
	}

	handle_add_map(dev,h,vaddr,len);

	while(vaddr < end) {
		node = find_node(h,vaddr);
		if(!node) {
			node = add_node(h,vaddr);
		}
		vaddr = map_node(node,vaddr,end);
	}
	dmmu_cache_wback(h);
#ifdef DEBUG
	dmmu_dump_handle(NULL,NULL,h);
#endif
	mutex_unlock(&h->lock);

	/*return dmmu_v2pfn(h->pdg);*/
	return (unsigned long)my_tlb_base_p;
}

int dmmu_unmap(struct device *dev,unsigned long vaddr, int len)
{
	unsigned long end = vaddr + len;
	struct dmmu_handle *h;
	struct pmd_node *node;
	struct map_node *n;

	h = find_handle();
	if(!h)
		return 0;

	mutex_lock(&h->lock);
#ifdef DEBUG
	printk("dmmu_unmap %lx %x**********************************************\n",vaddr,len);
#endif
	n = check_map(h,vaddr,len);
	if(!n) {
		mutex_unlock(&h->lock);
		return -EAGAIN;
	}
	if(n->dev != dev) {
		mutex_unlock(&h->lock);
		return -EAGAIN;
	}

	list_del(&n->list);
	kfree(n);

	while(vaddr < end) {
		node = find_node(h,vaddr);
		if(node)
			vaddr = unmap_node(node,vaddr,end,1);
	}

	if(list_empty(&h->pmd_list) && list_empty(&h->map_list)) {
		list_del(&h->list);
		ClearPageReserved(virt_to_page((void *)h->pdg));
		free_page(h->pdg);
		mutex_unlock(&h->lock);
		kfree(h);
		return 0;
	}

	mutex_unlock(&h->lock);
	return 0;
}

int dmmu_free_all(struct device *dev)
{
	struct dmmu_handle *h;
	struct pmd_node *node;
	struct map_node *cn;
	struct list_head *pos, *next;

	h = find_handle();
	if(!h)
		return 0;

	mutex_lock(&h->lock);

	list_for_each_safe(pos, next, &h->map_list) {
		cn = list_entry(pos, struct map_node, list);
		node = find_node(h,cn->start);
		if(node)
			unmap_node(node,cn->start,cn->len,1);
		list_del(&cn->list);
		kfree(cn);
	}

	list_for_each_safe(pos, next, &h->pmd_list) {
		node = list_entry(pos, struct pmd_node, list);
		if(node){
			printk("WARN: pmd list should NULL\n");
			unmap_node(node,0,0,0);
		}
	}

	list_del(&h->list);
	ClearPageReserved(virt_to_page((void *)h->pdg));
	free_page(h->pdg);
	kfree(h);

	mutex_unlock(&h->lock);
	return 0;
}

int dmmu_unmap_all(struct device *dev)
{
	struct dmmu_handle *h;
	struct map_node *cn;
	struct list_head *pos, *next;

#ifdef DEBUG
	printk("dmmu_unmap_all\n");
#endif
	h = find_handle();
	if(!h)
		return 0;

	list_for_each_safe(pos, next, &h->map_list) {
		cn = list_entry(pos, struct map_node, list);
		if(dev == cn->dev)
			dmmu_unmap(dev,cn->start,cn->len);
	}


	if(list_empty(&h->map_list))
		dmmu_free_all(dev);
	return 0;
}

static inline void * i_memset32(void *vbase, unsigned int value32, int total_size)
{
	unsigned int * s32;
	int iii, isize;
	s32 = (unsigned int *)vbase;
	isize = total_size/(sizeof(unsigned int));
	for (iii=0; iii<isize; iii++) {
		*s32++ = value32;
	}
	return (void*)s32;
}

int __init dmmu_init(void)
{
	unsigned int pte_index;
	unsigned long res_page_paddr;
	unsigned long reserved_page;
	unsigned int *res_pte_vaddr;

	reserved_page = __get_free_page(GFP_KERNEL);
	if (!reserved_page) {
		pr_err("%s %d, Get reserved page error\n",
				__func__, __LINE__);
		return ENOMEM;
	}
	SetPageReserved(virt_to_page((void *)reserved_page));
	reserved_pte = dmmu_v2pfn(reserved_page) | DMMU_PTE_VLD;

	res_page_paddr = virt_to_phys((void *)reserved_page) | 0xFFF;

	res_pte_vaddr = (unsigned int *)__get_free_page(GFP_KERNEL);
	if (!res_pte_vaddr) {
		pr_err("%s %d, Get free page for PTE error\n",
				__func__, __LINE__);
		free_page(reserved_page);
		return ENOMEM;
	}
	SetPageReserved(virt_to_page(res_pte_vaddr));
	res_pte_paddr = virt_to_phys((void *)res_pte_vaddr) | DMMU_PTE_VLD;

	for (pte_index = 0; pte_index < PTRS_PER_PTE; pte_index++)
		res_pte_vaddr[pte_index] = res_page_paddr;

#if 1
	printk("[%s,%d]\n\n\n",__func__,__LINE__);
	unsigned int total_size;
	unsigned int i;
	total_size = 2*1024*1024;
	my_tlb_base_v = kmalloc(total_size, GFP_KERNEL);
	if(my_tlb_base_v == NULL) {
		pr_err("%s %d, Get jz_dmmu vbase failed!\n",
				__func__, __LINE__);
	}
	my_tlb_base_v =(unsigned int *)(((unsigned int)my_tlb_base_v + PAGE_SIZE - 1)&(~(PAGE_SIZE-1)));
	my_tlb_base_p = virt_to_phys((void *)my_tlb_base_v);
	printk("---> my_tlb_base_v = 0x%08x\n", my_tlb_base_v);
	printk("---> my_tlb_base_p = 0x%08x\n", my_tlb_base_p);
	printk("---> res_page_paddr= 0x%08x\n", res_page_paddr);

	i_memset32((void *)my_tlb_base_v, res_page_paddr, total_size);
	dma_cache_wback(my_tlb_base_v, total_size);
	printk("---> my_tlb_base_v[222222] = 0x%08x\n",*my_tlb_base_v);
	/*for(i=0; i<total_size; i++)
	{
		*my_tlb_base_v++ = res_page_paddr;
	}
	printk("ly-test ---> my_tlb_base_v[222222] = 0x%08x\n",*my_tlb_base_v);*/
#endif
	return 0;
}
arch_initcall(dmmu_init);

void dmmu_dump_vaddr(unsigned long vaddr)
{
	struct dmmu_handle *h;
	struct pmd_node *node;

	unsigned long *pmd,*pte;
	h = find_handle();
	if(!h) {
		printk("dmmu_dump_vaddr %08lx error - h not found!\n",vaddr);
		return;
	}

	node = find_node(h,vaddr);
	if(!node) {
		printk("dmmu_dump_vaddr %08lx error - node not found!\n",vaddr);
		return;
	}

	pmd = (unsigned long *)h->pdg;
	pte = (unsigned long *)node->page;

	printk("pmd base = %p; pte base = %p\n",pmd,pte);
	printk("pmd = %08lx; pte = %08lx\n",pmd[vaddr>>22],pte[(vaddr&0x3ff000)>>12]);
}

static void dmmu_dump_handle(struct seq_file *m, void *v, struct dmmu_handle *h)
{
	struct list_head *pos, *next;
	struct map_node *n;

	list_for_each_safe(pos, next, &h->map_list) {
		n = list_entry(pos, struct map_node, list);
		{
			int i = 0;
			int vaddr = n->start;
			struct pmd_node *pn = find_node(h,vaddr);
			unsigned int *pte = (unsigned int *)pn->page;

			while(vaddr < (n->start + n->len)) {
				if(i++%8 == 0)
					printk("\nvaddr %08x : ",vaddr & 0xfffff000);
				printk("%08x ",pte[(vaddr & 0x3ff000)>>12]);
				vaddr += 4096;
			}
			printk("\n\n");
		}
	}
}

static int dmmu_proc_show(struct seq_file *m, void *v)
{
	struct list_head *pos, *next;
	struct dmmu_handle *h;
	volatile unsigned long flags;
	local_irq_save(flags);
		list_for_each_safe(pos, next, &handle_list) {
			h = list_entry(pos, struct dmmu_handle, list);
			dmmu_dump_handle(m, v, h);
		}
	local_irq_restore(flags);
	return 0;
}

static int dmmu_open(struct inode *inode, struct file *file)
{
	return single_open(file, dmmu_proc_show, PDE_DATA(inode));
}

static const struct file_operations dmmus_proc_fops ={
	.read = seq_read,
	.open = dmmu_open,
	.llseek = seq_lseek,
	.release = single_release,
};

static int __init init_dmmu_proc(void)
{
	struct proc_dir_entry *p;
	p = jz_proc_mkdir("dmmu");
	if (!p) {
		pr_warning("create_proc_entry for common dmmu failed.\n");
		return -ENODEV;
	}
	proc_create("dmmus", 0600,p,&dmmus_proc_fops);

	return 0;
}

module_init(init_dmmu_proc);
