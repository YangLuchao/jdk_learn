/*
 * Copyright (c) 1997, 2014, Oracle and/or its affiliates. All rights reserved.
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

#ifndef SHARE_VM_RUNTIME_HANDLES_HPP
#define SHARE_VM_RUNTIME_HANDLES_HPP

#include "oops/klass.hpp"

//------------------------------------------------------------------------------------------------------------------------
// In order to preserve oops during garbage collection, they should be
// allocated and passed around via Handles within the VM. A handle is
// simply an extra indirection allocated in a thread local handle area.
//
// A handle is a ValueObj, so it can be passed around as a value, can
// be used as a parameter w/o using &-passing, and can be returned as a
// return value.
//
// oop parameters and return types should be Handles whenever feasible.
//
// Handles are declared in a straight-forward manner, e.g.
//
//   oop obj = ...;
//   Handle h1(obj);              // allocate new handle
//   Handle h2(thread, obj);      // faster allocation when current thread is known
//   Handle h3;                   // declare handle only, no allocation occurs
//   ...
//   h3 = h1;                     // make h3 refer to same indirection as h1
//   oop obj2 = h2();             // get handle value
//   h1->print();                 // invoking operation on oop
//
// Handles are specialized for different oop types to provide extra type
// information and avoid unnecessary casting. For each oop type xxxOop
// there is a corresponding handle called xxxHandle, e.g.
//
//   oop           Handle
//   Method*       methodHandle
//   instanceOop   instanceHandle

//------------------------------------------------------------------------------------------------------------------------
// Base class for all handles. Provides overloading of frequently
// used operators for ease of use.
// 当对oop直接引用时，如果oop的地址发生变化，那么所有的引用都要更新
// 当通过Handle对oop间接引用时，如果oop的地址发生变化，那么只需要更新Handle中保存的对oop的引用即可。
// 每个oop都有一个对应的Handle，Handle继承关系与oop继承关系类似，也有对应的关系，如通过instanceHandle操作instanceOopDesc，
// 另外还需要知道，Handle被分配在本地线程的HandleArea中，
// 这样在进行垃圾回收时只需要扫描每个线程的HandleArea即可找出所有Handle，进而找出所有引用的活跃对象。
class Handle VALUE_OBJ_CLASS_SPEC {
 private:
  // 可以看到是对oop的封装
  oop* _handle;

 protected:
  oop     obj() const                            { return _handle == NULL ? (oop)NULL : *_handle; }
  oop     non_null_obj() const                   { assert(_handle != NULL, "resolving NULL handle"); return *_handle; }

 public:
  // Constructors
  Handle()                                       { _handle = NULL; }
  Handle(oop obj);
  Handle(Thread* thread, oop obj);

  // General access
  // 获取被封装的oop对象，并不会直接调用Handle对象的obj()或non_null_obj()函数，
  // 而是通过C++的运算符重载来获取。Handle类重载了()和->运算符
  /*
  可以这样使用：
oop     obj = ...;
Handle  h1(obj);

oop obj1 = h1();
h1->print();
由于重载了运算符()，所以h1()会调用()运算符的重载方法，然后在重载方法中调用obj()函数获取被封装的oop对象。
h1->print()同样会通过->运算符的重载方法调用oop对象的print()方法。
  */
  oop     operator () () const                   { return obj(); }
  oop     operator -> () const                   { return non_null_obj(); }
  bool    operator == (oop o) const              { return obj() == o; }
  bool    operator == (const Handle& h) const          { return obj() == h.obj(); }

  // Null checks
  bool    is_null() const                        { return _handle == NULL; }
  bool    not_null() const                       { return _handle != NULL; }

  // Debugging
  void    print()                                { obj()->print(); }

  // Direct interface, use very sparingly.
  // Used by JavaCalls to quickly convert handles and to create handles static data structures.
  // Constructor takes a dummy argument to prevent unintentional type conversion in C++.
  Handle(oop *handle, bool dummy)                { _handle = handle; }

  // Raw handle access. Allows easy duplication of Handles. This can be very unsafe
  // since duplicates is only valid as long as original handle is alive.
  oop* raw_value()                               { return _handle; }
  static oop raw_resolve(oop *handle)            { return handle == NULL ? (oop)NULL : *handle; }
};

// Specific Handles for different oop types
#define DEF_HANDLE(type, is_a)                   \
  class type##Handle: public Handle {            \
   protected:                                    \
    type##Oop    obj() const                     { return (type##Oop)Handle::obj(); } \
    type##Oop    non_null_obj() const            { return (type##Oop)Handle::non_null_obj(); } \
                                                 \
   public:                                       \
    /* Constructors */                           \
    type##Handle ()                              : Handle()                 {} \
    type##Handle (type##Oop obj) : Handle((oop)obj) {                         \
      assert(is_null() || ((oop)obj)->is_a(),                                 \
             "illegal type");                                                 \
    }                                                                         \
    type##Handle (Thread* thread, type##Oop obj) : Handle(thread, (oop)obj) { \
      assert(is_null() || ((oop)obj)->is_a(), "illegal type");                \
    }                                                                         \
    \
    /* Operators for ease of use */              \
    type##Oop    operator () () const            { return obj(); } \
    type##Oop    operator -> () const            { return non_null_obj(); } \
  };


