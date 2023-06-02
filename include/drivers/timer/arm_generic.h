/*
 * Copyright 2020, Data61, CSIRO (ABN 41 687 119 230)
 *
 * SPDX-License-Identifier: GPL-2.0-only
 */

#pragma once

#include <config.h>
#include <mode/machine.h>

/* ARM generic timer implementation */
#ifdef CONFIG_KERNEL_MCS
#include <model/statedata.h>
#include <api/types.h>

#define CNT_CTL_ENABLE      BIT(0)
#define CNT_CTL_IMASK       BIT(1)
#define CNT_CTL_ISTATUS     BIT(2)
/* CNT_CTL bits 3 to 31 are RES0 */

/** DONT_TRANSLATE **/
static inline ticks_t getCurrentTime(void)
{
    ticks_t time;
    SYSTEM_READ_64(CNT_CT, time);
    return time;
}

/** DONT_TRANSLATE **/
static inline void setDeadline(ticks_t deadline)
{
    assert(deadline >= NODE_STATE(ksCurTime));
    /* The timer interrupt is supposed to be masked. Set the new value and
     * unmask it.
     */
    SYSTEM_WRITE_64(CNT_CVAL, deadline);
    SYSTEM_WRITE_64(CNT_CTL, CNT_CTL_ENABLE);
}

static inline void ackDeadlineIRQ(void)
{
    /* mask interrupt */
    SYSTEM_WRITE_64(CNT_CTL, CNT_CTL_ENABLE | CNT_CTL_IMASK);
    /* Ensure that the timer deasserts the IRQ before GIC EOIR/DIR.
     * This is sufficient to remove the pending state from the GICR
     * and avoid the interrupt happening twice because of the level
     * sensitive configuration. */
    isb();
}
#else /* CONFIG_KERNEL_MCS */
#include <arch/machine/timer.h>
static inline void resetTimer(void)
{
    SYSTEM_WRITE_WORD(CNT_TVAL, TIMER_RELOAD);
    /* Ensure that the timer deasserts the IRQ before GIC EOIR/DIR.
     * This is sufficient to remove the pending state from the GICR
     * and avoid the interrupt happening twice because of the level
     * sensitive configuration. */
    isb();
}
#endif /* !CONFIG_KERNEL_MCS */

BOOT_CODE void initGenericTimer(void);

#ifdef CONFIG_ARM_HYPERVISOR_SUPPORT
static uint64_t read_cntpct(void) UNUSED;
static void save_virt_timer(vcpu_t *vcpu);
static void restore_virt_timer(vcpu_t *vcpu);
#endif /* CONFIG_ARM_HYPERVISOR_SUPPORT */

