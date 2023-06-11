[toc]

Java对象用oop来表示，在Java创建对象的时候创建。也就是说，在Java应用程序运行过程中每创建一个Java对象，在HotSpotVM内部都会创建一个oop实例来表示Java对象。

oopDesc类的继承关系如图2-9所示。

![image](https://cdn.staticaly.com/gh/YangLuchao/img_host@master/20230521/image.4gikndzxjr40.jpg)

> `markOopDesc`对应java对象头对象
>
> `instanceOopDesc`对应Java普通对象
>
> `objArrayOopDesc`与`typeArrayOopDesc`对应Java数组对象

`markOopDesc`不是指Java对象，而是指Java对象的头信息，因此表示普通Java类对象的`instanceOopDesc`实例和表示数组对象的`objArrayOopDesc`与`typeArrayOopDesc`实例都含有`markOopDesc`实例。

# oopDesc类

==oopDesc类的别名为oop,因此HotSpot VM中一般使用oop表示oopDesc类型==。oopDesc是所有类名格式为xxxOopDesc类的基类，这些类的实例表示Java对象，因此类名格式为xxxOopDesc的类中会声明一些保存Java对象信息的字段，这样就可以直接被C++获取。类及重要属性的定义如下：

```c++
// 来源： hotspot/src/share/vm/oops/oop.hpp
class oopDesc {
 private:
  volatile markOop  _mark;
  union _metadata {
    Klass*      _klass;
    narrowKlass _compressed_klass;
  } _metadata;
  ...
}
```

==Java对象内存布局主要分为`header`(头部)和`fields`(实例字段)====`header`由`_mark`和`_metadata`组成==。==`_mark`字段保存了Java对象的一些信息，如GC分代年龄、锁状态等==；==`_metadata`使用联合体（union)来声明这样是为了在64位平台能对指针进行压缩==。==从32位平台到64位平台，指针由4字节变为了8字节，因此通常64位的HotSpotVM消耗的内存比32位的大，造成堆内存损失。不过从JDK 1.6 update14开始，64位的HotSpot VM正式支持-XX:+UseCompressedOops命令（默认开启）==。该命令可以压缩指针，起到节约内存占用的作用。

注：这里所说的64位平台、32位平台指的是一个体系，包括软件和硬件，也包括HotSpot VM。

==在64位平台上，存放`_metadata`的空间是8字节，`_mark`是8字节，对象头为16字节。在64位开启指针压缩的情况下，存放`_metadata`的空间是4字节，`_mark`是8字节，对象头为12字节。==

> 压缩指针

==64位地址分为堆的基地址+偏移量，当堆内存小于32GB时，在压缩过程中会把偏移量除以8后的结果保存到32位地址中，解压时再把32位地址放大8倍==，因此启用-XX:+UseCompressedOops命令的条件是堆内存要在4GB×8=32GB以内。具体实现方式是在机器码中植入压缩与解压指令，但这样可能会给HotSpot VM增加额外的开销。

总结一下：

- 如果GC堆内存在4GB以下，直接忽略高32位，以避免编码、解码过程；
- 如果GC堆内存在4GB以上32GB以下，则启用-XX:+UseCompressedOops命令；
- 如果GC堆内存大于32GB,压缩指针的命令失效，使用原来的64位HotSpot VM。

另外，OpenJDK8使用元空间存储元数据，在-XX:+UseCompressedOops命令之外，额外增加了一个新的命令-XX:+UseCompressedClassPointer。这个命令打开后，类元信息中的指针也用32位的Compressed版本。而这些指针指向的空间被称作Compressed Class Space,默认是1GB,可以通过-XX:CompressedClassSpaceSize命令进行调整。==联合体中定义的`_klass`或`_compressed_klass`指针指向的是Klass实例，这个Klass实例保存了Java对象的实际类型，也就是Java对象所对应的Java类。==调用header_size()函数获取header占用的内存空间，具体实现代码如下：

```cpp
// 来源：hotspot/src/share/vm/oops/oop.hpp 
static int header_size()          { 
    return sizeof(oopDesc)/HeapWordSize; 
}
```