DEF_HANDLE(instance         , is_instance         )
DEF_HANDLE(array            , is_array            )
DEF_HANDLE(objArray         , is_objArray         )
DEF_HANDLE(typeArray        , is_typeArray        )

//------------------------------------------------------------------------------------------------------------------------

// Metadata Handles.  Unlike oop Handles these are needed to prevent metadata
// from being reclaimed by RedefineClasses.

// Specific Handles for different oop types
#define DEF_METADATA_HANDLE(name, type)          \
  class name##Handle;                            \
  class name##Handle : public StackObj {         \
    type*     _value;                            \
    Thread*   _thread;                           \
   protected:                                    \
    type*        obj() const                     { return _value; } \
    type*        non_null_obj() const            { assert(_value != NULL, "resolving NULL _value"); return _value; } \
                                                 \
   public:                                       \
    /* Constructors */                           \
    name##Handle () : _value(NULL), _thread(NULL) {}   \
    name##Handle (type* obj);                    \
    name##Handle (Thread* thread, type* obj);    \
                                                 \
    name##Handle (const name##Handle &h);        \
    name##Handle& operator=(const name##Handle &s); \
                                                 \
    /* Destructor */                             \
    ~name##Handle ();                            \
    void remove();                               \
                                                 \
    /* Operators for ease of use */              \
    type*        operator () () const            { return obj(); } \
    type*        operator -> () const            { return non_null_obj(); } \
                                                 \
    bool    operator == (type* o) const          { return obj() == o; } \
    bool    operator == (const name##Handle& h) const  { return obj() == h.obj(); } \
                                                 \
    /* Null checks */                            \
    bool    is_null() const                      { return _value == NULL; } \
    bool    not_null() const                     { return _value != NULL; } \
  };


DEF_METADATA_HANDLE(method, Method)
DEF_METADATA_HANDLE(constantPool, ConstantPool)

// Writing this class explicitly, since DEF_METADATA_HANDLE(klass) doesn't
// provide the necessary Klass* <-> Klass* conversions. This Klass
// could be removed when we don't have the Klass* typedef anymore.
// Klass句柄
class KlassHandle : public StackObj {
  Klass* _value;
 protected:
   Klass* obj() const          { return _value; }
   Klass* non_null_obj() const { assert(_value != NULL, "resolving NULL _value"); return _value; }

 public:
   KlassHandle()                                 : _value(NULL) {}
   KlassHandle(const Klass* obj)                 : _value(const_cast<Klass *>(obj)) {};
   KlassHandle(Thread* thread, const Klass* obj) : _value(const_cast<Klass *>(obj)) {};
   // 重载()
   Klass* operator () () const { return obj(); }
   // 重载->
   Klass* operator -> () const { return non_null_obj(); }
   // 重载==
   bool operator == (Klass* o) const             { return obj() == o; }
   // 重载==
   bool operator == (const KlassHandle& h) const { return obj() == h.obj(); }

    bool is_null() const  { return _value == NULL; }
    bool not_null() const { return _value != NULL; }
};

class instanceKlassHandle : public KlassHandle {
 public:
  /* Constructors */
  instanceKlassHandle () : KlassHandle() {}
  instanceKlassHandle (const Klass* k) : KlassHandle(k) {
    assert(k == NULL || k->oop_is_instance(),
           "illegal type");
  }
  instanceKlassHandle (Thread* thread, const Klass* k) : KlassHandle(thread, k) {
    assert(k == NULL || k->oop_is_instance(),
           "illegal type");
  }
  /* Access to klass part */
  InstanceKlass*       operator () () const { return (InstanceKlass*)obj(); }
  InstanceKlass*       operator -> () const { return (InstanceKlass*)obj(); }
};


//------------------------------------------------------------------------------------------------------------------------
// Thread local handle area
// 句柄都是在HandleArea中分配并存储的
class HandleArea: public Arena {
  friend class HandleMark;
  friend class NoHandleMark;
  friend class ResetNoHandleMark;
#ifdef ASSERT
  int _handle_mark_nesting;
  int _no_handle_mark_nesting;
#endif
  // HandleArea通过_prev连接成单链表
  HandleArea* _prev;          // link to outer (older) area
 public:
  // Constructor
  HandleArea(HandleArea* prev) : Arena(mtThread, Chunk::tiny_size) {
    debug_only(_handle_mark_nesting    = 0);
    debug_only(_no_handle_mark_nesting = 0);
    _prev = prev;
  }

  // Handle allocation
 private:
  // allocate_handle()函数为对象obj分配了一个新的句柄
  // 分配内存并存储obj对象
  // real_allocate_handle()函数在HandleArea中分配内存并存储obj对象，该函数调用父类Arena中定义的Amalloc_4()函数分配内存
  oop* real_allocate_handle(oop obj) {
#ifdef ASSERT
    oop* handle = (oop*) (UseMallocOnly ? internal_malloc_4(oopSize) : Amalloc_4(oopSize));
#else
    // 在代码中分配了一个新的空间并存储obj。
    oop* handle = (oop*) Amalloc_4(oopSize);
#endif
    *handle = obj;
    return handle;
  }
 public:
#ifdef ASSERT
  oop* allocate_handle(oop obj);
#else
  // allocate_handle()函数为对象obj分配了一个新的句柄
  oop* allocate_handle(oop obj) { return real_allocate_handle(obj); }
#endif

