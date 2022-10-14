/*
 * Copyright 2020, Data61, CSIRO (ABN 41 687 119 230)
 * Copyright 2015, 2016 Hesham Almatary <heshamelmatary@gmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-only
 */

#include <config.h>
#include <machine/assembler.h>
#include <arch/machine/hardware.h>
#include <arch/api/syscall.h>
#include <arch/machine/registerset.h>
#include <util.h>

#define REGBYTES (CONFIG_WORD_SIZE / 8)

.section .text

.global trap_entry
.extern c_handle_syscall
.extern c_handle_fastpath_reply_recv
.extern c_handle_fastpath_call
.extern c_handle_interrupt
.extern c_handle_exception

trap_entry:

#ifdef ENABLE_SMP_SUPPORT
  /* We need two things now:
   *   - the stack pointer for the kernel, which is in SSCRATCH
   *   - a pointer to a space where to store the thread context
   */
  csrrw sp, sscratch, sp        /* swap sp and sscratch to get a kernel stack */
  STORE tp, (-2*REGBYTES)(sp)   /* sp[-2] = tp/x4 */
  LOAD  tp, (-1*REGBYTES)(sp)   /* tp = sp[-1] */
  STORE ra, (0*REGBYTES)(tp)    /* t0[0] = ra/x1 */
  csrrw ra, sscratch, sp        /* sscratch -> ra, sp -> scratch */
  STORE ra, (1*REGBYTES)(tp)    /* tp[1] = user sp */
  LOAD  ra, (-2*REGBYTES)(sp)   /* get user tp from sscratch */
#else
  csrrw tp, sscratch, tp        /* swap tp/x4 and sscratch */
  STORE ra, (0*REGBYTES)(tp)    /* tp[0] = ra/x1 */
  STORE sp, (1*REGBYTES)(tp)    /* tp[1] = sp/x2 */
  la sp, (kernel_stack_alloc + BIT(CONFIG_KERNEL_STACK_BITS))
  csrr  ra, sscratch            /* ra/x1 = sscratch (t0/x5 of user thread) */
#endif
  /* x1/ra and x2/sp have already been saved */
  STORE gp, (2*REGBYTES)(tp)    /* x3 */
  STORE ra, (3*REGBYTES)(tp)    /* user tp/x4 is in ra/x1 */
  STORE t0, (4*REGBYTES)(tp)    /* x5 */
  STORE t1, (5*REGBYTES)(tp)    /* x6 */
  STORE t2, (6*REGBYTES)(tp)    /* x7 */
  STORE s0, (7*REGBYTES)(tp)    /* x8 */
  STORE s1, (8*REGBYTES)(tp)    /* x9 */
  STORE a0, (9*REGBYTES)(tp)    /* x10 */
  STORE a1, (10*REGBYTES)(tp)   /* x11*/
  STORE a2, (11*REGBYTES)(tp)   /* x12 */
  STORE a3, (12*REGBYTES)(tp)   /* x13 */
  STORE a4, (13*REGBYTES)(tp)   /* x14 */
  STORE a5, (14*REGBYTES)(tp)   /* x15 */
  STORE a6, (15*REGBYTES)(tp)   /* x16 */
  STORE a7, (16*REGBYTES)(tp)   /* x17 */
  STORE s2, (17*REGBYTES)(tp)   /* x18 */
  STORE s3, (18*REGBYTES)(tp)   /* x19 */
  STORE s4, (19*REGBYTES)(tp)   /* x20 */
  STORE s5, (20*REGBYTES)(tp)   /* x21 */
  STORE s6, (21*REGBYTES)(tp)   /* x22 */
  STORE s7, (22*REGBYTES)(tp)   /* x23 */
  STORE s8, (23*REGBYTES)(tp)   /* x24 */
  STORE s9, (24*REGBYTES)(tp)   /* x25 */
  STORE s10, (25*REGBYTES)(tp)  /* x26 */
  STORE s11, (26*REGBYTES)(tp)  /* x27 */
  STORE t3, (27*REGBYTES)(tp)   /* x28 */
  STORE t4, (28*REGBYTES)(tp)   /* x29 */
  STORE t5, (29*REGBYTES)(tp)   /* x30 */
  STORE t6, (30*REGBYTES)(tp)   /* x31 */
  csrr  t0, scause
  STORE t0, (31*REGBYTES)(tp)
  csrr  t1, sstatus
  STORE t1, (32*REGBYTES)(tp)
  csrr  t2, sepc
  STORE t2, (33*REGBYTES)(t0)

.option push
.option norelax
  la gp, __global_pointer$
.option pop

  /* Check if it's an interrupt */
  bltz t0, interrupt

  /* ratified priv has value 8 for ecall from U-mode exception */
  li   s4, 8
  bne  t0, s4, exception

handle_syscall:
  /* Set the return address to sepc + 4 in the case of a system/environment call */
  addi s2, s2, 4
  /* Save NextIP */
  STORE   s2, (34*REGBYTES)(t0)

#ifdef CONFIG_FASTPATH
  li t3, SYSCALL_CALL
  bne a7, t3, .Ltest_replyrecv
  j c_handle_fastpath_call

.Ltest_replyrecv:
  li t3, SYSCALL_REPLY_RECV
#ifdef CONFIG_KERNEL_MCS
  /* move reply to 3rd argument */
  mv a2, a6
#endif
  bne a7, t3, .Lslowpath
  j c_handle_fastpath_reply_recv

.Lslowpath:
#endif

  /* move syscall number to 3rd argument */
  mv a2, a7

  j c_handle_syscall

/* Not an interrupt or a syscall */
exception:
  /* Save NextIP */
  STORE  s2, (34*REGBYTES)(t0)
  j c_handle_exception

interrupt:
  /* Save NextIP */
  STORE   s2, (34*REGBYTES)(t0)
  j c_handle_interrupt
