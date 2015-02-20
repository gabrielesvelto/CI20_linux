/*
 * JZ4780 SMP
 *
 * Copyright (c) 2013 Imagination Technologies
 * Author: Paul Burton <paul.burton@imgtec.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of
 * the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston,
 * MA 02111-1307 USA
 */

#include <linux/clk.h>
#include <linux/clockchips.h>
#include <linux/interrupt.h>
#include <linux/of.h>
#include <linux/sched.h>
#include <linux/smp.h>
#include <asm/r4kcache.h>
#include <asm/smp-ops.h>
#include <asm/mach-jz4740/jz4780-smp.h>

static struct clk *cpu_clock_gates[CONFIG_NR_CPUS] = { NULL };

u32 jz4780_cpu_entry_sp;
u32 jz4780_cpu_entry_gp;

static DEFINE_SPINLOCK(smp_lock);

static irqreturn_t mbox_handler(int irq, void *dev_id)
{
	int cpu = smp_processor_id();
	u32 action, status;

	spin_lock(&smp_lock);

	switch (cpu) {
	case 0:
		action = read_c0_mailbox0();
		write_c0_mailbox0(0);
		break;
	case 1:
		action = read_c0_mailbox1();
		write_c0_mailbox1(0);
		break;
	default:
		panic("unhandled cpu %d!", cpu);
	}

	/* clear pending mailbox interrupt */
	status = read_c0_corestatus();
	status &= ~(CORESTATUS_MIRQ0P << cpu);
	write_c0_corestatus(status);

	spin_unlock(&smp_lock);

	if (action & SMP_RESCHEDULE_YOURSELF)
		scheduler_ipi();
	if (action & SMP_CALL_FUNCTION)
		smp_call_function_interrupt();

	return IRQ_HANDLED;
}

static struct irqaction mbox_action = {
	.handler = mbox_handler,
	.name = "core mailbox",
	.flags = IRQF_PERCPU | IRQF_NO_THREAD,
};

static void jz4780_smp_setup(void)
{
	u32 addr, reim;

	reim = read_c0_reim();

	/* mask mailbox interrupts for this core */
	reim &= ~REIM_MBOXIRQ0M;
	write_c0_reim(reim);

	/* clear mailboxes & pending mailbox IRQs */
	write_c0_mailbox0(0);
	write_c0_mailbox1(0);
	write_c0_corestatus(0);

	/* set reset entry point */
	addr = KSEG1ADDR((u32)&jz4780_secondary_cpu_entry);
	BUG_ON(addr & ~REIM_ENTRY);
	reim &= ~REIM_ENTRY;
	reim |= addr & REIM_ENTRY;

	/* unmask mailbox interrupts for this core */
	reim |= REIM_MBOXIRQ0M;
	write_c0_reim(reim);
	set_c0_status(STATUSF_IP3);
	irq_enable_hazard();
}

static void jz4780_smp_prepare_cpus(unsigned int max_cpus)
{
	struct device_node *cpu_node;
	unsigned cpu, ctrl;
	int err;

	/* setup the mailbox IRQ */
	setup_irq(MIPS_CPU_IRQ_BASE + 3, &mbox_action);

	init_cpu_present(cpu_possible_mask);

	ctrl = read_c0_corectrl();

	for (cpu = 0; cpu < max_cpus; cpu++) {
		__cpu_number_map[cpu] = cpu;
		__cpu_logical_map[cpu] = cpu;

		/* use reset entry point from REIM register */
		ctrl |= CORECTRL_RPC0 << cpu;
	}

	for_each_compatible_node(cpu_node, NULL, "ingenic,xburst") {
		err = of_property_read_u32_index(cpu_node, "reg", 0, &cpu);
		if (err) {
			pr_err("Failed to read index of %s\n",
			       cpu_node->full_name);
			continue;
		}

		cpu_clock_gates[cpu] = of_clk_get(cpu_node, 0);
		if (IS_ERR(cpu_clock_gates[cpu])) {
			cpu_clock_gates[cpu] = NULL;
			continue;
		}

		err = clk_prepare(cpu_clock_gates[cpu]);
		if (err)
			pr_err("Failed to prepare CPU clock gate\n");
	}

	write_c0_corectrl(ctrl);
}