  // Garbage collection support
  void oops_do(OopClosure* f);

  // Number of handles in use
  size_t used() const     { return Arena::used() / oopSize; }

  debug_only(bool no_handle_mark_active() { return _no_handle_mark_nesting > 0; })
};


//------------------------------------------------------------------------------------------------------------------------
// Handles are allocated in a (growable) thread local handle area. Deallocation
// is managed using a HandleMark. It should normally not be necessary to use
// HandleMarks manually.
//
// A HandleMark constructor will record the current handle area top, and the
// desctructor will reset the top, destroying all handles allocated in between.
// The following code will therefore NOT work:
//
//   Handle h;
//   {
//     HandleMark hm;
//     h = Handle(obj);
//   }
//   h()->print();       // WRONG, h destroyed by HandleMark destructor.
//
// If h has to be preserved, it can be converted to an oop or a local JNI handle
// across the HandleMark boundary.

// The base class of HandleMark should have been StackObj but we also heap allocate
// a HandleMark when a thread is created. The operator new is for this special case.
// 每一个Java线程都有一个私有的句柄区_handle_area用来存储其运行过程中的句柄信息，这个句柄区会随着Java线程的栈帧而变化。
// Java线程每调用一个Java方法就会创建一个对应的HandleMark保存创建的对象句柄，然后等调用返回后释放这些对象句柄，
// 此时释放的仅是调用当前方法创建的句柄，因此HandleMark只需要恢复到调用方法之前的状态即可
// HandleMark主要用于记录当前线程的HandleArea的内存地址top，当相关的作用域执行完成后，当前作用域之内的HandleMark实例会自动销毁
// 在HandleMark的析构函数中会将HandleArea当前的内存地址到方法调用前的内存地址top之间所有分配的地址中存储的内容都销毁，
// 然后恢复当前线程的HandleArea的内存地址top为方法调用前的状态。
class HandleMark {
 private:
  // 拥有当前HandleMark实例的线程
  Thread *_thread;              // thread that owns this mark
  // Chunk和Area配合，获得准确的内存地址
  HandleArea *_area;            // saved handle area
  Chunk *_chunk;                // saved arena chunk
  char *_hwm, *_max;            // saved arena info
  size_t _size_in_bytes;        // size of handle area
  // Link to previous active HandleMark in thread
  // 通过如下属性让HandleMark形成单链表
  HandleMark* _previous_handle_mark;

  void initialize(Thread* thread);                // common code for constructors
  void set_previous_handle_mark(HandleMark* mark) { _previous_handle_mark = mark; }
  HandleMark* previous_handle_mark() const        { return _previous_handle_mark; }

  size_t size_in_bytes() const { return _size_in_bytes; }
 public:
  HandleMark();                            // see handles_inline.hpp
  // 在HandleMark的构造方法中会调用initialize()方法
  // 创建一个新的HandleMark以后，它保存当前线程的area的_chunk、_hwm和_max等属性，
  // 代码执行期间新创建的Handle实例是在当前线程的area中分配内存，
  // 这会导致当前线程的area的_chunk、_hwm和_max等属性发生变化，
  // 因此代码执行完成后需要将这些属性恢复至之前的状态，并释放代码执行过程中新创建的Handle实例的内存。
  HandleMark(Thread* thread)                      { initialize(thread); }
  ~HandleMark();

  // Functions used by HandleMarkCleaner
  // called in the constructor of HandleMarkCleaner
  void push();
  // called in the destructor of HandleMarkCleaner
  void pop_and_restore();
  // overloaded operators
  // 一般情况下，HandleMark直接在线程栈内存上分配内存，应该继承自StackObj，
  // 但有时HandleMark也需要在堆内存上分配，因此没有继承自StackObj，
  // 并且为了支持在堆内存上分配内存，重载了new和delete方法。
  void* operator new(size_t size) throw();
  void* operator new [](size_t size) throw();
  void operator delete(void* p);
  void operator delete[](void* p);
};

//------------------------------------------------------------------------------------------------------------------------
// A NoHandleMark stack object will verify that no handles are allocated
// in its scope. Enabled in debug mode only.

class NoHandleMark: public StackObj {
 public:
#ifdef ASSERT
  NoHandleMark();
  ~NoHandleMark();
#else
  NoHandleMark()  {}
  ~NoHandleMark() {}
#endif
};


class ResetNoHandleMark: public StackObj {
  int _no_handle_mark_nesting;
 public:
#ifdef ASSERT
  ResetNoHandleMark();
  ~ResetNoHandleMark();
#else
  ResetNoHandleMark()  {}
  ~ResetNoHandleMark() {}
#endif
};

#endif // SHARE_VM_RUNTIME_HANDLES_HPP
