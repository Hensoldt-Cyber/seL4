/*
 * Copyright 2014, General Dynamics C4 Systems
 * Copyright 2021, HENSOLDT Cyber
 *
 * SPDX-License-Identifier: GPL-2.0-only
 */

#include <config.h>
#include <assert.h>
#include <kernel/boot.h>
#include <machine/io.h>
#include <model/statedata.h>
#include <object/interrupt.h>
#include <arch/machine.h>
#include <arch/kernel/boot.h>
#include <arch/kernel/vspace.h>
#include <arch/benchmark.h>
#include <arch/user_access.h>
#include <arch/object/iospace.h>
#include <linker.h>
#include <plat/machine/hardware.h>
#include <machine.h>
#include <arch/machine/timer.h>
#include <arch/machine/fpu.h>
#include <arch/machine/tlb.h>

#ifdef CONFIG_ARM_SMMU
#include <drivers/smmu/smmuv2.h>
#endif

#ifdef ENABLE_SMP_SUPPORT
/* sync variable to prevent other nodes from booting
 * until kernel data structures initialized */
BOOT_BSS static volatile int node_boot_lock;
#endif /* ENABLE_SMP_SUPPORT */


BOOT_CODE static void init_core_interrupts(word_t core_id)
{
    /* Initialize the architecture specific interrupts. The primary core
     * initializes its PPIs and all platform interrupts, the secondary
     * cores initialize their PPIs only. The IRQ cap control init is done
     * in the generic kernel setup once this returns.
     */
    word_t max = SMP_TERNARY((0 == core_id) ? maxIRQ : NUM_PPI, maxIRQ);
    for (word_t i = 0; i < max; i++) {
        maskInterrupt(true, CORE_IRQ_TO_IRQT(core_id, i));
    }

    /* Enable per-CPU timer interrupts */
    setIRQState(IRQTimer, CORE_IRQ_TO_IRQT(core_id, KERNEL_TIMER_IRQ));

#ifdef ENABLE_SMP_SUPPORT
    setIRQState(IRQIPI, CORE_IRQ_TO_IRQT(core_id, irq_remote_call_ipi));
    setIRQState(IRQIPI, CORE_IRQ_TO_IRQT(core_id, irq_reschedule_ipi));
#endif /* ENABLE_SMP_SUPPORT */

#ifdef CONFIG_ARM_HYPERVISOR_SUPPORT
    setIRQState(IRQReserved, CORE_IRQ_TO_IRQT(core_id, INTERRUPT_VGIC_MAINTENANCE));
    setIRQState(IRQReserved, CORE_IRQ_TO_IRQT(core_id, INTERRUPT_VTIMER_EVENT));
#endif /* CONFIG_ARM_HYPERVISOR_SUPPORT */
}

BOOT_CODE void arch_init_irqs(cap_t root_cnode_cap)
{
    /* Initialize the architecture specific interrupts, The IRQ cap control init
     * is done in the generic kernel setup once this returns.
     */
    init_core_interrupts(0); /* we are on the the primary core here */

#ifdef CONFIG_TK1_SMMU
    setIRQState(IRQReserved, CORE_IRQ_TO_IRQT(0, INTERRUPT_SMMU));
#endif

#ifdef CONFIG_ARM_ENABLE_PMU_OVERFLOW_INTERRUPT

#if (defined CONFIG_PLAT_TX1 && defined ENABLE_SMP_SUPPORT)
//SELFOUR-1252
#error "This platform doesn't support tracking CPU utilisation on multicore"
#endif /* CONFIG_PLAT_TX1 && ENABLE_SMP_SUPPORT */

#ifndef KERNEL_PMU_IRQ
#error "This platform doesn't support tracking CPU utilisation feature"
#endif /* not KERNEL_PMU_IRQ */

    setIRQState(IRQReserved, CORE_IRQ_TO_IRQT(0, KERNEL_PMU_IRQ));

#endif /* CONFIG_ARM_ENABLE_PMU_OVERFLOW_INTERRUPT */
}

#ifdef CONFIG_ARM_SMMU
BOOT_CODE void arch_init_smmu(cap_t root_cnode_cap)
{
    plat_smmu_init();
    /* Provide the SID and CB control cap. This is still very ARM specific and
     * thus not part of the generic kernel setup.
     */
    write_slot(SLOT_PTR(pptr_of_cap(root_cnode_cap), seL4_CapSMMUSIDControl),
               cap_sid_control_cap_new());
    write_slot(SLOT_PTR(pptr_of_cap(root_cnode_cap), seL4_CapSMMUCBControl),
               cap_cb_control_cap_new());
}
#endif /* CONFIG_ARM_SMMU */

/** This and only this function initialises the CPU.
 *
 * It does NOT initialise any kernel state.
 * @return For the verification build, this currently returns true always.
 */