计算占用的字的大小，==对于64位平台来说，一个字的大小为8字节，因此HeapWordSize的值为8==。

Java对象的header信息可以存储在oopDesc类中定义的`_mark`和`_metadata`属性中，而Java对象的fields没有在oopDesc类中定义相应的属性来存储，因此只能申请一定的内存空间，然后按一定的布局规则进行存储。==对象字段存放在紧跟着oopDesc实例本身占用的内存空间之后，在获取时只能通过偏移来取值==。==oopDesc类的`field_base()`函数用于获取字段地址==，实现代码如下：

```cpp
// 来源：hotspot/src/share/vm/oops/oop.inline.hpp
inline void* oopDesc::field_base(int offset) const { 
    // offset是偏移量，可以通过相对于当前实例this的内存首地址的偏移量来存取字段的值
    return (void*)&((char*)this)[offset]; 
}
```

其中，offset是偏移量，可以通过相对于当前实例this的内存首地址的偏移量来存取字段的值。

# markOopDesc类

markOopDesc类的实例可以表示Java对象的头信息Mark Word,包含的信息有哈希码、GC分代年龄、偏向锁标记、线程持有的锁、偏向线程ID和偏向时间戳等。==markOopDesc类的实例并不能表示一个具体的Java对象，而是通过一个字的各个位来表示Java对象的头信息。==对于32位平台来说，一个字为32位，对于64位平台来说，一个字为64位。由于目前64位是主流，所以不再对32位的结构进行说明。图2-10所示为Java对象在不同锁状态下的MarkWord各个位区间的含义。

![image](https://cdn.staticaly.com/gh/YangLuchao/img_host@master/20230521/image.30azsimh0l60.jpg)

其中，各部分的说明如下：

- `lock`：2位的锁状态标记位。为了用尽可能少的二进制位表示尽可能多的信息，因此设置了lock标记。该标记的值不同，整个Mark Word表示的含义就不同。==biased_lock和lock表示锁的状态==。
- `biased_lock`:对象是否启用偏向锁标记，只占一个二进制位。==值为1时表示对象启用偏向锁，值为0时表示对象没有偏向锁==。lock和biased_lock共同表示对象的锁状态。
- `age`:占用4个二进制位，存储的是Java对象的年龄。在GC中，如果对象在Survivor区复制一次，则年龄增加1。当对象的年龄达到设定的阈值时，将会晋升到老年代。==默认情况下，并行GC的年龄闯值为15,并发GC的年龄阈值为6。由于age只有4位，所以最大值为15,这就是-XX:MaxTenuringThreshold选项最大值为15的原因。==
- `identity_hashcode`:占用31个二进制位，用来存储对象的HashCode，采用延时加载技术。调用System.identityHashCode()方法计算HashCode并将结果写入该对象头中。如果当前对象的锁状态为偏向锁，而偏向锁没有存储HashCode的地方，因此调用identityHashCode()方法会造成锁升级，而轻量级锁和重量级锁所指向的lock record或monitor都有存储HashCode的空间。HashCode只针对identity hash code。用户自定义的hashCode()方法所返回的值不存储在Mark Word中。==identity hash code是未被覆写的java.lang.Object.hashCode()或者java.lang.System.identityHashCode()方法返回的值==。
- `thread`:持有偏向锁的线程ID。
- `epoch`:偏向锁的时间戳。
- `ptr_to_lock_record`:轻量级锁状态下，指向栈中锁记录的指针。
- `ptr_to_heavyweight_monitor`:重量级锁状态下，指向对象监视器Monitor的指针。

# instanceOopDesc类

==instanceOopDesc类的实例表示除数组对象外的其他对象==。在HotSpot虚拟机中，对象在内存中存储的布局可以分为如图2-11所示的三个区域：==对象头（header)、对象字段数据（field data)和对齐填充（padding)==。

