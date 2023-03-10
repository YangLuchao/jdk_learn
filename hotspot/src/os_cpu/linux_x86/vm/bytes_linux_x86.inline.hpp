/*
 * Copyright (c) 1999, 2010, Oracle and/or its affiliates. All rights reserved.
 * DO NOT ALTER OR REMOVE COPYRIGHT NOTICES OR THIS FILE HEADER.
 *
 * This code is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 only, as
 * published by the Free Software Foundation.
 *
 * This code is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
 * version 2 for more details (a copy is included in the LICENSE file that
 * accompanied this code).
 *
 * You should have received a copy of the GNU General Public License version
 * 2 along with this work; if not, write to the Free Software Foundation,
 * Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301 USA.
 *
 * Please contact Oracle, 500 Oracle Parkway, Redwood Shores, CA 94065 USA
 * or visit www.oracle.com if you need additional information or have any
 * questions.
 *
 */

#ifndef OS_CPU_LINUX_X86_VM_BYTES_LINUX_X86_INLINE_HPP
#define OS_CPU_LINUX_X86_VM_BYTES_LINUX_X86_INLINE_HPP

#include <byteswap.h>

// Efficient swapping of data bytes from Java byte
// ordering to native byte ordering and vice versa.
inline u2   Bytes::swap_u2(u2 x) {
#ifdef AMD64
  return bswap_16(x);
#else
  u2 ret;
  __asm__ __volatile__ (
    "movw %0, %%ax;"
    "xchg %%al, %%ah;"
    "movw %%ax, %0"
    :"=r" (ret)      // output : register 0 => ret
    :"0"  (x)        // input  : x => register 0
    :"ax", "0"       // clobbered registers
  );
  return ret;
#endif // AMD64
}
// 基于Linux内核的x86架构下64位系统的代码实现过程，其中调用的bswap_16()、bswap_32()和bswap_64()函数是GCC提供的内建函数。
// 由于HotSpot VM需要跨平台兼容，因此会增加一些针对各平台的特定实现，如Bytes::swap_u2()函数的完整实现代码
// 其中，AMD64表示x86架构下的64位系统实现。如果是非AMD64位的系统，可以使用GCC内联汇编实现相关的功能。
// 具体就是将x的值读入某个寄存器中，然后在指令中使用相应寄存器并将x的值移动到%ax寄存器中，通过xchg指令交换%eax寄存器中的高低位，再将最终的结果送入某个寄存器，最后将该结果送到ret中。
inline u4   Bytes::swap_u4(u4 x) {
#ifdef AMD64
  return bswap_32(x);
#else
  u4 ret;
  __asm__ __volatile__ (
    "bswap %0"
    :"=r" (ret)      // output : register 0 => ret
    :"0"  (x)        // input  : x => register 0
    :"0"             // clobbered register
  );
  return ret;
#endif // AMD64
}

#ifdef AMD64
inline u8 Bytes::swap_u8(u8 x) {
#ifdef SPARC_WORKS
  // workaround for SunStudio12 CR6615391
  __asm__ __volatile__ (
    "bswapq %0"
    :"=r" (x)        // output : register 0 => x
    :"0"  (x)        // input  : x => register 0
    :"0"             // clobbered register
  );
  return x;
#else
  return bswap_64(x);
#endif
}
#else
// Helper function for swap_u8
inline u8   Bytes::swap_u8_base(u4 x, u4 y) {
  return (((u8)swap_u4(x))<<32) | swap_u4(y);
}

inline u8 Bytes::swap_u8(u8 x) {
  return swap_u8_base(*(u4*)&x, *(((u4*)&x)+1));
}
#endif // !AMD64

#endif // OS_CPU_LINUX_X86_VM_BYTES_LINUX_X86_INLINE_HPP
