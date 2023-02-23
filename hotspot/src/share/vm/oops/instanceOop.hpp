/*
 * Copyright (c) 1997, 2013, Oracle and/or its affiliates. All rights reserved.
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

#ifndef SHARE_VM_OOPS_INSTANCEOOP_HPP
#define SHARE_VM_OOPS_INSTANCEOOP_HPP

#include "oops/oop.hpp"

// An instanceOop is an instance of a Java Class
// Evaluating "new HashTable()" will create an instanceOop.
// instanceOopDesc类的实例表示除数组对象外的其他对象。
// 在HotSpot虚拟机中，对象在内存中存储的布局可以分为三个区域：对象头（header）、对象字段数据（field data）和对齐填充（padding）。
// 对象头:对象头分为两部分，一部分是MarkWord，另一部分是存储指向元数据区对象类型数据的指针_klass或_compressed_klass。它们两个在介绍oopDesc类时详细讲过，这里不再赘述。
// 对象字段数据:Java对象中的字段数据存储了Java源代码中定义的各种类型的字段内容，具体包括父类继承及子类定义的字段。存储顺序受HotSpot VM布局策略命令-XX:FieldsAllocationStyle和字段在Java源代码中定义的顺序的影响，默认布局策略的顺序为long/double、int、short/char、boolean、oop（对象指针，32位系统占用4字节，64位系统占用8字节），相同宽度的字段总被分配到一起。如果虚拟机的-XX:+CompactFields参数为true，则子类中较窄的变量可能插入空隙中，以节省使用的内存空间。例如，当布局long/double类型的字段时，由于对齐的原因，可能会在header和long/double字段之间形成空隙，如64位系统开启压缩指针，header占12字节，剩下的4字节就是空隙，这时就可以将一些短类型插入long/double和header之间的空隙中。
// 对齐填充:对齐填充不是必需的，只起到占位符的作用，没有其他含义。HotSpot VM要求对象所占的内存必须是8字节的整数倍，对象头刚好是8字节的整数倍，因此填充是对实例数据没有对齐的情况而言的。对象所占的内存如果是以8字节对齐，那么对象在内存中进行线性分配时，对象头的地址就是以8字节对齐的，这时候就为对象指针压缩提供了条件，可以将地址缩小8倍进行存储。
class instanceOopDesc : public oopDesc {
 public:
  // aligned header size.
  static int header_size() { return sizeof(instanceOopDesc)/HeapWordSize; }

  // If compressed, the offset of the fields of the instance may not be aligned.
  static int base_offset_in_bytes() {
    // offset computation code breaks if UseCompressedClassPointers
    // only is true
    return (UseCompressedOops && UseCompressedClassPointers) ?
             // 开启指针压缩后计算的值为12
             klass_gap_offset_in_bytes() :
             // 在64位平台上计算的值为16
             sizeof(instanceOopDesc);
  }

  static bool contains_field_offset(int offset, int nonstatic_field_size) {
    int base_in_bytes = base_offset_in_bytes();
    return (offset >= base_in_bytes &&
            (offset-base_in_bytes) < nonstatic_field_size * heapOopSize);
  }
};

#endif // SHARE_VM_OOPS_INSTANCEOOP_HPP
