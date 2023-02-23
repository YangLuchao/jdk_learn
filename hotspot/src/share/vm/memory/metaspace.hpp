/*
 * Copyright (c) 2011, 2019, Oracle and/or its affiliates. All rights reserved.
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
#ifndef SHARE_VM_MEMORY_METASPACE_HPP
#define SHARE_VM_MEMORY_METASPACE_HPP

#include "memory/allocation.hpp"
#include "memory/memRegion.hpp"
#include "memory/metaspaceChunkFreeListSummary.hpp"
#include "runtime/virtualspace.hpp"
#include "utilities/exceptions.hpp"

// Metaspace
//
// Metaspaces are Arenas for the VM's metadata.
// They are allocated one per class loader object, and one for the null
// bootstrap class loader
// Eventually for bootstrap loader we'll have a read-only section and read-write
// to write for DumpSharedSpaces and read for UseSharedSpaces
//
//    block X ---+       +-------------------+
//               |       |  Virtualspace     |
//               |       |                   |
//               |       |                   |
//               |       |-------------------|
//               |       || Chunk            |
//               |       ||                  |
//               |       ||----------        |
//               +------>||| block 0 |       |
//                       ||----------        |
//                       ||| block 1 |       |
//                       ||----------        |
//                       ||                  |
//                       |-------------------|
//                       |                   |
//                       |                   |
//                       +-------------------+
//

class ChunkManager;
class ClassLoaderData;
class Metablock;
class Metachunk;
class MetaspaceTracer;
class MetaWord;
class Mutex;
class outputStream;
class SpaceManager;
class VirtualSpaceList;

// Metaspaces each have a  SpaceManager and allocations
// are done by the SpaceManager.  Allocations are done
// out of the current Metachunk.  When the current Metachunk
// is exhausted, the SpaceManager gets a new one from
// the current VirtualSpace.  When the VirtualSpace is exhausted
// the SpaceManager gets a new one.  The SpaceManager
// also manages freelists of available Chunks.
//
// Currently the space manager maintains the list of
// virtual spaces and the list of chunks in use.  Its
// allocate() method returns a block for use as a
// quantum of metadata.
/*
元空间
从OpenJDK 8开始，使用元空间（Metaspace）替换了之前版本中使用的永久代（PermGen）。永久代主要存放以下数据：
  类的元数据信息，如常量池、方法等；
  类的静态信息；
  字符串驻留。

相关的数据已经被转移到元空间或堆中了，
如字符串驻留和类的静态信息被转移到了堆中，
而类的元数据信息被转移到了元空间中，因此前面介绍的保存类的元数据信息的Klass、Method、ConstMethod与ConstantPool等实例都是在元空间上分配内存。

Metaspace区域位于堆外，因此它的内存大小取决于系统内存而不是堆大小，我们可以指定MaxMetaspaceSize参数来限定它的最大内存。

Metaspace用来存放类的元数据信息，元数据信息用于记录一个Java类在JVM中的信息，包括以下几类信息：

  Klass结构：可以理解为类在HotSpot VM内部的对等表示。
  Method与ConstMethod：保存Java方法的相关信息，包括方法的字节码、局部变量表、异常表和参数信息等。
  ConstantPool：保存常量池信息。
  注解：提供与程序有关的元数据信息，但是这些信息并不属于程序本身。
  方法计数器：记录方法被执行的次数，用来辅助JIT决策。

除了以上最主要的5项信息外，还有一些占用内存比较小的元数据信息也存放在Metaspace里。

虽然每个Java类都关联了一个java.lang.Class对象，而且是一个保存在堆中的Java对象，但是类的元数据信息不是一个Java对象，它不在堆中而是在Metaspace中。

元空间数据结构
元空间主要用来存储Java类的元数据信息，每个类加载器都会在元空间得到自己的存储区域。当一个类加载器被垃圾收集器标记为不再存活时，这块存储区域将会被回收。

HotSpot VM负责对元空间的内存进行管理，包括分配、回收及释放内存等。如图 test/ylctest/8/8.1/元空间的内存结构.png

在元空间中会保存一个VirtualSpace的单链表，每个链表的节点为VirtualSpaceNode，单链表之间通过VirtualSpaceNode类中定义的next属性连接。
每个VirtualSpaceNode节点在实际使用过程中会分出4种不同大小的Metachunk块，主要是增加灵活分配的同时便于复用，方便管理。

一个块只归属一个类加载器。类加载器可能会使用特定大小的块，如反射或者匿名类加载器，也可能根据其内部条目的数量使用小型或者中型的组块，如用户类加载器。

链表VirtualSpaceList和节点Node是全局的，而Node内部的MetaChunk块是分配给每个类加载器的，因此一个Node通常由分配给多个类加载器的chunks组成。

元空间定义的所属性都是成对出现，因为元空间其实还有类指针压缩空间（Compressed Class Pointer Space），这两部分是相互独立的。

只有当64位平台上启用了类指针压缩后才会存在这个区域。
对于64位平台，为了压缩JVM对象中的_klass指针的大小，引入了类指针压缩空间（Compressed Class Pointer Space）。
对象中指向类元数据的指针会被压缩成32位。

元空间和类指针压缩空间的区别如下：

  类指针压缩空间只包含类的元数据，如InstanceKlass和ArrayKlass，虚拟机仅在打开了UseCompressedClassPointers选项时才生效。
  为了提高性能，Java中的虚方法表也存放到这里。
  
  元空间包含的是类里比较大的元数据，如方法、字节码和常量池等。

我们暂时不讨论类指针压缩空间，因为其数据结构及内存管理与元空间完全相同。
我们只看_vsm、_space_list与_chunk_manager_class，后两个属性是静态的，无论有多少个Metaspace实例，它们都共享这两个属性值。
_spaceList就是指定VirtualSpaceList，共享存储空间，
而_chunk_manager_class用于管理空闲内存块，如果某个类加载器卸载了，则会回收这个类加载器占用的所有Metachunk块，以便下次有类加载器申请空间时重复使用。
_vsm是实例字段，不同的Metaspace会有不同的值，也就是说不同的类加载器会有各自的SpaceManager来管理自己的Metachunk块。

每个类加载器都会对应一个ClassLoaderData实例，该实例负责初始化并销毁一个ClassLoader实例对应的Metaspace。
类加载器共有4类，分别是根类加载器、反射类加载器、匿名类加载器和普通类加载器，其中反射类加载器和匿名类加载器比较少见，
根类加载器在第3章中介绍过，其使用C++编写，其他的扩展类加载器、应用类加载器及自定义的加载器等都属于普通类加载器。
每个加载器都会对应一个Metaspace实例，创建出的实例由ClassLoaderData类中定义的_metaspace属性保存，以便进行管理。
*/
// 元空间
class Metaspace : public CHeapObj<mtClass> {
  friend class VMStructs;
  friend class SpaceManager;
  friend class VM_CollectForMetadataAllocation;
  friend class MetaspaceGC;
  friend class MetaspaceAux;

