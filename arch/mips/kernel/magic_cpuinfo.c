/*
* Copyright (C) 2016 Imagination Technologies
*
* This program is free software; you can redistribute it and/or modify it
* under the terms of the GNU General Public License as published by the
* Free Software Foundation; either version 2 of the License, or (at your
* option) any later version.
*/

#include <linux/proc_fs.h>
#include <linux/sched.h>
#include <linux/thread_info.h>
#include <linux/module.h>
#include <asm/uaccess.h>

#define BUF_LEN		64

static ssize_t magic_read_proc(struct file *file, char __user *user_buf,
						  size_t count, loff_t *ppos)
{
	struct thread_struct *mc_thread = &current->thread;
	char buf[BUF_LEN];
	unsigned int len;

	/* sprintf auto add '\0' */
	if (mc_thread->mcflags == CPU_ARM){
		len = sprintf(buf, "arm	 \n");
	}
	else if (mc_thread->mcflags == CPU_ARM_NEON){
		len = sprintf(buf, "arm_neon\n");
	}
	else {
		len = sprintf(buf, "mips	\n");
	}
	copy_to_user(user_buf, buf, len);

	return len;
}

static ssize_t magic_write_proc(struct file *file, const char __user *buffer,
							size_t count, loff_t *ppos)
{
	char buf[BUF_LEN];
	struct thread_struct *mc_thread = &current->thread;

	if (count > BUF_LEN)
			count = BUF_LEN;
	if (copy_from_user(buf, buffer, count))
			return -EFAULT;

	if (strncmp(buf, "arm_neon", 8) == 0) {
		mc_thread->mcflags = CPU_ARM_NEON;
	} else if (strncmp(buf, "arm", 3) == 0) {
		mc_thread->mcflags = CPU_ARM;
	} else if (strncmp(buf, "mips", 4) == 0) {
		mc_thread->mcflags = CPU_MIPS;
	}

	return count;
}

struct file_operations magic_fops = {
		.read = magic_read_proc,
		.write = magic_write_proc,
	};

static int __init init_proc_magic(void)
{
		proc_create("magic", 0666, NULL, &magic_fops);

		return 0;
}

module_init(init_proc_magic);