static void jz4780_boot_secondary(int cpu, struct task_struct *idle)
{
	unsigned long flags;
	u32 ctrl;

	spin_lock_irqsave(&smp_lock, flags);

	/* ensure the core is in reset */
	ctrl = read_c0_corectrl();
	ctrl |= CORECTRL_SWRST0 << cpu;
	write_c0_corectrl(ctrl);

	/* ungate core clock */
	if (cpu_clock_gates[cpu])
		clk_enable(cpu_clock_gates[cpu]);

	/* power up the core */
	/* TODO: write a nice driver/abstraction for this */
	{
		void __iomem *lpcr = (void __iomem *)0xb0000004;
		writel(readl(lpcr) & ~(1 << 31), lpcr);
		/* wait for the CPU to be powered up */
		while (readl(lpcr) & (1 << 27))
			;
	}

	/* set entry sp/gp register values */
	jz4780_cpu_entry_sp = __KSTK_TOS(idle);
	jz4780_cpu_entry_gp = (u32)task_thread_info(idle);
	smp_wmb();

	/* take the core out of reset */
	ctrl &= ~(CORECTRL_SWRST0 << cpu);
	write_c0_corectrl(ctrl);

	spin_unlock_irqrestore(&smp_lock, flags);
}

static void jz4780_init_secondary(void)
{
}

static void jz4780_smp_finish(void)
{
	int cpu = smp_processor_id();
	u32 reim;

	spin_lock(&smp_lock);

	/* unmask mailbox interrupts for this core */
	reim = read_c0_reim();
	reim |= REIM_MBOXIRQ0M << smp_processor_id();
	write_c0_reim(reim);

	spin_unlock(&smp_lock);

	/* unmask interrupts for this core */
	change_c0_status(ST0_IM, STATUSF_IP3 | STATUSF_IP2 |
			 STATUSF_IP1 | STATUSF_IP0);
	irq_enable_hazard();

	/* force broadcast timer */
	clockevents_notify(CLOCK_EVT_NOTIFY_BROADCAST_FORCE, &cpu);
}

static void jz4780_send_ipi_single_locked(int cpu, unsigned int action)
{
	u32 mbox;

	switch (cpu) {
	case 0:
		mbox = read_c0_mailbox0();
		write_c0_mailbox0(mbox | action);
		break;
	case 1:
		mbox = read_c0_mailbox1();
		write_c0_mailbox1(mbox | action);
		break;
	default:
		panic("unhandled cpu %d!", cpu);
	}
}

static void jz4780_send_ipi_single(int cpu, unsigned int action)
{
	unsigned long flags;
	spin_lock_irqsave(&smp_lock, flags);
	jz4780_send_ipi_single_locked(cpu, action);
	spin_unlock_irqrestore(&smp_lock, flags);
}

static void jz4780_send_ipi_mask(const struct cpumask *mask,
				 unsigned int action)
{
	unsigned long flags;
	int cpu;

	spin_lock_irqsave(&smp_lock, flags);

	for_each_cpu(cpu, mask)
		jz4780_send_ipi_single_locked(cpu, action);

	spin_unlock_irqrestore(&smp_lock, flags);
}

static struct plat_smp_ops jz4780_smp_ops = {
	.send_ipi_single = jz4780_send_ipi_single,
	.send_ipi_mask = jz4780_send_ipi_mask,
	.init_secondary = jz4780_init_secondary,
	.smp_finish = jz4780_smp_finish,
	.boot_secondary = jz4780_boot_secondary,
	.smp_setup = jz4780_smp_setup,
	.prepare_cpus = jz4780_smp_prepare_cpus,
};

void jz4780_smp_init(void)
{
	register_smp_ops(&jz4780_smp_ops);
}