 public:
  enum MetadataType {
    ClassType,
    NonClassType,
    MetadataTypeCount
  };
  enum MetaspaceType {
    StandardMetaspaceType,
    BootMetaspaceType,
    ROMetaspaceType,
    ReadWriteMetaspaceType,
    AnonymousMetaspaceType,
    ReflectionMetaspaceType
  };

 private:
  static void verify_global_initialization();

  void initialize(Mutex* lock, MetaspaceType type);

  // Initialize the first chunk for a Metaspace.  Used for
  // special cases such as the boot class loader, reflection
  // class loader and anonymous class loader.
  void initialize_first_chunk(MetaspaceType type, MetadataType mdtype);
  Metachunk* get_initialization_chunk(MetaspaceType type, MetadataType mdtype);

  // Align up the word size to the allocation word size
  static size_t align_word_size_up(size_t);

  // Aligned size of the metaspace.
  static size_t _compressed_class_space_size;

  static size_t compressed_class_space_size() {
    return _compressed_class_space_size;
  }

  static void set_compressed_class_space_size(size_t size) {
    _compressed_class_space_size = size;
  }

  static size_t _first_chunk_word_size;
  static size_t _first_class_chunk_word_size;

  static size_t _commit_alignment;
  static size_t _reserve_alignment;