BOOT_CODE static bool_t init_cpu(void)
{
    bool_t haveHWFPU;

#ifdef CONFIG_ARCH_AARCH64
    if (config_set(CONFIG_ARM_HYPERVISOR_SUPPORT)) {
        if (!checkTCR_EL2()) {
            return false;
        }
    }
#endif

    activate_global_pd();
    if (config_set(CONFIG_ARM_HYPERVISOR_SUPPORT)) {
        vcpu_boot_init();
    }

#ifdef CONFIG_HARDWARE_DEBUG_API
    if (!Arch_initHardwareBreakpoints()) {
        printf("Kernel built with CONFIG_HARDWARE_DEBUG_API, but this board doesn't "
               "reliably support it.\n");
        return false;
    }
#endif

    /* Setup kernel stack pointer.
     * On ARM SMP, the array index here is the CPU ID
     */
    word_t stack_top = ((word_t) kernel_stack_alloc[CURRENT_CPU_INDEX()]) + BIT(CONFIG_KERNEL_STACK_BITS);
#if defined(ENABLE_SMP_SUPPORT) && defined(CONFIG_ARCH_AARCH64)
    /* the least 12 bits are used to store logical core ID */
    stack_top |= CURRENT_CPU_INDEX();
#endif
    setKernelStack(stack_top);

#ifdef CONFIG_ARCH_AARCH64
    /* initialise CPU's exception vector table */
    setVtable((pptr_t)arm_vector_table);
#endif /* CONFIG_ARCH_AARCH64 */

    haveHWFPU = fpsimd_HWCapTest();

    /* Disable FPU to avoid channels where a platform has an FPU but doesn't make use of it */
    if (haveHWFPU) {
        disableFpu();
    }

#ifdef CONFIG_HAVE_FPU
    if (haveHWFPU) {
        if (!fpsimd_init()) {
            return false;
        }
    } else {
        printf("Platform claims to have FP hardware, but does not!");
        return false;
    }
#endif /* CONFIG_HAVE_FPU */

    cpu_initLocalIRQController();

#ifdef CONFIG_ENABLE_BENCHMARKS
    arm_init_ccnt();
#endif /* CONFIG_ENABLE_BENCHMARKS */

    /* Export selected CPU features for access by PL0 */
    armv_init_user_access();

    initTimer();

    return true;
}

#ifdef ENABLE_SMP_SUPPORT
BOOT_CODE void arch_release_secondary_cores(void)
{
    /* All secondary harts are released at the same time. The generic kernel
     * boot process will use the BKL eventually to serialize things where this
     * is necessary.
     */
    assert(0 == node_boot_lock);
    node_boot_lock = 1;

#ifndef CONFIG_ARCH_AARCH64
    /* At this point in time the other CPUs do *not* have the seL4 global pd set.
     * However, they still have a PD from the elfloader (which is mapping memory
     * as strongly ordered uncached, as a result we need to explicitly clean
     * the cache for it to see the update of node_boot_lock
     *
     * For ARMv8, the elfloader sets the page table entries as inner shareable
     * (so is the attribute of the seL4 global PD) when SMP is enabled, and
     * turns on the cache. Thus, we do not need to clean and invalidate the cache.
     */
    cleanInvalidateL1Caches();
    plat_cleanInvalidateL2Cache();
#endif
}
#endif /* ENABLE_SMP_SUPPORT */

extern void arm_errata(void);

BOOT_CODE VISIBLE void init_kernel(
    paddr_t ui_phys_start,
    paddr_t ui_phys_end,
    sword_t ui_pv_offset,
    vptr_t  ui_virt_entry,
    paddr_t dtb_phys_addr,
    uint32_t dtb_size
)
{
    arm_errata();
    /* Assume there is a core with ID 0 and use it for bootstrapping. */
    if (likely(0 == CURRENT_CPU_INDEX())) {
        map_kernel_window();
        if (!init_cpu()) {
            fail("ERROR: CPU init failed\n");
            UNREACHABLE();
        }
        /* Platform initialization */
        initIRQController();
        initL2Cache();
#ifdef CONFIG_ARM_SMMU
        plat_smmu_init();
#endif /* CONFIG_ARM_SMMU */
        /* Debug output via serial port is only available from here on. Call the
         * generic kernel setup. It will release the secondary cores and boot
         * them. They may have left to userspace already when we return here.
         * This is fine, because the only thread at this stage is the initial
         * thread on the primary core. All other cores can just run the idle
         * thread.
         */
        if (!setup_kernel(ui_phys_start, ui_phys_end, ui_pv_offset,
                          ui_virt_entry, dtb_phys_addr, dtb_size)) {
            fail("ERROR: kernel init failed on primary core");
            UNREACHABLE();
        }
        /* Nothing architecture specific to be done here. */
    } else {
#ifdef ENABLE_SMP_SUPPORT
        /* Spin until primary hart boot releases the secondary harts. */
        while (!node_boot_lock) {
            /* busy waiting loop */
        }
        if (!init_cpu()) {
            fail("ERROR: kernel init failed on primary core");
            UNREACHABLE();
        }
        init_cpu();
        init_core_interrupts(CURRENT_CPU_INDEX());
        /* Call the generic kernel setup. It assumes the primary code boot has
         * been done and the BKL has been initialized, but this core is not
         * holding it. Eventually, the setup acquires the BKL and returns while
         * still holding it. There is no need to release the BKL explicitly,
         * exiting to user space will do this automatically.
         */
        setup_kernel_on_secondary_core();
        /* Nothing architecture specific to be done here. */
#else /* not ENABLE_SMP_SUPPORT */
        fail("ERROR: SMP no enabled\n");
        UNREACHABLE();
#endif /* [not] ENABLE_SMP_SUPPORT */
    }

#ifdef CONFIG_KERNEL_MCS
    NODE_STATE(ksCurTime) = getCurrentTime();
    NODE_STATE(ksConsumed) = 0;
#endif

    schedule();
    activateThread();
}
