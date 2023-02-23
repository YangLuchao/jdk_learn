/*
 * Copyright (c) 2011, 2013, Oracle and/or its affiliates. All rights reserved.
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

#ifndef SHARE_VM_OOPS_INSTANCEMIRRORKLASS_HPP
#define SHARE_VM_OOPS_INSTANCEMIRRORKLASS_HPP

#include "classfile/systemDictionary.hpp"
#include "oops/instanceKlass.hpp"
#include "runtime/handles.hpp"
#include "utilities/macros.hpp"

// An InstanceMirrorKlass is a specialized InstanceKlass for
// java.lang.Class instances.  These instances are special because
// they contain the static fields of the class in addition to the
// normal fields of Class.  This means they are variable sized
// instances and need special logic for computing their size and for
// iteration of their oops.
// HotSpot VM使用Klass表示Java类，用oop表示Java对象。
// 而Java类中可能定义了静态或非静态字段，因此将非静态字段值存储在oop中，
// 静态字段值存储在表示当前Java类的java.lang.Class对象中。
// 表示java.lang.Class类的InstanceMirrorKlass
/*
添加虚拟机参数命令-XX:+PrintFieldLayout后，打印的java.lang.Class对象的非静态字段布局如下：
java.lang.Class: field layout
  @ 12 --- instance fields start ---
  @ 12 "cachedConstructor" Ljava.lang.reflect.Constructor;
  @ 16 "newInstanceCallerCache" Ljava.lang.Class;
  @ 20 "name" Ljava.lang.String;
  @ 24 "reflectionData" Ljava.lang.ref.SoftReference;
  @ 28 "genericInfo" Lsun.reflect.generics.repository.ClassRepository;
  @ 32 "enumConstants" [Ljava.lang.Object;
  @ 36 "enumConstantDirectory" Ljava.util.Map;
  @ 40 "annotationData" Ljava.lang.Class$AnnotationData;
  @ 44 "annotationType" Lsun.reflect.annotation.AnnotationType;
  @ 48 "classValueMap" Ljava.lang.ClassValue$ClassValueMap;
  @ 52 "protection_domain" Ljava.lang.Object;
  @ 56 "init_lock" Ljava.lang.Object;
  @ 60 "signers_name" Ljava.lang.Object;
  @ 64 "klass" J
  @ 72 "array_klass" J
  @ 80 "classRedefinedCount" I
  @ 84 "oop_size" I
  @ 88 "static_oop_field_count" I
  @ 92 --- instance fields end ---
  @ 96 --- instance ends ---
  以上就是java.lang.Class对象非静态字段的布局，在类解析过程中已经计算出了各个字段的偏移量。
  当完成非静态字段的布局后，紧接着会布局静态字段，此时的_offset_of_static_fields属性的值为96。
*/
class InstanceMirrorKlass: public InstanceKlass {
  friend class VMStructs;
  friend class InstanceKlass;

 private:
  // InstanceMirrorKlass类实例表示特殊的java.lang.Class类，
  // 这个类中新增了一个静态属性_offset_of_static_fields，用来保存静态字段的起始偏移量
  // vjava.lang.Class类比较特殊，用InstanceMirrorKlass实例表示，
  // java.lang.Class对象用oop对象表示。
  // 由于java.lang.Class类自身也定义了静态字段，这些值同样存储在java.lang.Class对象中，
  // 也就是存储在表示java.lang.Class对象的oop中，这样静态与非静态字段就存储在一个oop上，
  // 需要参考_offset_of_static_fields属性的值进行偏移来定位静态字段的存储位置。
  static int _offset_of_static_fields;

  // Constructor
  InstanceMirrorKlass(int vtable_len, int itable_len, int static_field_size, int nonstatic_oop_map_size, ReferenceType rt, AccessFlags access_flags,  bool is_anonymous)
    : InstanceKlass(vtable_len, itable_len, static_field_size, nonstatic_oop_map_size, rt, access_flags, is_anonymous) {}

 public:
  InstanceMirrorKlass() { assert(DumpSharedSpaces || UseSharedSpaces, "only for CDS"); }
  // Type testing
  bool oop_is_instanceMirror() const             { return true; }

  // Casting from Klass*
  static InstanceMirrorKlass* cast(Klass* k) {
    assert(k->oop_is_instanceMirror(), "cast to InstanceMirrorKlass");
    return (InstanceMirrorKlass*) k;
  }

  // Returns the size of the instance including the extra static fields.
  virtual int oop_size(oop obj) const;

  // Static field offset is an offset into the Heap, should be converted by
  // based on UseCompressedOop for traversal
  static HeapWord* start_of_static_fields(oop obj) {
    return (HeapWord*)(cast_from_oop<intptr_t>(obj) + offset_of_static_fields());
  }

  // 初始化_offset_of_static_fields字段
  static void init_offset_of_static_fields() {
    // Cache the offset of the static fields in the Class instance
    assert(_offset_of_static_fields == 0, "once");
    // 调用size_helper()函数获取oop（表示java.lang.Class对象）的大小，左移3位将字转换为字节。紧接着oop后开始存储静态字段的值。
    _offset_of_static_fields = InstanceMirrorKlass::cast(SystemDictionary::Class_klass())->size_helper() << LogHeapWordSize;
  }

  static int offset_of_static_fields() {
    return _offset_of_static_fields;
  }

  int compute_static_oop_field_count(oop obj);

  // Given a Klass return the size of the instance
  int instance_size(KlassHandle k);

  // allocation
  instanceOop allocate_instance(KlassHandle k, TRAPS);

  // Garbage collection
  int  oop_adjust_pointers(oop obj);
  void oop_follow_contents(oop obj);

  // Parallel Scavenge and Parallel Old
  PARALLEL_GC_DECLS

  int oop_oop_iterate(oop obj, ExtendedOopClosure* blk) {
    return oop_oop_iterate_v(obj, blk);
  }
  int oop_oop_iterate_m(oop obj, ExtendedOopClosure* blk, MemRegion mr) {
    return oop_oop_iterate_v_m(obj, blk, mr);
  }

#define InstanceMirrorKlass_OOP_OOP_ITERATE_DECL(OopClosureType, nv_suffix)           \
  int oop_oop_iterate##nv_suffix(oop obj, OopClosureType* blk);                       \
  int oop_oop_iterate##nv_suffix##_m(oop obj, OopClosureType* blk, MemRegion mr);

  ALL_OOP_OOP_ITERATE_CLOSURES_1(InstanceMirrorKlass_OOP_OOP_ITERATE_DECL)
  ALL_OOP_OOP_ITERATE_CLOSURES_2(InstanceMirrorKlass_OOP_OOP_ITERATE_DECL)

#if INCLUDE_ALL_GCS
#define InstanceMirrorKlass_OOP_OOP_ITERATE_BACKWARDS_DECL(OopClosureType, nv_suffix) \
  int oop_oop_iterate_backwards##nv_suffix(oop obj, OopClosureType* blk);

  ALL_OOP_OOP_ITERATE_CLOSURES_1(InstanceMirrorKlass_OOP_OOP_ITERATE_BACKWARDS_DECL)
  ALL_OOP_OOP_ITERATE_CLOSURES_2(InstanceMirrorKlass_OOP_OOP_ITERATE_BACKWARDS_DECL)
#endif // INCLUDE_ALL_GCS
};

#endif // SHARE_VM_OOPS_INSTANCEMIRRORKLASS_HPP