  SpaceManager* _vsm;
  SpaceManager* vsm() const { return _vsm; }

  SpaceManager* _class_vsm;
  SpaceManager* class_vsm() const { return _class_vsm; }
  SpaceManager* get_space_manager(MetadataType mdtype) {
    assert(mdtype != MetadataTypeCount, "MetadaTypeCount can't be used as mdtype");
    return mdtype == ClassType ? class_vsm() : vsm();
  }

  // Allocate space for metadata of type mdtype. This is space
  // within a Metachunk and is used by
  //   allocate(ClassLoaderData*, size_t, bool, MetadataType, TRAPS)
  MetaWord* allocate(size_t word_size, MetadataType mdtype);

  // Virtual Space lists for both classes and other metadata
  static VirtualSpaceList* _space_list;
  static VirtualSpaceList* _class_space_list;

  static ChunkManager* _chunk_manager_metadata;
  static ChunkManager* _chunk_manager_class;

  static const MetaspaceTracer* _tracer;

 public:
  static VirtualSpaceList* space_list()       { return _space_list; }
  static VirtualSpaceList* class_space_list() { return _class_space_list; }
  static VirtualSpaceList* get_space_list(MetadataType mdtype) {
    assert(mdtype != MetadataTypeCount, "MetadaTypeCount can't be used as mdtype");
    return mdtype == ClassType ? class_space_list() : space_list();
  }

  static ChunkManager* chunk_manager_metadata() { return _chunk_manager_metadata; }
  static ChunkManager* chunk_manager_class()    { return _chunk_manager_class; }
  static ChunkManager* get_chunk_manager(MetadataType mdtype) {
    assert(mdtype != MetadataTypeCount, "MetadaTypeCount can't be used as mdtype");
    return mdtype == ClassType ? chunk_manager_class() : chunk_manager_metadata();
  }

  static const MetaspaceTracer* tracer() { return _tracer; }

 private:
  // These 2 methods are used by DumpSharedSpaces only, where only _vsm is used. So we will
  // maintain a single list for now.
  void record_allocation(void* ptr, MetaspaceObj::Type type, size_t word_size);
  void record_deallocation(void* ptr, size_t word_size);

#ifdef _LP64
  static void set_narrow_klass_base_and_shift(address metaspace_base, address cds_base);

  // Returns true if can use CDS with metaspace allocated as specified address.
  static bool can_use_cds_with_metaspace_addr(char* metaspace_base, address cds_base);

  static void allocate_metaspace_compressed_klass_ptrs(char* requested_addr, address cds_base);

  static void initialize_class_space(ReservedSpace rs);
#endif

  class AllocRecord : public CHeapObj<mtClass> {
  public:
    AllocRecord(address ptr, MetaspaceObj::Type type, int byte_size)
      : _next(NULL), _ptr(ptr), _type(type), _byte_size(byte_size) {}
    AllocRecord *_next;
    address _ptr;
    MetaspaceObj::Type _type;
    int _byte_size;
  };

  AllocRecord * _alloc_record_head;
  AllocRecord * _alloc_record_tail;

  size_t class_chunk_size(size_t word_size);

 public:

  Metaspace(Mutex* lock, MetaspaceType type);
  ~Metaspace();

  static void ergo_initialize();
  static void global_initialize();
  static void post_initialize();

  static size_t first_chunk_word_size() { return _first_chunk_word_size; }
  static size_t first_class_chunk_word_size() { return _first_class_chunk_word_size; }

  static size_t reserve_alignment()       { return _reserve_alignment; }
  static size_t reserve_alignment_words() { return _reserve_alignment / BytesPerWord; }
  static size_t commit_alignment()        { return _commit_alignment; }
  static size_t commit_alignment_words()  { return _commit_alignment / BytesPerWord; }