![image](https://cdn.staticaly.com/gh/YangLuchao/img_host@master/20230521/image.58bgkfc2f1c0.jpg)

下面详细介绍这三个组成部分。

### 1.对象头

对象头分为两部分，一部分是Mark Word,另一部分是存储指向元数据区对象类型数据的指针`_klass`或`_compressed_klass`。它们两个在介绍oopDesc类时详细讲过，这里不再奏述。

### 2.对象字段数据

Java对象中的字段数据存储了Java源代码中定义的各种类型的字段内容，具体包括父类继承及子类定义的字段。

存储顺序受HotSpot VM布局策略命令-XX:FieldsAllocationStyle和字段在Java源代码中定义的顺序的影响，默认布局策略的顺序为long/double、int、short/char、boolean、==oop(对象指针，32位系统占用4字节，64位系统占用8字节）==,相同宽度的字段总被分配到一起。

如果虚拟机的-XX:+CompactFields参数为true,则子类中较窄的变量可能插入空隙中，以节省使用的内存空间。例如，当布局long/double类型的字段时，由于对齐的原因，可能会在header和long/double字段之间形成空隙，如64位系统开启压缩指针，header占12字节，剩下的4字节就是空隙，这时就可以将一些短类型插入long/double和header之间的空隙中。

### 3.对齐填充

对齐填充不是必需的，只起到占位符的作用，没有其他含义。HotSpotVM要求对象所占的内存必须是8字节的整数倍，对象头刚好是8字节的整数倍，因此填充是对实例数据没有对齐的情况而言的。==对象所占的内存如果是以8字节对齐，那么对象在内存中进行线性分配时，对象头的地址就是以8字节对齐的，这时候就为对象指针压缩提供了条件，可以将地址缩小8倍进行存储。==

在创建instanceOop实例时会调用`allocate_instance()`函数，实现代码如下：

```cpp
// 来源：hotspot/src/share/vm/oops/instanceKlass.cpp
instanceOop InstanceKlass::allocate_instance(TRAPS) {
    // 获取创建instanceOop实例所需要的内存空间
    int size = size_helper();  
    // 分配size的内存
	KlassHandle h_k(THREAD, this);
	instanceOop i;
	i = (instanceOop)CollectedHeap::obj_allocate(h_k, size, CHECK_NULL);
  return i;
}
```

上面的代码中调用了instanceKlass类中的`size_helper()`函数获取创建instanceOop实例所需要的内存空间，调用了`CollectedHeap:obj_allocate()`函数分配size的内存。size_helper()函数在前面介绍过，用于从_layout_helper属性中获取Java对象所需要的内存空间大小。代码如下：

```cpp
  // 来源： hotspot/src/share/vm/oops/klass.hpp
  static int layout_helper_to_size_helper(jint lh) {
    return lh >> LogHeapWordSize;
  }
```

==当调用者为InstanceKlass时，可以通过_layout_helper属性获取instanceOop实例的大小==，在设置时调用的是`instance_layout_helper()`函数，代码如下：

```cpp
  // 来源： hotspot/src/share/vm/oops/klass.hpp
  static jint instance_layout_helper(jint size, bool slow_path_flag) {
      // 保存时，size值左移3位，低3位用来保存其他信息
    return (size << LogHeapWordSize)
      |    (slow_path_flag ? _lh_instance_slow_path_bit : 0);
  }
```

`instance_layout_helper()`函数会将size的值左移3位，因此获取size时需要向右移动3位。调用`parseClassFile()`函数计算实例的大小，然后调用`instance_layout_helper()`函数将其保存在`_layout_helper`属性中。

获取size的值后，调用CollectedHeap:obj_allocate()函数分配size的内存并将内存初始化为零值，相关知识将在第9章中详细介绍。

# arrayOopDesc类

arrayOopDesc类的实例表示Java数组对象。具体的基本类型数组或对象类型数组由具体的C++中定义的子类实例表示。在HotSpot VM中，数组对象在内存中的布局可以分为如图2-12所示的三个区域：对象头（header)、对象字段数据（field data)和对齐填充(padding)。

