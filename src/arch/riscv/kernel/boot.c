/*
 * Copyright 2020, Data61, CSIRO (ABN 41 687 119 230)
 * Copyright 2015, 2016 Hesham Almatary <heshamelmatary@gmail.com>
 * Copyright 2021, HENSOLDT Cyber
 *
 * SPDX-License-Identifier: GPL-2.0-only
 */

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

BOOT_BSS static region_t res_reg[NUM_RESERVED_REGIONS];

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

BOOT_CODE bool_t arch_init_freemem(p_region_t ui_p_reg,
                                   p_region_t dtb_p_reg,
                                   v_region_t it_v_reg,
                                   word_t extra_bi_size_bits)
{
    /* Reserve the kernel image region. This may look a bit awkward, as the
     * symbols are a reference in the kernel image window, but all allocations
     * are done in terms of the main kernel window, so we do some translation.
     */
    res_reg[0].start = (pptr_t)paddr_to_pptr(kpptr_to_paddr((void *)KERNEL_ELF_BASE));
    res_reg[0].end = (pptr_t)paddr_to_pptr(kpptr_to_paddr((void *)ki_end));

    int index = 1;

    /* add the dtb region, if it is not empty */
    if (dtb_p_reg.start) {
        if (index >= ARRAY_SIZE(res_reg)) {
            printf("ERROR: no slot to add DTB to reserved regions\n");
            return false;
        }
        res_reg[index] = paddr_to_pptr_reg(dtb_p_reg);
        index += 1;
    }

    /* reserve the user image region */
    if (index >= ARRAY_SIZE(res_reg)) {
        printf("ERROR: no slot to add user image to reserved regions\n");
        return false;
    }
    res_reg[index] = paddr_to_pptr_reg(ui_p_reg);
    index += 1;

    /* avail_p_regs comes from the auto-generated code */
    return init_freemem(ARRAY_SIZE(avail_p_regs), avail_p_regs,
                        index, res_reg,
                        it_v_reg, extra_bi_size_bits);
}

BOOT_CODE static void init_irqs(cap_t root_cnode_cap)
{
    irq_t i;

    for (i = 0; i <= maxIRQ; i++) {
        if (i != irqInvalid) {
            /* IRQ 0 is irqInvalid */
            setIRQState(IRQInactive, i);
        }
    }
    setIRQState(IRQTimer, KERNEL_TIMER_IRQ);
#ifdef ENABLE_SMP_SUPPORT
    setIRQState(IRQIPI, irq_remote_call_ipi);
    setIRQState(IRQIPI, irq_reschedule_ipi);
#endif
    /* provide the IRQ control cap */
    write_slot(SLOT_PTR(pptr_of_cap(root_cnode_cap), seL4_CapIRQControl), cap_irq_control_cap_new());
}

/* ASM symbol for the CPU initialisation trap. */
extern char trap_entry[1];

/* This and only this function initialises the CPU. It does NOT initialise any kernel state. */

#ifdef CONFIG_HAVE_FPU
BOOT_CODE static void init_fpu(void)
{
    set_fs_clean();
    write_fcsr(0);
    disableFpu();
}
#endif

BOOT_CODE static void init_cpu(void)
{

    activate_kernel_vspace();
    /* Write trap entry address to stvec */
    write_stvec((word_t)trap_entry);
    initLocalIRQController();
#ifndef CONFIG_KERNEL_MCS
    initTimer();
#endif

    /* disable FPU access */
    set_fs_off();

#ifdef CONFIG_HAVE_FPU
    init_fpu();
#endif
}

/* This and only this function initialises the platform. It does NOT initialise any kernel state. */

BOOT_CODE static void init_plat(void)
{
    initIRQController();
}


#ifdef ENABLE_SMP_SUPPORT
BOOT_CODE static bool_t try_init_kernel_secondary_core(word_t hart_id, word_t core_id)
{
    while (!node_boot_lock);

    fence_r_rw();

    init_cpu();

    /* Call the generic kernel setup. It assumes the BKL has been initialized
     * but this core is not holding it. Eventually, is acquires the BKL and
     * returns while still holding it. There is no need to release the BKL
     * explicitly, exiting to user space will do this automatically.
     */
    setup_kernel_on_secondary_core();

    ifence_local(); /* ToDo: clarify why this is needed */

    return true;
}

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

#endif