  char*  bottom() const;
  size_t used_words_slow(MetadataType mdtype) const;
  size_t free_words_slow(MetadataType mdtype) const;
  size_t capacity_words_slow(MetadataType mdtype) const;

  size_t used_bytes_slow(MetadataType mdtype) const;
  size_t capacity_bytes_slow(MetadataType mdtype) const;

  size_t allocated_blocks_bytes() const;
  size_t allocated_chunks_bytes() const;

  static MetaWord* allocate(ClassLoaderData* loader_data, size_t word_size,
                            bool read_only, MetaspaceObj::Type type, TRAPS);
  void deallocate(MetaWord* ptr, size_t byte_size, bool is_class);

  MetaWord* expand_and_allocate(size_t size,
                                MetadataType mdtype);

  static bool contains(const void* ptr);

  void dump(outputStream* const out) const;

  // Free empty virtualspaces
  static void purge(MetadataType mdtype);
  static void purge();

  static void report_metadata_oome(ClassLoaderData* loader_data, size_t word_size,
                                   MetaspaceObj::Type type, MetadataType mdtype, TRAPS);

  static const char* metadata_type_name(Metaspace::MetadataType mdtype);

  void print_on(outputStream* st) const;
  // Debugging support
  void verify();

  static void print_compressed_class_space(outputStream* st, const char* requested_addr = 0) NOT_LP64({});

  class AllocRecordClosure :  public StackObj {
  public:
    virtual void doit(address ptr, MetaspaceObj::Type type, int byte_size) = 0;
  };

  void iterate(AllocRecordClosure *closure);

  // Return TRUE only if UseCompressedClassPointers is True and DumpSharedSpaces is False.
  static bool using_class_space() {
    return NOT_LP64(false) LP64_ONLY(UseCompressedClassPointers && !DumpSharedSpaces);
  }

  static bool is_class_space_allocation(MetadataType mdType) {
    return mdType == ClassType && using_class_space();
  }

};

class MetaspaceAux : AllStatic {
  static size_t free_chunks_total_words(Metaspace::MetadataType mdtype);

  // These methods iterate over the classloader data graph
  // for the given Metaspace type.  These are slow.
  static size_t used_bytes_slow(Metaspace::MetadataType mdtype);
  static size_t free_bytes_slow(Metaspace::MetadataType mdtype);
  static size_t capacity_bytes_slow(Metaspace::MetadataType mdtype);
  static size_t capacity_bytes_slow();

  // Running sum of space in all Metachunks that has been
  // allocated to a Metaspace.  This is used instead of
  // iterating over all the classloaders. One for each
  // type of Metadata
  static size_t _capacity_words[Metaspace:: MetadataTypeCount];
  // Running sum of space in all Metachunks that
  // are being used for metadata. One for each
  // type of Metadata.
  static size_t _used_words[Metaspace:: MetadataTypeCount];

 public:
  // Decrement and increment _allocated_capacity_words
  static void dec_capacity(Metaspace::MetadataType type, size_t words);
  static void inc_capacity(Metaspace::MetadataType type, size_t words);

  // Decrement and increment _allocated_used_words
  static void dec_used(Metaspace::MetadataType type, size_t words);
  static void inc_used(Metaspace::MetadataType type, size_t words);

  // Total of space allocated to metadata in all Metaspaces.
  // This sums the space used in each Metachunk by
  // iterating over the classloader data graph
  static size_t used_bytes_slow() {
    return used_bytes_slow(Metaspace::ClassType) +
           used_bytes_slow(Metaspace::NonClassType);
  }

  // Used by MetaspaceCounters
  static size_t free_chunks_total_words();
  static size_t free_chunks_total_bytes();
  static size_t free_chunks_total_bytes(Metaspace::MetadataType mdtype);

