/*
 * Copyright 2020, Data61, CSIRO (ABN 41 687 119 230)
 * Copyright 2015, 2016 Hesham Almatary <heshamelmatary@gmail.com>
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
#include <linker.h>
#include <plat/machine/hardware.h>
#include <machine.h>

#ifdef ENABLE_SMP_SUPPORT
BOOT_BSS static volatile word_t node_boot_lock;
#endif

BOOT_CODE cap_t create_mapped_it_frame_cap(cap_t pd_cap, pptr_t pptr, vptr_t vptr, asid_t asid, bool_t
                                           use_large, bool_t executable)
{
    cap_t cap;
    vm_page_size_t frame_size;

    if (use_large) {
        frame_size = RISCV_Mega_Page;
    } else {
        frame_size = RISCV_4K_Page;
    }

    cap = cap_frame_cap_new(
              asid,                            /* capFMappedASID       */
              pptr,                            /* capFBasePtr          */
              frame_size,                      /* capFSize             */
              wordFromVMRights(VMReadWrite),   /* capFVMRights         */
              0,                               /* capFIsDevice         */
              vptr                             /* capFMappedAddress    */
          );

    map_it_frame_cap(pd_cap, cap);
    return cap;
}

BOOT_CODE void arch_init_irqs(cap_t root_cnode_cap)
{
    /* Initialize the architecture specific interrupts, The IRQ cap control init
     * is done in the generic kernel setup once this returns.
     */
    assert(0 == irqInvalid); /* IRQ 0 is invalid on RISC-V */
    for (irq_t i = 1; i <= maxIRQ; i++) {
        setIRQState(IRQInactive, i);
    }

    setIRQState(IRQTimer, KERNEL_TIMER_IRQ);
#ifdef ENABLE_SMP_SUPPORT
    setIRQState(IRQIPI, irq_remote_call_ipi);
    setIRQState(IRQIPI, irq_reschedule_ipi);
#endif /* ENABLE_SMP_SUPPORT*/
}

/* ASM symbol for the CPU initialisation trap. */
extern char trap_entry[1];

/* This and only this function initialises the CPU. It does NOT initialise any
 * kernel state.
 */
BOOT_CODE static void init_cpu(void)
{
    activate_kernel_vspace();
    write_stvec((word_t)trap_entry); /* Write trap entry address to stvec */

    initLocalIRQController();

#ifndef CONFIG_KERNEL_MCS
    initTimer();
#endif /* CONFIG_KERNEL_MCS */

    set_fs_off(); /* disable FPU access by default */
#ifdef CONFIG_HAVE_FPU
    set_fs_clean();
    write_fcsr(0);
    disableFpu();
#endif /* CONFIG_HAVE_FPU */
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
    fence_w_r();
}
#endif /* ENABLE_SMP_SUPPORT */

BOOT_CODE VISIBLE void init_kernel(
    paddr_t ui_p_reg_start,
    paddr_t ui_p_reg_end,
    sword_t pv_offset,
    vptr_t  v_entry,
    paddr_t dtb_addr_p,
    uint32_t dtb_size
#ifdef ENABLE_SMP_SUPPORT
    ,
    word_t hart_id,
    word_t core_id
#endif /* ENABLE_SMP_SUPPORT */
)
{
    SMP_COND_STATEMENT(add_hart_to_core_map(hart_id, core_id));
    if (likely((SMP_TERNARY(0 == core_id, true)))) {
        map_kernel_window();
        init_cpu();
        /* Platform initialization */
        initIRQController();
        /* The generic kernel setup will initialize the memory mapping and
         * eventually release the secondary harts. They may have left to
         * userspace already when we return here. This is fine, because the only
         * thread at this stage is the initial thread on the primary core. All
         * other cores can just run the idle thread.
         */
        if (!setup_kernel(ui_p_reg_start, ui_p_reg_end, pv_offset, v_entry,
                          dtb_addr_p, dtb_size)) {
            fail("ERROR: kernel initialization failed\n");
            UNREACHABLE();
        }
        /* Nothing architecture specific left to be done on the primary hart. */
    } else {
#ifdef ENABLE_SMP_SUPPORT
        /* Spin until primary hart boot releases the secondary harts. */
        while (!node_boot_lock) {
            /* busy waiting loop */
        }
        fence_r_rw();
        init_cpu();
        /* Call the generic kernel setup. It assumes the primary code boot has
         * been done and the BKL has been initialized, but this core is not
         * holding it. Eventually, the setup acquires the BKL and returns while
         * still holding it. There is no need to release the BKL explicitly,
         * exiting to user space will do this automatically.
         */
        setup_kernel_on_secondary_core();
        ifence_local(); /* ToDo: clarify why this is needed */
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
