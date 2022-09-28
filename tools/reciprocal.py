#!/usr/bin/env python3
#
# Copyright 2020, Data61, CSIRO (ABN 41 687 119 230)
#
# SPDX-License-Identifier: GPL-2.0-only
#

# this script can be used to calculate the reciprocal for
# unsigned division of an unknown 64 bit value by a known 64bit
# constant. It is used to calculate the correct magic numbers
# for fixed cpu times on arm in order to do 64 bit division to calculate
# ticks -> microseconds.

# for details on how this script works,
# see Hacker's delight, Chapter 10, unsigned division.
import argparse


def magicgu(nmax: int, d: int)  -> (int, int):
    nc = ((nmax + 1)//d)*d - 1
    nbits = len(bin(nmax)) - 2
    for p in range(0, 2*nbits + 1):
        if 2**p > nc*(d - 1 - (2**p - 1) % d):
            m = (2**p + d - 1 - (2**p - 1) % d)//d
            return (m, p)
    raise ValueError("Can't find p, something is wrong.")


def do_div(n: int, magic: int, shift_amt: int) -> int:
    return (n  * magic) >> shift_amt


def calculate_recipocal(divisor: int):
    nmax = 2**32 - 1
    magic, shift_amt = magicgu(nmax, divisor)
    print(f'magic number is: {magic}, shift amount is {shift_amt}')
    # sanity check
    print("Doing sanity check for all 32-bit numbers")
    for i in range(nmax):
        if (i > 0) and (0 == (i & 0x1FFFFFF)):
            print(f'{100*(i/nmax):5.2f}%')
        q1 = i // divisor
        q2 = do_div(i, magic, shift_amt)
        if q1 != q2:
            raise ValueError(f'Combination failed for i={i}, q1={q1}, q2={q2}')

    print(f'Success! Use (n * {magic}) >> {shift_amt} to calculate n / {divisor}')
    print(f'magic number is: {magic}, shift amount is {shift_amt}')


if __name__ == "__main__":
    parser = argparse.ArgumentParser(
        description="Generate magic numbers for emulating 64-bit division with multiplication by reciprocal using algorithm from Hacker's Delight, chapter 10.")
    parser.add_argument("--divisor", type=int, required=True,
                        help="Devisor to calculate magic numbers for")
    args = parser.parse_args()
    calculate_recipocal(args.divisor)