  static size_t capacity_words(Metaspace::MetadataType mdtype) {
    return _capacity_words[mdtype];
  }
  static size_t capacity_words() {
    return capacity_words(Metaspace::NonClassType) +
           capacity_words(Metaspace::ClassType);
  }
  static size_t capacity_bytes(Metaspace::MetadataType mdtype) {
    return capacity_words(mdtype) * BytesPerWord;
  }
  static size_t capacity_bytes() {
    return capacity_words() * BytesPerWord;
  }

  static size_t used_words(Metaspace::MetadataType mdtype) {
    return _used_words[mdtype];
  }
  static size_t used_words() {
    return used_words(Metaspace::NonClassType) +
           used_words(Metaspace::ClassType);
  }
  static size_t used_bytes(Metaspace::MetadataType mdtype) {
    return used_words(mdtype) * BytesPerWord;
  }
  static size_t used_bytes() {
    return used_words() * BytesPerWord;
  }

  static size_t free_bytes();
  static size_t free_bytes(Metaspace::MetadataType mdtype);

  static size_t reserved_bytes(Metaspace::MetadataType mdtype);
  static size_t reserved_bytes() {
    return reserved_bytes(Metaspace::ClassType) +
           reserved_bytes(Metaspace::NonClassType);
  }

  static size_t committed_bytes(Metaspace::MetadataType mdtype);
  static size_t committed_bytes() {
    return committed_bytes(Metaspace::ClassType) +
           committed_bytes(Metaspace::NonClassType);
  }

  static size_t min_chunk_size_words();
  static size_t min_chunk_size_bytes() {
    return min_chunk_size_words() * BytesPerWord;
  }

  static bool has_chunk_free_list(Metaspace::MetadataType mdtype);
  static MetaspaceChunkFreeListSummary chunk_free_list_summary(Metaspace::MetadataType mdtype);

  // Print change in used metadata.
  static void print_metaspace_change(size_t prev_metadata_used);
  static void print_on(outputStream * out);
  static void print_on(outputStream * out, Metaspace::MetadataType mdtype);

  static void print_class_waste(outputStream* out);
  static void print_waste(outputStream* out);
  static void dump(outputStream* out);
  static void verify_free_chunks();
  // Checks that the values returned by allocated_capacity_bytes() and
  // capacity_bytes_slow() are the same.
  static void verify_capacity();
  static void verify_used();
  static void verify_metrics();
};

// Metaspace are deallocated when their class loader are GC'ed.
// This class implements a policy for inducing GC's to recover
// Metaspaces.

class MetaspaceGC : AllStatic {

  // The current high-water-mark for inducing a GC.
  // When committed memory of all metaspaces reaches this value,
  // a GC is induced and the value is increased. Size is in bytes.
  static volatile intptr_t _capacity_until_GC;

  // For a CMS collection, signal that a concurrent collection should
  // be started.
  static bool _should_concurrent_collect;

  static uint _shrink_factor;

  static size_t shrink_factor() { return _shrink_factor; }
  void set_shrink_factor(uint v) { _shrink_factor = v; }

 public:

  static void initialize();
  static void post_initialize();

  static size_t capacity_until_GC();
  static bool inc_capacity_until_GC(size_t v,
                                    size_t* new_cap_until_GC = NULL,
                                    size_t* old_cap_until_GC = NULL,
                                    bool* can_retry = NULL);
  static size_t dec_capacity_until_GC(size_t v);

  static bool should_concurrent_collect() { return _should_concurrent_collect; }
  static void set_should_concurrent_collect(bool v) {
    _should_concurrent_collect = v;
  }

  // The amount to increase the high-water-mark (_capacity_until_GC)
  static size_t delta_capacity_until_GC(size_t bytes);

  // Tells if we have can expand metaspace without hitting set limits.
  static bool can_expand(size_t words, bool is_class);

  // Returns amount that we can expand without hitting a GC,
  // measured in words.
  static size_t allowed_expansion();

  // Calculate the new high-water mark at which to induce
  // a GC.
  static void compute_new_size();
};

#endif // SHARE_VM_MEMORY_METASPACE_HPP