/* Main kernel initialisation function. */
static BOOT_CODE bool_t try_init_kernel(
    paddr_t ui_p_reg_start,
    paddr_t ui_p_reg_end,
    word_t  pv_offset,
    vptr_t  v_entry,
    paddr_t dtb_phys_addr,
    word_t  dtb_size
)
{
    cap_t root_cnode_cap;
    cap_t it_pd_cap;
    cap_t it_ap_cap;
    cap_t ipcbuf_cap;
    p_region_t boot_mem_reuse_p_reg = ((p_region_t) {
        kpptr_to_paddr((void *)KERNEL_ELF_BASE), kpptr_to_paddr(ki_boot_end)
    });
    region_t boot_mem_reuse_reg = paddr_to_pptr_reg(boot_mem_reuse_p_reg);
    region_t ui_reg = paddr_to_pptr_reg((p_region_t) {
        ui_p_reg_start, ui_p_reg_end
    });
    word_t extra_bi_size = 0;
    pptr_t extra_bi_offset = 0;
    vptr_t extra_bi_frame_vptr;
    vptr_t bi_frame_vptr;
    vptr_t ipcbuf_vptr;
    create_frames_of_region_ret_t create_frames_ret;
    create_frames_of_region_ret_t extra_bi_ret;

    /* Convert from physical addresses to userland vptrs with the parameter
     * pv_offset, which is defined as:
     *     virt_address + pv_offset = phys_address
     * The offset is usually a positive value, because the virtual address of
     * the user image is a low value and the actually physical address is much
     * greater. However, there is no restrictions on the physical and virtual
     * image location in general. Defining pv_offset as a signed value might
     * seem the intuitive choice how to handle this, but there is are two
     * catches here that break the C rules. We can't cover the full integer
     * range then, and overflows/underflows are well defined for unsigned values
     * only. They are undefined for signed values, even if such operations
     * practically work in many cases due to how compiler and machine implement
     * negative integers using the two's-complement.
     * Assume a 32-bit system with virt_address=0xc0000000 and phys_address=0,
     * then pv_offset would have to be -0xc0000000. This value is not in the
     * 32-bit signed integer range. Calculating '0 - 0xc0000000' using unsigned
     * integers, the result is 0x40000000 after an underflow, the reverse
     * calculation '0xc0000000 + 0x40000000' results in 0 again after overflow.
     * If 0x40000000 is a signed integer, the result is likely the same, but the
     * whole operation is undefined by C rules.
     */
    v_region_t ui_v_reg = {
        .start = ui_p_reg_start - pv_offset,
        .end   = ui_p_reg_end   - pv_offset
    };

    ipcbuf_vptr = ui_v_reg.end;
    bi_frame_vptr = ipcbuf_vptr + BIT(PAGE_BITS);
    extra_bi_frame_vptr = bi_frame_vptr + BIT(PAGE_BITS);

    map_kernel_window();

    /* initialise the CPU */
    init_cpu();

    /* initialize the platform */
    init_plat();

    /* Call the generic kernel setup. It will release the secondary cores and
     * boot them. They may have left to userspace already when we return here.
     * This is fine, because the only thread at this stage is the initial thread
     * on the primary core. All other cores can just run the idle thread.
     */
    if (!setup_kernel(ui_p_reg_start, ui_p_reg_end, pv_offset, v_entry,
                      dtb_phys_addr, dtb_size)) {
        printf("ERROR: kernel initialization failed\n");
        return false;
    }

    /* Nothing architecture specific left to be done here. */

    return true;
}

/* This is called from assembly code and thus there are no specific types in
 * the signature.
 */
BOOT_CODE VISIBLE void init_kernel(
    word_t ui_p_reg_start,
    word_t ui_p_reg_end,
    word_t pv_offset,
    word_t v_entry,
    word_t dtb_addr_p,
    word_t dtb_size
#ifdef ENABLE_SMP_SUPPORT
    ,
    word_t hart_id,
    word_t core_id
#endif
)
{
    bool_t result;

#ifdef ENABLE_SMP_SUPPORT
    add_hart_to_core_map(hart_id, core_id);
    if (core_id == 0) {
        result = try_init_kernel((paddr_t)ui_p_reg_start,
                                 (paddr_t)ui_p_reg_end,
                                 pv_offset,
                                 (vptr_t)v_entry,
                                 (paddr_t)dtb_addr_p,
                                 dtb_size);
    } else {
        result = try_init_kernel_secondary_core(hart_id, core_id);
    }
#else
    result = try_init_kernel((paddr_t)ui_p_reg_start,
                             (paddr_t)ui_p_reg_end,
                             pv_offset,
                             (vptr_t)v_entry,
                             (paddr_t)dtb_addr_p,
                             dtb_size);
#endif
    if (!result) {
        fail("ERROR: kernel init failed");
        UNREACHABLE();
    }

#ifdef CONFIG_KERNEL_MCS
    NODE_STATE(ksCurTime) = getCurrentTime();
    NODE_STATE(ksConsumed) = 0;
#endif

    schedule();
    activateThread();
}
