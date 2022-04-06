/*
 * Copyright 2020, Data61, CSIRO (ABN 41 687 119 230)
 * Copyright 2021, HENSOLDT Cyber
 *
 * SPDX-License-Identifier: GPL-2.0-only
 */

#include <config.h>
#include <machine/assembler.h>
#include <arch/machine/hardware.h>
#include <arch/machine/registerset.h>
#include <util.h>

#ifndef ALLOW_UNALIGNED_ACCESS
#define ALLOW_UNALIGNED_ACCESS 1
#endif

#if ALLOW_UNALIGNED_ACCESS
#define CR_ALIGN_SET     0
#define CR_ALIGN_CLEAR   BIT(CONTROL_A)
#else
#define CR_ALIGN_SET     BIT(CONTROL_A)
#define CR_ALIGN_CLEAR   0
#endif

#ifndef CONFIG_DEBUG_DISABLE_L1_ICACHE
    #define CR_L1_ICACHE_SET   BIT(CONTROL_I)
    #define CR_L1_ICACHE_CLEAR 0
#else
    #define CR_L1_ICACHE_SET   0
    #define CR_L1_ICACHE_CLEAR BIT(CONTROL_I)
#endif

#ifndef CONFIG_DEBUG_DISABLE_L1_DCACHE
    #define CR_L1_DCACHE_SET   BIT(CONTROL_C)
    #define CR_L1_DCACHE_CLEAR 0
#else
    #define CR_L1_DCACHE_SET   0
    #define CR_L1_DCACHE_CLEAR BIT(CONTROL_C)
#endif

#define CR_BITS_SET    (CR_ALIGN_SET | \
                        CR_L1_ICACHE_SET | \
                        CR_L1_DCACHE_SET | \
                        BIT(CONTROL_M))

#define CR_BITS_CLEAR  (CR_ALIGN_CLEAR | \
                        CR_L1_ICACHE_CLEAR | \
                        CR_L1_DCACHE_CLEAR | \
                        BIT(CONTROL_SA0) | \
                        BIT(CONTROL_EE) | \
                        BIT(CONTROL_E0E))

/*
 * Entry point of the kernel ELF image.
 * X0-X5 contain parameters that are passed to init_kernel().
 *
 * Note that for SMP kernel, the tpidr_el1 is used to pass
 * the logical core ID.
 */

#ifdef CONFIG_ARM_HYPERVISOR_SUPPORT
#define SCTLR   sctlr_el2
#else
#define SCTLR   sctlr_el1
#endif

.section .boot.text
BEGIN_FUNC(_start)

    /* Make sure interrupts are disabled */
    msr daifset, #DAIFSET_MASK

    /* Initialise sctlr_el1 or sctlr_el2 register */
    msr     spsel, #1
    mrs     x9, SCTLR
    ldr     x10, =CR_BITS_SET
    ldr     x11, =CR_BITS_CLEAR
    orr     x9, x9, x10
    bic     x9, x9, x11
    msr     SCTLR, x9

    ldr     x9, =kernel_stack_alloc

#ifdef ENABLE_SMP_SUPPORT

    /* The kernel loader is supposed to initialize tpidr_el1 with the logical
     * core ID, counting starts at 0. Set the sp for each core.
     *   sp = {stack base} + {stack_size}
     *      = kernel_stack_alloc + (core_id * {stack_size}) + {stack_size}
     *      = kernel_stack_alloc + ((core_id + 1) * {stack_size})
     *      = kernel_stack_alloc + ((core_id + 1) * (1 << CONFIG_KERNEL_STACK_BITS)
     *      = kernel_stack_alloc + ((core_id + 1) << CONFIG_KERNEL_STACK_BITS)
     */
    mrs     x10, tpidr_el1 /* x10 = logical core ID (starting at 0) */
    addi    x11, x10, 1    /* x11 = x10 + 1 */

#else /* not ENABLE_SMP_SUPPORT */

    mov     x11, 1

#endif /* [not] ENABLE_SMP_SUPPORT */

    ldr     x12, =CONFIG_KERNEL_STACK_BITS
    lsl     x11, x12, x11
    add     x9, x9, x11
    mov     sp, x9
    /* the kernel stack must be 4-KiB aligned since we use the
       lowest 12 bits to store the logical core ID. */
    orr     x10, x10, x9
#ifdef CONFIG_ARM_HYPERVISOR_SUPPORT
    msr     tpidr_el2, x10
#else
    msr     tpidr_el1, x10
#endif

    /* Call bootstrapping implemented in C with parameters:
     *  x0: user image physical start address
     *  x1: user image physical end address
     *  x2: physical/virtual offset
     *  x3: user image virtual entry address
     *  x4: DTB physical address (0 if there is none)
     *  x5: DTB size (0 if there is none)
     */
    bl      init_kernel

    /* Restore the initial thread. Note that the function restore_user_context()
     * could technically also be called at the end of init_kernel() directly,
     * there is no need to return to the assembly code here at all. However, for
     * verification things are a lot easier when init_kernel() is a normal C
     * function that returns. The function restore_user_context() is not a
     * normal C function and thus handled specially in verification, it does
     * highly architecture specific things to exit to user mode.
     */
    b restore_user_context

END_FUNC(_start)
