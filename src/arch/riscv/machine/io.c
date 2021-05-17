/*
 * Copyright 2020, Data61, CSIRO (ABN 41 687 119 230)
 * Copyright 2015, 2016 Hesham Almatary <heshamelmatary@gmail.com>
 * Copyright 2021, HENSOLDT Cyber
 *
 * SPDX-License-Identifier: GPL-2.0-only
 */

#include <config.h>
#include <machine/io.h>
#include <arch/sbi.h>

#ifdef CONFIG_PRINTING
void kernel_putDebugChar(unsigned char c)
{

#if defined(CONFIG_RISCV_SBI_NONE)
#error Implment a UART driver for RISC-V if no SBI console exists
#endif

    /* Don't use any UART driver, but write to the SBI console. */

#if defined(CONFIG_RISCV_SBI_BBL)
    /* BBL does not implement ab abstract console, but passes data to the UART
     * transparently. Thus we must take care of printing a '\r' (CR) before any
     * '\n' (LF) to comply with the common serial terminal usage.
     */
    if (c == '\n') {
        sbi_console_putchar('\r');
    }
#endif

    sbi_console_putchar(c);
}
#endif /* CONFIG_PRINTING */

#ifdef CONFIG_DEBUG_BUILD
unsigned char kernel_getDebugChar(void)
{

#if defined(CONFIG_RISCV_SBI_NONE)
#error Implment a UART driver for RISC-V if no SBI console exists
#endif

    /* Don't use UART, but read from the SBI console. */
    return sbi_console_getchar();
}
#endif /* CONFIG_DEBUG_BUILD */
