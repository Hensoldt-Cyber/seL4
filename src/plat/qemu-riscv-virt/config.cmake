#
# Copyright 2022, HENSOLDT Cyber
# Copyright 2020, Data61, CSIRO (ABN 41 687 119 230)
#
# SPDX-License-Identifier: GPL-2.0-only
#

cmake_minimum_required(VERSION 3.7.2)

declare_platform(qemu-riscv-virt KernelPlatformQEMURiscVVirt PLAT_QEMU_RISCV_VIRT KernelArchRiscV)

if(KernelPlatformQEMURiscVVirt)

    # ToDo: use setup_seL4_arch("riscv64;riscv32")
    if("${KernelSel4Arch}" STREQUAL riscv64)
        declare_seL4_arch(riscv64)
    elseif("${KernelSel4Arch}" STREQUAL riscv32)
        declare_seL4_arch(riscv32) # This is still untested
    else()
        fallback_declare_seL4_arch_default(riscv64)
    endif()

    config_set(KernelOpenSBIPlatform OPENSBI_PLATFORM "generic")
    config_set(KernelPlatformFirstHartID FIRST_HART_ID 0)

    # Unless QEMU_DTS or QEMU_DTB is set explicitly, the device tree is extract
    # from QEMU. This keeps it nicely up to date with the newer QEMU versions
    # and is quite convenient for the development usecase.
    if(NOT DEFINED QEMU_DTS)

        set(QEMU_DTS "${CMAKE_BINARY_DIR}/qemu-riscv-virt.dts")

        if(NOT DEFINED QEMU_DTB)

            message(STATUS "Extracting device tree from QEMU")
            set(QEMU_DTB "${CMAKE_BINARY_DIR}/qemu-riscv-virt.dtb")

            # Use the system's QEMU if no custom QEMU is provided. Have a sanity
            # check about the version to ensure it can be used.
            if(NOT QEMU_BINARY)
                set(QEMU_RISCV_VIRT_BINARY "qemu-system-${KernelSel4Arch}")
                find_program(QEMU_RISCV_VIRT ${QEMU_RISCV_VIRT_BINARY})
                # RISC-V virtual platform works since QEMU v5.1.0
                set(QEMU_RISCV_VIRT_MIN_VERSION "5.1.0")
                execute_process(
                    COMMAND ${QEMU_RISCV_VIRT} -version
                    RESULT_VARIABLE error
                    OUTPUT_STRIP_TRAILING_WHITESPACE
                    OUTPUT_VARIABLE QEMU_STDOUT_MESSAGE
                )
                if(error)
                    message(FATAL_ERROR "Failed to determine QEMU version (${QEMU_RISCV_VIRT})")
                endif()
                string(
                    REGEX
                        MATCH
                        "[0-9](\\.[0-9])+"
                        QEMU_VERSION
                        "${QEMU_STDOUT_MESSAGE}"
                )
                if("${QEMU_VERSION}" VERSION_LESS "${QEMU_RISCV_VIRT_MIN_VERSION}")
                    message(
                        FATAL_ERROR
                            "Error: need at least QEMU version ${QEMU_RISCV_VIRT_MIN_VERSION}, found '${QEMU_VERSION}'"
                    )
                endif()
            endif()

            if(NOT DEFINED QEMU_MACHINE)
                set(QEMU_MACHINE "virt")
                #list(APPEND QEMU_MACHINE "aclint=off")   # on/off
                #list(APPEND QEMU_MACHINE "aia=none")     # none/aplic/aplic-imsic
                #list(APPEND QEMU_MACHINE "aia-guests=0") # VS-level AIA IMSIC pages per hart
                list(APPEND QEMU_MACHINE "dumpdtb=${QEMU_DTB}")

                # Lists are just strings with ";" as item separator, so we can
                # just replace them by ",". The cleaner way would be using the
                # CMake 3.12 feature: list(JOIN QEMU_MACHINE "," QEMU_MACHINE)
                string(
                    REPLACE
                        ";"
                        ","
                        QEMU_MACHINE
                        "${QEMU_MACHINE}"
                )
            endif()

            if(NOT DEFINED QEMU_CPU)
                set(QEMU_CPU "rv${KernelWordSize}")
            endif()

            if(NOT DEFINED QEMU_MEMORY)
                # Having 2 GiB of memory as default seems a good trade-off. It's
                # sufficient for test/demo systems, but still something the host
                # can provide without running short on resources.
                set(QEMU_MEMORY "2048M")
            endif()

            if(KernelMaxNumNodes)
                set(QEMU_SMP_OPTION "${KernelMaxNumNodes}")
            else()
                set(QEMU_SMP_OPTION "1")
            endif()

            # Run QEMU to get the device tree binary. Remember the command, so
            # it can be added to the DTS as reference.
            set(
                QEMU_CMD
                "${QEMU_BINARY}"
                "-machine"
                "${QEMU_MACHINE}"
                "-cpu"
                "${ARM_CPU}"
                "-smp"
                "${QEMU_SMP_OPTION}"
                "-m"
                "${QEMU_MEMORY}"
                "-nographic"
            )
            # When dumping the DTB to a file, QEMU prints a status message to
            # stderr. Capture it and print on stdout to avoid polluting stderr
            # unnecessarily.
            execute_process(
                COMMAND ${QEMU_CMD}
                RESULT_VARIABLE error
                ERROR_STRIP_TRAILING_WHITESPACE
                ERROR_VARIABLE QEMU_STDERR_MESSAGE
            )
            if(QEMU_STDERR_MESSAGE)
                message(STATUS "${QEMU_STDERR_MESSAGE}")
            endif()
            if(error)
                message(FATAL_ERROR "Failed to dump DTB using ${QEMU_RISCV_VIRT})")
            endif()

        endif()

        # At this point there is a DTB file, either it was passed or dumped from
        # QEMU. Create a DTS
        execute_process(
            COMMAND
                dtc -q -I dtb -O dts -o "${QEMU_DTS}" "${QEMU_DTB}"
            RESULT_VARIABLE error
        )
        if(error)
            message(FATAL_ERROR "Failed to create DTS from ${QEMU_DTB}")
        endif()

        set(QEMU_DTS_INFO "converted from ${QEMU_DTB}")
        if(QEMU_CMD)
            # Lists are just strings with ";" as item separator, so we can
            # simply replace them with something else. The cleaner is using a
            # CMake 3.12 feature: list(JOIN QEMU_CMD "\n *    " QEMU_DTS_INFO)
            string(
                REPLACE
                    ";"
                    "\n *    "
                    QEMU_DTS_INFO
                    "${QEMU_CMD}"
            )
        endif()
        file(
            WRITE
                "${QEMU_DTS}"
                "/*\n"
                " * ${QEMU_DTS_INFO}\n"
                " */\n"
                "\n"
                "${QEMU_DTS_DATA}"
        )

    endif()

    list(APPEND KernelDTSList "${QEMU_DTS}" "${CMAKE_CURRENT_LIST_DIR}/overlay-qemu-riscv-virt.dts")

    # QEMU emulates a SiFive PLIC/CLINT with 127 interrupt sources by default.
    # The CLINT timer pretends to run at 10 MHz, but this speed may not hold in
    # practical measurements.
    declare_default_headers(
        TIMER_FREQUENCY 10000000 PLIC_MAX_NUM_INT 128
        INTERRUPT_CONTROLLER drivers/irq/riscv_plic0.h
    )

endif()
