/*
 * Copyright (c) 1997, 2010, Oracle and/or its affiliates. All rights reserved.
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

#ifndef CPU_X86_VM_BYTES_X86_HPP
#define CPU_X86_VM_BYTES_X86_HPP

#include "memory/allocation.hpp"

class Bytes: AllStatic {
 private:
#ifndef AMD64
  // Helper function for swap_u8
  static inline u8   swap_u8_base(u4 x, u4 y);        // compiler-dependent implementation
#endif // AMD64

 public:
  // Returns true if the byte ordering used by Java is different from the native byte ordering
  // of the underlying machine. For example, this is true for Intel x86, but false for Solaris
  // on Sparc.
  static inline bool is_Java_byte_ordering_different(){ return true; }


  // Efficient reading and writing of unaligned unsigned data in platform-specific byte ordering
  // (no special code is needed since x86 CPUs can access unaligned data)
  static inline u2   get_native_u2(address p)         { return *(u2*)p; }
  static inline u4   get_native_u4(address p)         { return *(u4*)p; }
  static inline u8   get_native_u8(address p)         { return *(u8*)p; }

  static inline void put_native_u2(address p, u2 x)   { *(u2*)p = x; }
  static inline void put_native_u4(address p, u4 x)   { *(u4*)p = x; }
  static inline void put_native_u8(address p, u8 x)   { *(u8*)p = x; }


  // Efficient reading and writing of unaligned unsigned data in Java
  // byte ordering (i.e. big-endian ordering). Byte-order reversal is
  // needed since x86 CPUs use little-endian format.
  // 多字节数据项总是按照Big-Endian的顺序进行存储，而x86等处理器则使用相反的Little-Endian顺序存储数据。因此，在x86架构下需要进行转换
  static inline u2   get_Java_u2(address p)           { return swap_u2(get_native_u2(p)); }
  static inline u4   get_Java_u4(address p)           { return swap_u4(get_native_u4(p)); }
  static inline u8   get_Java_u8(address p)           { return swap_u8(get_native_u8(p)); }

  static inline void put_Java_u2(address p, u2 x)     { put_native_u2(p, swap_u2(x)); }
  static inline void put_Java_u4(address p, u4 x)     { put_native_u4(p, swap_u4(x)); }
  static inline void put_Java_u8(address p, u8 x)     { put_native_u8(p, swap_u8(x)); }


  // Efficient swapping of byte ordering
  static inline u2   swap_u2(u2 x);                   // compiler-dependent implementation
  static inline u4   swap_u4(u4 x);                   // compiler-dependent implementation
  static inline u8   swap_u8(u8 x);
};


// The following header contains the implementations of swap_u2, swap_u4, and swap_u8[_base]
#ifdef TARGET_OS_ARCH_linux_x86
# include "bytes_linux_x86.inline.hpp"
#endif
#ifdef TARGET_OS_ARCH_solaris_x86
# include "bytes_solaris_x86.inline.hpp"
#endif
#ifdef TARGET_OS_ARCH_windows_x86
# include "bytes_windows_x86.inline.hpp"
#endif
#ifdef TARGET_OS_ARCH_bsd_x86
# include "bytes_bsd_x86.inline.hpp"
#endif


#endif // CPU_X86_VM_BYTES_X86_HPP
