/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __PREEMPTIRQ_LONG_H
#define __PREEMPTIRQ_LONG_H

struct task_struct;

void note_irq_disable(void *u1, unsigned long u2, unsigned long u3);
void test_irq_disable_long(void *u1, unsigned long u2, unsigned long u3);
void test_preempt_disable_long(void *u1, unsigned long u2, unsigned long u3);
void note_preempt_disable(void *u1, unsigned long u2, unsigned long u3);
void note_context_switch(void *u1, bool u2, struct task_struct *u3,
			 struct task_struct *next, unsigned int prev_state);
int preemptirq_long_init(void);

#endif /* __PREEMPTIRQ_LONG_H */
