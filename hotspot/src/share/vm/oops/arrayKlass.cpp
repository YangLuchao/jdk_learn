/*
 * Copyright (c) 1997, 2017, Oracle and/or its affiliates. All rights reserved.
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

#include "precompiled.hpp"
#include "classfile/javaClasses.hpp"
#include "classfile/systemDictionary.hpp"
#include "classfile/vmSymbols.hpp"
#include "gc_interface/collectedHeap.inline.hpp"
#include "jvmtifiles/jvmti.h"
#include "memory/gcLocker.hpp"
#include "memory/universe.inline.hpp"
#include "oops/arrayKlass.hpp"
#include "oops/arrayOop.hpp"
#include "oops/instanceKlass.hpp"
#include "oops/objArrayOop.hpp"
#include "oops/oop.inline.hpp"

int ArrayKlass::static_size(int header_size) {
  // size of an array klass object
  // header_size属性的值应该是TypeArrayKlass类自身占用的内存空间，但是现在获取的是InstanceKlass类自身占用的内存空间。
  // 这是因为InstanceKlass占用的内存比TypeArrayKlass大，有足够内存来存放相关数据。
  // 更重要的是，为了统一从固定的偏移位置获取vtable等信息，在实际操作Klass实例的过程中无须关心是数组还是类，直接偏移固定位置后就可获取。
  assert(header_size <= InstanceKlass::header_size(), "bad header size");
  // If this assert fails, see comments in base_create_array_klass.
  // ArrayKlass实例所需要的内存的计算逻辑，以上函数在计算header_size时获取的是InstanceKlass本身占用的内存空间，而不是ArrayKlass本身占用的内存空间
  // 这是因为InstanceKlass本身占用的内存空间比ArrayKlass大，所以以InstanceKlass本身占用的内存空间为标准进行统一操作，在不区分Klass实例的具体类型时，
  // 只要偏移InstanceKlass::header_size()后就可以获取vtable等信息。
  header_size = InstanceKlass::header_size();
  int vtable_len = Universe::base_vtable_size();
#ifdef _LP64
  int size = header_size + align_object_offset(vtable_len);
#else
  int size = header_size + vtable_len;
#endif
  return align_object_size(size);
}


Klass* ArrayKlass::java_super() const {
  if (super() == NULL)  return NULL;  // bootstrap case
  // Array klasses have primary supertypes which are not reported to Java.
  // Example super chain:  String[][] -> Object[][] -> Object[] -> Object
  return SystemDictionary::Object_klass();
}


oop ArrayKlass::multi_allocate(int rank, jint* sizes, TRAPS) {
  ShouldNotReachHere();
  return NULL;
}

// find field according to JVM spec 5.4.3.2, returns the klass in which the field is defined
Klass* ArrayKlass::find_field(Symbol* name, Symbol* sig, fieldDescriptor* fd) const {
  // There are no fields in an array klass but look to the super class (Object)
  assert(super(), "super klass must be present");
  return super()->find_field(name, sig, fd);
}

Method* ArrayKlass::uncached_lookup_method(Symbol* name, Symbol* signature, OverpassLookupMode overpass_mode) const {
  // There are no methods in an array klass but the super class (Object) has some
  assert(super(), "super klass must be present");
  // Always ignore overpass methods in superclasses, although technically the
  // super klass of an array, (j.l.Object) should not have
  // any overpass methods present.
  return super()->uncached_lookup_method(name, signature, Klass::skip_overpass);
}

ArrayKlass::ArrayKlass(Symbol* name) {
  set_name(name);

  set_super(Universe::is_bootstrapping() ? (Klass*)NULL : SystemDictionary::Object_klass());
  set_layout_helper(Klass::_lh_neutral_value);
  set_dimension(1);
  set_higher_dimension(NULL);
  set_lower_dimension(NULL);
  set_component_mirror(NULL);
  // Arrays don't add any new methods, so their vtable is the same size as
  // the vtable of klass Object.
  int vtable_size = Universe::base_vtable_size();
  set_vtable_length(vtable_size);
  set_is_cloneable(); // All arrays are considered to be cloneable (See JLS 20.1.5)
  JFR_ONLY(INIT_ID(this);)
}


// Initialization of vtables and mirror object is done separatly from base_create_array_klass,
// since a GC can happen. At this point all instance variables of the ArrayKlass must be setup.
// 虚表和镜像对象的初始化与base_create_array_klass是分开进行的，因为可能发生GC。此时，必须设置ArrayKlass的所有实例变量。
void ArrayKlass::complete_create_array_klass(ArrayKlass* k, KlassHandle super_klass, TRAPS) {
  ResourceMark rm(THREAD);
  // initialize_supers()函数初始化_primary_supers、_super_check_offset等属性
  // 初始化数组类的父类
  k->initialize_supers(super_klass(), CHECK);
  // 初始化vtable表
  k->vtable()->initialize_vtable(false, CHECK);
  // 调用java_lang_Class::create_mirror()函数对_component_mirror属性进行设置
  // 调用java_lang_Class::create_mirror()函数完成当前ObjTypeArray对象对应的java.lang.Class对象的创建并设置相关属性。
  java_lang_Class::create_mirror(k, Handle(THREAD, k->class_loader()), Handle(NULL), CHECK);
}

GrowableArray<Klass*>* ArrayKlass::compute_secondary_supers(int num_extra_slots) {
  // interfaces = { cloneable_klass, serializable_klass };
  assert(num_extra_slots == 0, "sanity of primitive array type");
  // Must share this for correct bootstrapping!
  set_secondary_supers(Universe::the_array_interfaces_array());
  return NULL;
}

bool ArrayKlass::compute_is_subtype_of(Klass* k) {
  // An array is a subtype of Serializable, Clonable, and Object
  return    k == SystemDictionary::Object_klass()
         || k == SystemDictionary::Cloneable_klass()
         || k == SystemDictionary::Serializable_klass();
}


inline intptr_t* ArrayKlass::start_of_vtable() const {
  // all vtables start at the same place, that's why we use InstanceKlass::header_size here
  return ((intptr_t*)this) + InstanceKlass::header_size();
}


klassVtable* ArrayKlass::vtable() const {
  KlassHandle kh(Thread::current(), this);
  return new klassVtable(kh, start_of_vtable(), vtable_length() / vtableEntry::size());
}


objArrayOop ArrayKlass::allocate_arrayArray(int n, int length, TRAPS) {
  if (length < 0) {
    THROW_0(vmSymbols::java_lang_NegativeArraySizeException());
  }
  if (length > arrayOopDesc::max_array_length(T_ARRAY)) {
    report_java_out_of_memory("Requested array size exceeds VM limit");
    JvmtiExport::post_array_size_exhausted();
    THROW_OOP_0(Universe::out_of_memory_error_array_size());
  }
  int size = objArrayOopDesc::object_size(length);
  Klass* k = array_klass(n+dimension(), CHECK_0);
  ArrayKlass* ak = ArrayKlass::cast(k);
  objArrayOop o =
    (objArrayOop)CollectedHeap::array_allocate(ak, size, length, CHECK_0);
  // initialization to NULL not necessary, area already cleared
  return o;
}

void ArrayKlass::array_klasses_do(void f(Klass* k, TRAPS), TRAPS) {
  Klass* k = this;
  // Iterate over this array klass and all higher dimensions
  while (k != NULL) {
    f(k, CHECK);
    k = ArrayKlass::cast(k)->higher_dimension();
  }
}

void ArrayKlass::array_klasses_do(void f(Klass* k)) {
  Klass* k = this;
  // Iterate over this array klass and all higher dimensions
  while (k != NULL) {
    f(k);
    k = ArrayKlass::cast(k)->higher_dimension();
  }
}

// GC support

void ArrayKlass::oops_do(OopClosure* cl) {
  Klass::oops_do(cl);

  cl->do_oop(adr_component_mirror());
}

// JVM support

jint ArrayKlass::compute_modifier_flags(TRAPS) const {
  return JVM_ACC_ABSTRACT | JVM_ACC_FINAL | JVM_ACC_PUBLIC;
}

// JVMTI support

jint ArrayKlass::jvmti_class_status() const {
  return JVMTI_CLASS_STATUS_ARRAY;
}

void ArrayKlass::remove_unshareable_info() {
  Klass::remove_unshareable_info();
  // Clear the java mirror
  set_component_mirror(NULL);
}

void ArrayKlass::restore_unshareable_info(ClassLoaderData* loader_data, Handle protection_domain, TRAPS) {
  assert(loader_data == ClassLoaderData::the_null_class_loader_data(), "array classes belong to null loader");
  Klass::restore_unshareable_info(loader_data, protection_domain, CHECK);
  // Klass recreates the component mirror also
}

// Printing

void ArrayKlass::print_on(outputStream* st) const {
  assert(is_klass(), "must be klass");
  Klass::print_on(st);
}

void ArrayKlass::print_value_on(outputStream* st) const {
  assert(is_klass(), "must be klass");
  for(int index = 0; index < dimension(); index++) {
    st->print("[]");
  }
}

void ArrayKlass::oop_print_on(oop obj, outputStream* st) {
  assert(obj->is_array(), "must be array");
  Klass::oop_print_on(obj, st);
  st->print_cr(" - length: %d", arrayOop(obj)->length());
}


// Verification

void ArrayKlass::verify_on(outputStream* st) {
  Klass::verify_on(st);

  if (component_mirror() != NULL) {
    guarantee(component_mirror()->klass() != NULL, "should have a class");
  }
}

void ArrayKlass::oop_verify_on(oop obj, outputStream* st) {
  guarantee(obj->is_array(), "must be array");
  arrayOop a = arrayOop(obj);
  guarantee(a->length() >= 0, "array with negative length?");
}