![image](https://cdn.staticaly.com/gh/YangLuchao/img_host@master/20230521/image.1e8b378tlwsg.jpg)

与Java对象内存布局唯一不同的是，数组对象的对象头中还会存储数组的长度`length`,它占用的内存空间为4字节。在64位系统下，存放`_metadata`的空间是8字节，`_mark`是8字节，`length`是4字节，对象头为20字节，由于要按8字节对齐，所以会填充4字节，最终占用24字节。64位开启指针压缩的情况下，存放`_metadata`的空间是4字节，_mark是8字节，`length`是4字节，对象头为16字节。

# arrayOopDesc类的子类

==arrayOopDesc类的子类有两个，分别是表示组件类型为基本类型的typeArrayOopDesc和表示组件类型为对象类型的objArrayOopDesc==。==二维及二维以上的数组都用objArrayOopDesc的实例来表示。当需要创建typeArrayOopDesc实例时，通常会调用oopFactory类中定义的工厂方法。==例如，调用new_boolArray()创建一个boolean数组，代码如下：

```c++
// 来源： hotspot/src/share/vm/memory/oopFactory.hpp

  static typeArrayOop    new_boolArray  (int length, TRAPS) { return TypeArrayKlass::cast(Universe::boolArrayKlassObj  ())->allocate(length, THREAD); }
  static typeArrayOop    new_charArray  (int length, TRAPS) { return TypeArrayKlass::cast(Universe::charArrayKlassObj  ())->allocate(length, THREAD); }
  static typeArrayOop    new_singleArray(int length, TRAPS) { return TypeArrayKlass::cast(Universe::singleArrayKlassObj())->allocate(length, THREAD); }
  static typeArrayOop    new_doubleArray(int length, TRAPS) { return TypeArrayKlass::cast(Universe::doubleArrayKlassObj())->allocate(length, THREAD); }
  static typeArrayOop    new_byteArray  (int length, TRAPS) { return TypeArrayKlass::cast(Universe::byteArrayKlassObj  ())->allocate(length, THREAD); }
  static typeArrayOop    new_shortArray (int length, TRAPS) { return TypeArrayKlass::cast(Universe::shortArrayKlassObj ())->allocate(length, THREAD); }
  static typeArrayOop    new_intArray   (int length, TRAPS) { return TypeArrayKlass::cast(Universe::intArrayKlassObj   ())->allocate(length, THREAD); }
  static typeArrayOop    new_longArray  (int length, TRAPS) { return TypeArrayKlass::cast(Universe::longArrayKlassObj  ())->allocate(length, THREAD); }

```

代码中调用`Universe::boolArrayKlassObj()`函数获取`_charArrayKlassObj`属性的值。`_charArrayKlassObj`属性保存的是表示`boolean`数组的TypeArrayKlass实例，该实例是通过调用`TypeArrayKlass::create_klass()`函数创建的，在2.1.5节中介绍过。然后调用`TypeArrayKlass`类中的`allocate()`函数创建`typeArrayOop`实例，代码如下：

```cpp
// 来源：hotspot/src/share/vm/oops/typeArrayKlass.hpp
typeArrayOop allocate(int length, TRAPS) { 
	return allocate_common(length, true, THREAD); 
}


/*
`length`表示创建数组的大小
`do_zero`表示是否需要在分配数组内存时将内存初始化为零值
*/
typeArrayOop TypeArrayKlass::allocate_common(int length, bool do_zero, TRAPS) {
 
  if (length >= 0) {
    if (length <= max_length()) {
      // `_layout_helper`中获取数组的大小
      size_t size = typeArrayOopDesc::object_size(layout_helper(), length);
      KlassHandle h_k(THREAD, this);
      typeArrayOop t;
      CollectedHeap* ch = Universe::heap();
      // 调用`array_allocate()`或`array_allocate_nozero()`函数分配内存并初始化对象头
      if (do_zero) {
        t = (typeArrayOop)CollectedHeap::array_allocate(h_k, (int)size, length, CHECK_NULL);
      } else {
        t = (typeArrayOop)CollectedHeap::array_allocate_nozero(h_k, (int)size, length, CHECK_NULL);
      }
      return t;
    } else {
      // 异常抛出
    }
  } else {
    // 异常抛出
  }
}
```

参数`length`表示创建数组的大小，而`do_zero`表示是否需要在分配数组内存时将内存初始化为零值。首先调用`typeArrayOopDesc:object_size()`函数从`_layout_helper`中获取数组的大小，然后调用`array_allocate()`或`array_allocate_nozero()`函数分配内存并初始化对象头，即为`length`、`_mark`和`_metadat`a属性赋值。`typeArrayOopDesc::object_size()`函数的实现代码如下：

```cpp
// 来源：hotspot/src/share/vm/oops/typeArrayOop.hpp
// 从`_layout_helper`中获取数组的大小
static int object_size(int lh, int length) {
    int instance_header_size = Klass::layout_helper_header_size(lh);
    int element_shift = Klass::layout_helper_log2_element_size(lh);
    DEBUG_ONLY(BasicType etype = Klass::layout_helper_element_type(lh));

    julong size_in_bytes = (juint)length;
    size_in_bytes <<= element_shift;
    size_in_bytes += instance_header_size;
    julong size_in_words = ((size_in_bytes + (HeapWordSize-1)) >> LogHeapWordSize);

    return align_object_size((intptr_t)size_in_words);
}
```

`ArrayKlass`实例的`_layout_helper`属性是组合数字，可以通过调用对应的函数从该属性中获取数组头需要占用的字节数及组件类型需要占用的字节数，如果组件类型为`boolean`类型，则值为1。最终`arrayOopDesc`实例占用的内存空间是通过如下公式计算出来的：

$size = instance\_header\_size + length<<element\_shift + 对齐填充$

也就是对象头加上实例数据，然后再加上对齐填充。在`TypeArrayKlass::allocate_common()`函数中获取`TypeArrayOopDesc`实例需要分配的内存空间后，会调用`CollectedHeap::array_allocate()`或`CollectedHeap::array_allocate_nozero()`函数在堆上分配内存空间，然后初始化对象头信息。`CollectedHeap::array_allocate()`和`CollectedHeap:array_allocate_nozero()`两个函数的实现类似，我们只看`CollectedHeap::array_allocate()`函数的实现即可，代码如下：

```cPP
// 来源：hotspot/src/share/vm/gc_interface/collectedHeap.inline.hpp
oop CollectedHeap::array_allocate(KlassHandle klass,
                                  int size,
                                  int length,
                                  TRAPS) {
  HeapWord* obj = common_mem_allocate_init(klass, size, CHECK_NULL);
  post_allocation_setup_array(klass, obj, length);
  return (oop)obj;
}
```

调用`common_mem_allocate_init()`函数在堆上分配指定size大小的内存。关于在堆上分配内存的知识将在第9章中详细介绍，这里不做介绍。调用`post_allocation_setup_array()`函数初始化对象头，代码如下：

```c++
// 来源：hotspot/src/share/vm/gc_interface/collectedHeap.inline.hpp
void CollectedHeap::post_allocation_setup_array(KlassHandle klass,
                                                HeapWord* obj_ptr,
                                                int length) {
  ((arrayOop)obj_ptr)->set_length(length);
  post_allocation_setup_common(klass, obj_ptr);
  post_allocation_notify(klass, new_obj, new_obj->size());
}

void CollectedHeap::post_allocation_setup_common(KlassHandle klass,
                                                 HeapWord* obj_ptr) {
  post_allocation_setup_no_klass_install(klass, obj_ptr);
}

void CollectedHeap::post_allocation_setup_no_klass_install(KlassHandle klass,
                                                           HeapWord* obj_ptr) {
  oop obj = (oop)obj_ptr;
  // 在允许使用偏向锁的情况下，获取Klass中的_prototype_header属性值，
  // 其中的锁状态一般为偏向锁状态，而markOopDesc::prototype()函数初始化的对象头，
  // 其锁状态一般为无锁状态Klass中的_prototype_header完全是为了支持偏向锁增加的属性，
  // 后面章节中会详细介绍偏向锁的实现机制
  if (UseBiasedLocking && (klass() != NULL)) {
    obj->set_mark(klass->prototype_header());
  } else {
    // May be bootstrapping
    obj->set_mark(markOopDesc::prototype());
  }
}
```

以上代码中调用的函数比较多，但是实现非常简单，这里不做过多介绍。`objArrayOop`的创建与`typeArrayOop`的创建非常类似，即先调用oopFactory类中的工厂方法`new_objectArray()`,然后调用`ObjArrayKlass::allocate()`函数分配内存，这里不再介绍。
