[toc]

==HotSpot采用oop-Klass模型表示Java的对象和类==。==oop (ordinary object pointer)指普通的对象指针，Klass表示对象的具体类型==。==为何要设计一个一分为二的对象模型呢？这是因为HotSpot的设计者不想让每个对象中都含有一个vtable(虚函数表）,所以就把对象模型拆成Klass和oop。其中，oop中不含有任何虚函数，自然就没有虚函数表，而Klass中含有虚函数表，可以进行方法的分发==

# Klass类

Java类通过Klass来表示。简单来说Klass就是Java类在HotSpot中的C++对等体，主要用于描述Java对象的具体类型。一般而言，HotSpot VM在加载Class文件时会在元数据区创建Klass,表示类的元数据，通过Klass可以获取类的常量池、字段和方法等信息。

![image](https://github.com/YangLuchao/img_host/raw/master/20230511/image.2ifj5f13uym0.webp)

Metadata是元数据类的基类，除了Klass类会直接继承Metadata基类以外，表示方法的Method类与表示常量池的ConstantPool类也会直接继承Metadata基类。

本节只讨论Klass继承体系中涉及的相关类。Klass继承体系中涉及的C++类主要提供了两个功能：

- 提供C++层面的Java类型（包括Java类和Java数组）表示方式，也就是用C++类的对象来描述Java类型；
- 方法分派。

==一个C++的Klass类实例表示一个Java类的元数据（相当于java.lang.Class对象）==,主要提供两个功能：

- 实现Java语言层面的类；
- 提供对多态方法的支持，即通过vtbl指针实现多态。

在HotSpot中，==Java对象使用oop实例来表示，不提供任何虚函数的功能==。oop实例保存了对应Klass的指针，通过Klass完成所有的方法调用并获取类型信息，==Klass基于C++的虚函数提供对Java多态的支持==。

笔者在==讲Java类的对象时使用的是“对象”这个词，而在讲C++类的对象时使用的是“实例”这个词==。同样，在==讲Java的方法时使用的是“方法”一词，而在讲C++的方法时使用的是“函数”一词==。Klass类及其重要属性的定义如下：

```c

class Klass : public Metadata {
  friend class VMStructs;
 protected:
  enum { _primary_super_limit = 8 };

  /*
	对象布局的综合描述符
	如果不是InstanceKlass或ArrayKlass,值为0,
	如果是InstanceKlass或ArrayKlass,这个值是一个组合数字。
	（1)对于InstanceKlass而言，组合数字中包含表示对象的以字节为单位的内存占用量。由于InstanceKlass实例能够表示Java类，因此这里指的内存占用量是指这个Java类创建的对象所需要的内存。	（2)对于ArrayKlass而言，组合数字中包含tag、hsize、etype和esize四部分，具体怎么组合和解析由子类实现
  */
  jint        _layout_helper;

  /*
  快速查找supertype的一个偏移量，这个偏移量是相对于Klass实例起始地址的偏移量。
  如果当前类是IOException,那么这个属性就指向_primarysupers数组中存储IOException的位置。
  当存储的类多于8个时，值与secondary_super_cache相等
  */
  juint       _super_check_offset;

  // 类名，如java/lang/String和[Ljava/lang/String;等
  Symbol*     _name;

  // Klass指针，保存上一次查询父类的结果
  Klass*      _secondary_super_cache;

  // klass指针数组，一般存储Java类实现的接口，偶尔还会存储Java类及其父类
  Array<Klass*>* _secondary_supers;

  /*
  primary_supers代表这个类的父类，其类型是一个Klass指针数组，大小固定为8。
  例如，IOException是Exception的子类，而Exception又是Throwab的子类，因此表示IOException类的_primary_supers属性值为[Throwable,Exception,IOException]。
  如果继承链过长，即当前类加上继承的类多于8个（默认值，可通过命令更改）时，会将多出来的类存储到_secondary_supers数组中
  */
  Klass*      _primary_supers[_primary_super_limit];

  // oopDesc*类型，保存的是当前Klass实例表示的Java类所对应的java.lang.Class对象，可以据此访问类的静态属性
  oop       _java_mirror;

  // Klass指针，指向Java类的直接父类
  Klass*      _super;
  // Klass指针，指向Java类的直接子类
  Klass*      _subklass;
  
  // 由于直接子类可能有多个，因此多个子类会通过_next_sibling连接起来
  Klass*      _next_sibling;
 
  // Klass指针，ClassLoader加载的下一个Klass
  Klass*      _next_link;

  ClassLoaderData* _class_loader_data;

  jint        _modifier_flags;  
  
  // 保存Java类的修饰符，如private、final、static、abstract和native等
  AccessFlags _access_flags;    


  jlong    _last_biased_lock_bulk_revocation_time;
    
  // 在锁实现过程zhong
  markOop  _prototype_header;   
    
  ...
}
```

通过Klass类中的相关属性保存Java类型定义的一些元数据信息，如\_name保存Java类型的名称，\_super保存Java类型的父类等。==Klass类是Klass继承体系中定义的C++类的基类，因此该类实例会保存Java类型的一些共有信息。==

## 1.\_layout_helper属性

==\_layout_helper是一个组合属性==。如果当前的Klass实例表示一个Java数组类型，则这个属性的值比较复杂。通常会调用如下函数生成值：

```c
// 来源：hotspot/src/share/vm/oops/klass.cpp
jint Klass::array_layout_helper(BasicType etype) {
  
  int  hsize = arrayOopDesc::base_offset_in_bytes(etype);
  //Java基本类型元素需要占用的字节数
  int  esize = type2aelembytes(etype);
  bool isobj = (etype == T_OBJECT);
  int  tag   =  isobj ? _lh_array_tag_obj_value : _lh_array_tag_type_value;
  int lh = array_layout_helper(tag, hsize, etype, exact_log2(esize));

  return lh;
}
```

表示Java数组类型的\_layout_helper属性由四部分组成，下面分别介绍

### tag

> tag判断数组类型是对象还是基本类型

==如果数组元素的类型为对象类型，则值为0×80,否则值为0xC0,表示数组元素的类型为Java基本类型==。其中用到下面两个枚举常量：

```c
// 来源：hotspot/src/share/vm/oops/klass.hpp
enum
  {
    ...
    // _Ih_array_tag_obj_value的二进制表达式为最高位31个1:11111111111111111111111110,其实就是0×80000000>>30。
    _lh_array_tag_obj_value = ~0x01
  };
  // _lh_array_tag_type_value的二进制表达式为32个1:11111111111111111111111111,其实就是0xC0000000>>30
  static const unsigned int _lh_array_tag_type_value = 0Xffffffff;
```



### hsize

> 获取头结点所需字节数

==hsize表示数组头元素的字节数==，调用==arrayOopDesc::base_offset_in_bytes()及相关函数可以获取hsize的值==。代码如下：

```c
// 来源： hotspot/src/share/vm/oops/arrayOop.hpp
static int base_offset_in_bytes(BasicType type) {
    return header_size(type) * HeapWordSize;
}

  static int header_size(BasicType type) {
    size_t typesize_in_bytes = header_size_in_bytes();
    return (int)(Universe::element_type_should_be_aligned(type)
      ? align_object_offset(typesize_in_bytes/HeapWordSize)
      : typesize_in_bytes/HeapWordSize);
  }

  //在默认参数下，存放_metadata的空间容量是8字节，_mark是8字节 length是4字节，对象头为20字节，由于要按8字节对齐，所以会填充4字节，最终占用24字节
  static int header_size_in_bytes() {
    size_t hs = align_size_up(length_offset_in_bytes() + sizeof(int),
                              HeapWordSize);
    return (int)hs;
  }

  static int length_offset_in_bytes() {
    return UseCompressedClassPointers ? klass_gap_offset_in_bytes() :
                               sizeof(arrayOopDesc);
  }
```

在length_offset_in_bytes()函数中，使用-XX:+UseCompressedClassPointers选项来压缩类指针，默认值为true。sizeof(arrayOopDesc)的返回值为16,其中\_mark和\_metadata.\_klass各占用8字节。在压缩指针的情况下，\_mark占用8字节，\_metadata.\_narrowKlass占用4字节，共12字节。

###  etype与esize

> 或运算就是二进制的加法

==etype表示数组元素的类型，esize表示数组元素的大小==。最终会在Klass::array_layout_helper()函数中调用array_layout_helper()函数完成属性值的计算。这个函数的实现代码如下：

```c
// 来源：hotspot/src/share/vm/oops/klass.hpp
  static jint array_layout_helper(jint tag, int hsize, BasicType etype, int log2_esize)
  {
    return 
        // 左移30位
        (tag << _lh_array_tag_shift) 
        // 左移16位
        | (hsize << _lh_header_size_shift) 
        // 左移8位
        | ((int)etype << _lh_element_type_shift) 
        // 左移0位
        | (log2_esize << _lh_log2_element_size_shift);
  }
```

最终计算出来的数组类型的\_layout_helper值为负数，因为最高位为1,而对象类型通常是一个正数，这样就可以简单地通过判断\_layout_helper值来区分数组和对象。_layout_helper的最终布局如图2-2所示。

![image](https://github.com/YangLuchao/img_host/raw/master/20230511/image.7jezrogueno0.webp)

## 2.\_primary_supers、\_super_check_offset、\_secondary_supers与_secondary_super_cache

==\_primary_supers、\_super_check_offset、\_secondary_supers与\_secondary_super_cache这几个属性完全是为了加快判定父子关系等逻辑而加入的==。

==在设置当前类型的父类时通常都会调用initialize_supers()函数，同时也会设置primary_supers与\_super_check_offset属性的值，如果继承链过长，还有可能设置secondary_supers、_secondary_super_cache属性的值。这些属性中保存的信息可用来快速地进行类之间关系的判断，如父子关系的判断。==

下面看一下在initialize_supers()函数中是如何初始化这几个属性的

```c
void Klass::initialize_supers(Klass *k, TRAPS)
{
  if (FastSuperclassLimit == 0)
  {
    set_super(k);
    return;
  }
  // 当前类的父类k可能为NULL,例如object的父类为NULL
  if (k == NULL)
  {
    set_super(NULL);
    _primary_supers[0] = this;
    assert(super_depth() == 0, "Object must already be initialized properly");
  }
  // k就是当前类的直接父类，如果有父类，那么super()一般为NULL,如果k为NULL,那么就是Object类
  else if (k != super() || k == SystemDictionary::Object_klass())
  {
    assert(super() == NULL || super() == SystemDictionary::Object_klass(),
           "initialize this only once to a non-trivial value");
    set_super(k);
    Klass *sup = k;
    int sup_depth = sup->super_depth();
    // 调用primary_super_limit()函数得到的默认值为8
    juint my_depth = MIN2(sup_depth + 1, (int)primary_super_limit());
    // 当父类的继承链长度大于等于primary_super_limit()时
    // 当前的深度只能是primary_super_limit(),也就是8
    // 因为_primary_supers数组中最多只能保存8个类
    if (!can_be_primary_super_slow())
      my_depth = primary_super_limit();
      // my_depth的默认值为8
      // 将直接父类的继承类复制到_primary_supers中
      // 因为直接父类和当前子类肯定有共同的继承链
    for (juint i = 0; i < my_depth; i++)
    {
      _primary_supers[i] = sup->_primary_supers[i];
    }
    Klass **super_check_cell;
    if (my_depth < primary_super_limit())
    {
      // 将当前类存储在_primary_supers中
      _primary_supers[my_depth] = this;
      super_check_cell = &_primary_supers[my_depth];
    }
    else
    {
      // 需要将部分父类放入_secondary_supers数组中.
      super_check_cell = &_secondary_super_cache;
    }
    //设置Klass类中的_super_check_offset属性
    set_super_check_offset((address)super_check_cell - (address)this);
     
  	if (secondary_supers() == NULL)
  	{
    	KlassHandle this_kh(THREAD, this);

    	int extras = 0;
    	Klass *p;
        // 当p不为NULL并且p已经存储在_secondary_supers数组中时，条件为true,也就
        // 是当前类的父类多于8个时，需要将多出来的父类存储到_secondary_supers数组中
    	for (p = super(); !(p == NULL || p->can_be_primary_super()); p = p->super())
    	{
     	 ++extras;
    	}
		
        // 计算secondaries的大小，因为secondaries数组中还需要存储当前类型的
        // 所有实现接口（包括直接和间接实现的接口）
    	GrowableArray<Klass *> *secondaries = compute_secondary_supers(extras);
    	if (secondaries == NULL)
    	{

      	return;
    	}
		// 将无法存储在_primary_supers中的类暂时存储在primaries中
    	GrowableArray<Klass *> *primaries = new GrowableArray<Klass *>(extras);

    	for (p = this_kh->super(); !(p == NULL || p->can_be_primary_super()); p = p->super())
    	{
         ...
     	 primaries->push(p);
    	}

    	int new_length = primaries->length() + secondaries->length();
    	Array<Klass *> *s2 = MetadataFactory::new_array<Klass *>(
      	  class_loader_data(), new_length, CHECK);
   		int fill_p = primaries->length();
    	for (int j = 0; j < fill_p; j++)
    	{
            // 这样的设置会让父类永远在数组前，而子类永远在数组后
      		s2->at_put(j, primaries->pop()); 
    	}
    	for (int j = 0; j < secondaries->length(); j++)
    	{
            // 类的部分存储在数组的前面，接口存储在数组的后面
      		s2->at_put(j + fill_p, secondaries->at(j));
    	}
        // 设置_secondary_supers属性
    	this_kh->set_secondary_supers(s2);
  	}
}

```

==is_subtype_of()函数会利用以上属性保存的一些信息进行父子关系的判断==，代码如下：

```c
// 来源： hotspot/src/share/vm/oops/klass.hpp  
/*
判断当前类是否为k的子类。k可能为接口，如果当前类型实现了k接口，函数也返回true
*/
bool is_subtype_of(Klass *k) const
  {
    // 拿到k类型的继承链的偏移量
    juint off = k->super_check_offset();
    // this + 偏移量强转为地址，再强转为Klass的二级指针，再解指针
    // 拿到继承类指针
    Klass *sup = *(Klass **)((address)this + off);
    const juint secondary_offset = in_bytes(secondary_super_cache_offset());
    // 如果k在_primary_supers中，那么利用_primary_supers一定能判断出k与当前类的父子关系
    if (sup == k)
    {
      return true;
    }
    // 如果k存储在_secondary_supers中，那么当前类也肯定存储在secondary_sueprs中
    // 如果两者有父子关系，那么_super_check_offset需要与_secondary_super_cache相等
    else if (off != secondary_offset)
    {
      return false;
    }
    else
    {
      return search_secondary_supers(k);
    }
  }
```

当通过\_super_check_offset获取的类与k相同时，k存在于当前类的继承链上，肯定有父子关系。如果k存在于\_primary_supers数组中，那么通过\_super_check_offset就可快速判断；如果k存在于_secondary_supers中，需要调用search_secondary_supers()函数来判断。调用search_secondary_supers()函数的代码如下：

```c
// 来源：hotspot/src/share/vm/oops/klass.cpp
bool Klass::search_secondary_supers(Klass *k) const
{
  if (this == k)
    return true;

  int cnt = secondary_supers()->length();
  for (int i = 0; i < cnt; i++)
  {
    if (secondary_supers()->at(i) == k)
    {
      // 属性_secondary_super_cache保存了这一次父类查询的结果
      ((Klass *)this)->set_secondary_super_cache(k);
      return true;
    }
  }
  return false;
}
```

从代码中可以看到，属性\_secondary_super_cache保存了这一次父类查询的结果。查询的逻辑很简单，遍历_secondary_supers数组中的值并比较即可。

## 3.\_supe、\_subklass和\_next_sibling_

==由于Java是单继承方式，因此可通过\_super、\_subklass和\_next_sibling属性直接找到当前类型的父类或所有子类型==。调用Klass::append_to_sibling_list()函数设置\_next_sibling与_subklass属性的值，代码如下：

```c
// 来源：hotspot/src/share/vm/oops/klass.cpp
void Klass::append_to_sibling_list()
{
  // 获取super的值
  InstanceKlass *super = superklass();
  // 如果Klass实例表示的是object类，此类没有超类
  if (super == NULL)
    return;                      
  // super可能有多个子类，多个子类会用_next_sibling属性连接成单链表，
  // 当前的类是链表头元素获取subklass属性的值
  Klass *prev_first_subklass = super->subklass_oop();
  if (prev_first_subklass != NULL)
  {
    // 设置_next_sibling属性的值
    set_next_sibling(prev_first_subklass);
  }
  // 设置_subklass属性的值
  super->set_subklass(this);
}
```



# InstanceKlass类

==每个InstanceKlass实例都表示一个具体的Java类型==（这里的Java类型不包括Java数组类型）。

InstanceKlass类及重要属性的定义如下：

```c
// 来源： hotspot/src/share/vm/oops/instanceKlass.hpp
class InstanceKlass: public Klass {
 ...
 protected:

  /*
  数组元素为该类型的数组Klass指针。
  例如，当ObjArrayKlass实例表示的是数组且元素类型为Object时，表示Object类的InstanceKlass实例的array_klasses就是指向ObjArrayKlass实例的指针
  */
  Klass*          _array_klasses;

  /*
  以该类型作为数组组件类型（指的是数组去掉一个维度的类型）的数组名称，
  如果当前InstanceKlass实例表示Object类，则名称为"[Ljava/lang/Object;"
  */
  Symbol*         _array_name;
  
  // ConstantPool类型的指针，用来指向保存常量池信息的ConstantPool实例
  ConstantPool* _constants;

  /*
  非静态字段需要占用的内存空间，以字为单位。
  在为该InstanceKlass实例表示的Java类所创建的对象（使用oop表示）分配内存时，
  会参考此属性的值分配内存，在类解析时会事先计算好这个值
  */
  int             _nonstatic_field_size;
  
  /*
  静态字段需要占用的内存空间，以字为单位。
  在为该InstanceKlass实例表示的Java类创建对应的java.lang.Class对象（使用oop表示）时，
  会根据此属性的值分配对象内存，在类解析时会事先计算好这个值
  */
  int             _static_field_size;    
  
  // 保存Java类的签名在常量池中的索引
  u2              _generic_signature_index;
  // 保存Java类的源文件名在常量池中的索引
  u2              _source_file_name_index;
  // Java类包含的静态引用类型字段的数量
  u2              _static_oop_field_count;
  // Java类包含的字段总数量
  u2              _java_fields_count;   
  /*
  OopMapBlock需要占用的内存空间，以字为单位。
  OopMapBlock使用<偏移量，数量>描述Java类（InstanceKlass实例）中各个非静态对象
  (oop)类型的变量在Java对象中的具体位置，这样垃圾回收时就能找到Java对多中引用的其他对象
  */
  int             _nonstatic_oop_map_size;

  // 类的主版本号
  u2              _minor_version; 
  // 类的次版本号
  u2              _major_version;    
  // 执行Java类初始化的Thread指针
  Thread*         _init_thread;
  // Java虚函数表（vtable)所占用的内存空间，以字为单位
  int _vtable_len; 
  // Java接口函数表（itable)所占用的内存空间，以字为单位
  int _itable_len;                      
  ...
  /*
  表示类的状态，为枚举类型ClassState,定义了如下常量值：
  allocated(已分配内存）、
  loaded(读取Class文件信息并加载到内存中）、
  linked(经成功连接和校验）、
  being_initialized(正在初始化）、
  fully_initializec(已经完成初始化）
  initialization_error(初始化发生错误）
  */
  u1              _init_state;  
  // 引用类型，表示当前的InstanceKlass实例的引用类型，可能是强引用、软引厂用和弱引用等
  u1              _reference_type;                
  // 保存方法的指针数组
  Array<Method*>* _methods;
  // 保存方法的指针数组，是从接口继承的默认方法
  Array<Method*>* _default_methods;
  // 保存接口的指针数组，是直接实现的接口Klass
  Array<Klass*>* _local_interfaces;
  // 保存接口的指针数组，包含local interfaces和间接实现的接口
  Array<Klass*>* _transitive_interfaces;
  // 默认方法在虚函数表中的索引
  Array<int>*     _default_vtable_indices;
  /*
  类的字段属性，每个字段有6个属性，分别为
  access、name index、sig index、initial value index、low_offset和high_offset,
  它们组成一个元组。
  access示访问控制属性，
  根据name index可以获取属性名，
  根据initial value index可以获取初始值，
  根据low_offset与high_offset可以获取该属性在内存中的偏移量。
  保存以上所有的属性之后还可能会保存泛型签名信息
  */
  Array<u2>*      _fields;
  ...
}
```

==InstanceKlass类与Klass类中定义的这些属性用来保存Java类元信息==。

在后续的类解析中会看到对相关属性的赋值操作。==除了保存类元信息外，Klass类还有另外一个重要的功能，即支持方法分派，这主要是通过Java虚函数表和Java接口函数表来完成的。不过C+并不像Java一样需要在保存信息时在类中定义相关属性，C++只是在分配内存时为要存储的信息分配特定的内存，然后直接通过内存偏移来操作即可。==以下几个属性没有对应的属性名，不过可以==通过指针加偏移量的方式访问==。

- ==Java vtable:Java虚函数表，大小等于\_vtable_len。==
- ==Java itable:Java接口函数表，大小等于\_itable_len。==
- ==非静态`OopMapBlock`:大小等于`_nonstatic_oop_map_size`。当前类也会继承父类的属性，因此同样可能需要保存父类的`OopMapBlock`信息，这样当前的`Klass`实例可能会含有多个`OopMapBlock`。GC在回收垃圾时，如果遍历某个对象所引用的其他对象，则会依据此信息进行查找。==

- ==接口的实现类：只有当前Klass实例表示一个接口时才存在这个信息。如果接口没有任何实现类，则为NULL;如果只有一个实现类，则为该实现类的Klass指针；如果有多个实现类，则为当前接口本身。==
- `host_klass`:只在匿名类中存在，为了支持JSR292中的动态语言特性，会给匿名类生成一个`host_klass`。

==HotSpot VM在解析一个类时会调用InstanceKlass::allocate_instance_klass()函数分配内存，而分配多大的内存则是通过调用InstanceKlass::size()函数计算出来的。==代码如下：

```c
// 来源：hotspot/src/share/vm/oops/instanceKlass.cpp
InstanceKlass* InstanceKlass::allocate_instance_klass(
                                              ClassLoaderData* loader_data,
                                              int vtable_len,
                                              int itable_len,
                                              int static_field_size,
                                              int nonstatic_oop_map_size,
                                              ReferenceType rt,
                                              AccessFlags access_flags,
                                              Symbol* name,
                                              Klass* super_klass,
                                              bool is_anonymous,
                                              TRAPS) {

  int size = InstanceKlass::size(vtable_len, itable_len, nonstatic_oop_map_size,
                                 access_flags.is_interface(), is_anonymous);
...
}


// hotspot/src/share/vm/oops/instanceKlass.hpp 
static int size(int vtable_length, int itable_length,
                  int nonstatic_oop_map_size,
                  bool is_interface, bool is_anonymous)
  {
    // Instanceklass类本身占用的内存空间
    return align_object_size(header_size() +
                             //vtable占用的内存空间
                             align_object_offset(vtable_length) +
                             //itable占用的内存空间
                             align_object_offset(itable_length) +
                             (
                                 // OopMapBlock占用的内存空间
                                 (is_interface || is_anonymous) ? align_object_offset(nonstatic_oop_map_size) : nonstatic_oop_map_size
                             ) +
                             (
                                 // 针对接口存储信息
                                 is_interface ? (int)sizeof(Klass *) / HeapWordSize : 0
                             ) +
                             (
                                 // 针对匿名类存储信息
                                 is_anonymous ? (int)sizeof(Klass *) / HeapWordSize : 0)
                            );
}
```

size()函数的返回值就是此次创建Klass实例所需要开辟的内存空间。由该函数的计算逻辑可以看出，Klass实例的内存布局如图2-3所示。

![image](https://github.com/YangLuchao/img_host/raw/master/20230511/image.4bfbydzjic00.webp)

InstanceKlass本身占用的内存其实就是类中声明的实例变量。

如果类中定义了虚函数，那么依据C++类实例的内存布局，还需要为一个指向C++虚函数表的指针预留存储空间。

图2-3中的灰色部分是可选的，是否选择需要依据虚拟机的实际情况而定。

为vtable、itable及nonstatic_oop_map分配的内存空间在类解析的过程中会事先计算好，第4章会详细介绍。==调用header_size()函数计算InstanceKlass本身占用的内存空间==，代码如下：

```c
// 来源：hotspot/src/share/vm/oops/instanceKlass.hpp
static int header_size() { 
  // HeapwordSize在64位系统下的值为8,也就是一个字的大小，同时也是一个非压缩指针占用的内存空间
  return align_object_offset(sizeof(InstanceKlass) / HeapWordSize); 
}
```

调用align_object_offset()函数进行内存对齐，方便对内存进行高效操作。

# InstanceKlass类的子类

InstanceKlass类共有3个直接子类，分别是`InstanceRefKlass`、`InstanceMirrorklass`和`lnstanceClassLoaderklass`,它们用来表示一些特殊的Java类。下面简单介绍一下这3个子类。

## 1.表示Java引用类型的InstanceRefKlass

> InstanceRefKlass是java引用的具体实现

==`java.lang.ref.Reference`类需要使用C++类`InstanceRefKlass`的实例来表示==，在创建这个类的实例时，==`_reference_type`(定义在InstanceKlass中)字段（定义在InstanceKlass类中）的值通常会指明`java.lang.ref.Reference`类表示的是哪种引用类型==。值通过枚举类进行定义，代码如下：

```c
// 来源： hotspot/src/share/vm/memory/referenceType.hpp
enum ReferenceType {
	REF_NONE,//普通类，也就是非引用类型表示java/lang/ref/Reference子类，但是这个子类不是REF_SOFT、REF_WEAK、REF_FINAL和REF_PHANTOM中的任何一种
	REF_OTHER,
	REF_SOFT,	//表示java/lang/ref/SoftReference类及其子类
	REF_WEAK,	//表示java/lang/ref/WeakReference类及其子类
	REF_FINAL,	//表示java/lang/ref/FinalReference类及其子类
	REF_PHANTOM	//表示java/lang/ref/PhantomReference类及其子类
};
```

当为引用类型但是又不是==REF_SOFT、REF_WEAK、REF_FINAL和REF_PHANTOM==中的任何一种类型时，\_reference_type属性的值为REF_OTHER。==通过_reference_type可以将普通类与引用类型区分开==，因为引用类型需要垃圾收集器进行特殊处理。

## 2.表示java.lang.Class类的InstanceMirrorKlass

==InstanceMirrorKlass类实例表示特殊的java.lang.Class类==，这个类中新增了一个静态属==_offset_of_static_fields,用来保存静态字段的起始偏移量==。代码如下：

```c++
// 来源：hotspot/src/share/vm/oops/instanceMirrorKlass.hpp
class InstanceMirrorKlass: public InstanceKlass {
  // 保存静态字段的起始偏移量
  static int _offset_of_static_fields;
  ...
}
```

正常情况下，HotSpot VM使用Klass表示Java类，用oop表示Java对象。而==Java类中可能定义了静态或非静态字段，因此将非静态字段值存储在oop中，静态字段值存储在表示当前Java类的java.lang.Class对象中==。

需要特别说明一下，java.lang.Class类比较特殊，用InstanceMirrorKlass实例表示，java.lang.Class对象用oop对象表示。==由于java.lang.Class类自身也定义了静态字段，这些值同样存储在java.lang.Class对象中，也就是存储在表示java.lang.Class对象的oop中，这样静态与非静态字段就存储在一个oop上，需要参考\_offset_of_static_fields属性的值进行偏移来定位静态字段的存储位置==。在init_offset_of_static_fields()函数中初始化_offset_of_static_fields属性，代码如下：

```c++
// 来源：hotspot/src/share/vm/oops/instanceMirrorKlass.hpp
static void init_offset_of_static_fields() {
    _offset_of_static_fields =
        InstanceMirrorKlass::cast(SystemDictionary::Class_klass())
        ->size_helper() << LogHeapWordSize;
}
```

调用size_helper()函数获取oop(表示java.lang.Class对象)的大小。紧接着oop后开始存储静态字段的值。调用size_helper()函数的代码如下：

```c++
// 来源：hotspot/src/share/vm/oops/instanceKlass.hpp  
  int size_helper() const
  {
    return layout_helper_to_size_helper(layout_helper());
  }
```

上面的代码调用了layout_helper()函数获取Klass类中定义的\_layout_helper属性的值，然后调用了layout_helper_to_size_helper()函数获取对象所需的内存空间。对象占用的内存空间在类解析过程中会计算好并存储到_layout_helper属性中。layout_helper_to_size_helper()函数的代码如下：

```c++
  static int layout_helper_to_size_helper(jint lh) {
    // 
    return lh >> LogHeapWordSize;
  }
```

> layout_helper()返回的_layout_helper属性，直接就是oop对象所占内存空间大小，以字节为单位
>
> layout_helper_to_size_helper()中右移三位，压缩指针过程，减少内存消耗
>
> init_offset_of_static_fields()中左移三位，压缩指针还原过程

调用size_helper()函数获取oop对象（表示java.lang.Class对象）的值，这个值是java.lang.Class类中声明的一些属性需要占用的内存空间，紧随其后的就是静态存储区域。

添加虚拟机参数命令-XX:+PrintFieldLayout后，打印的java.lang.Class对象的非静态字段布局如下：

![image](https://github.com/YangLuchao/img_host/raw/master/20230521/image.4ss33pm49c40.jpg)

以上就是java.lang.Class对象非静态字段的布局，在类解析过程中已经计算出了各个字段的偏移量。当完成非静态字段的布局后，紧接着会布局静态字段，此时的_offset_of_static_fields属性的值为96。

我们需要分清Java类及对象在HotSpot VM中的表示形式，如图2-4所示。

![image](https://github.com/YangLuchao/img_host/raw/master/20230521/image.b2axbdx0nww.webp)

==java.lang.Class对象是通过对应的oop实例保存类的静态属性的，需要通过特殊的方式计算它们的大小并遍历各个属性==。

==Klass类的_java_mirror属性指向保存该Java类静态字段的oop对象，可通过该属性访问类的静态字段==。oop是HotSpot VM的对象表示方式，将在2.2节中详细介绍。

## 3.表示java.lang.ClassLoader类的InstanceClassLoaderKlass

InstanceClassLoaderklass类没有添加新的字段，但增加了新的oop遍历方法，在垃圾回收阶段遍历类加载器加载的所有类来标记引用的所有对象。==下面介绍一下HotSpot VM创建Klass类的实例的过程。调用InstanceKlass::allocate_instance_klass()函数创建InstanceKlass实例。在创建时首先需要分配内存，这涉及C++对new运算符重载的调用，通过重载new运算符的函数为对象分配内存空间，然后再调用类的构造函数初始化相关的属性。==相关函数的实现代码如下：

```c++
// hotspot/src/share/vm/oops/instanceKlass.cpp
InstanceKlass* InstanceKlass::allocate_instance_klass(
                                              ClassLoaderData* loader_data,
                                              int vtable_len,
                                              int itable_len,
                                              int static_field_size,
                                              int nonstatic_oop_map_size,
                                              ReferenceType rt,
                                              AccessFlags access_flags,
                                              Symbol* name,
                                              Klass* super_klass,
                                              bool is_anonymous,
                                              TRAPS) {
  // 获取创建InstanceKlass实例时需要分配的内存空间
  // 首先调用InstanceKlass::size()函数获取表示Java类的Klass实例的内存空间
  int size = InstanceKlass::size(vtable_len, itable_len, nonstatic_oop_map_size,
                                 access_flags.is_interface(), is_anonymous);

  // 根据需要创建的类型rt(引用类型)，判断是引用类还是普通类创建不同的C++类实例
  InstanceKlass* ik;
  if (rt == REF_NONE) {
    // 通过InstanceMirrorKlass实例表示java.lang.Class类
    if (name == vmSymbols::java_lang_Class()) {
      ik = new (loader_data, size, THREAD) InstanceMirrorKlass(
        vtable_len, itable_len, static_field_size, nonstatic_oop_map_size, rt,
        access_flags, is_anonymous);
    }
    // 通过InstanceClassLoaderklass实例表示java.lang.ClassLoader或相关子类
    else if (name == vmSymbols::java_lang_ClassLoader() ||
          (SystemDictionary::ClassLoader_klass_loaded() &&
          super_klass != NULL &&
          super_klass->is_subtype_of(SystemDictionary::ClassLoader_klass()))) {
      ik = new (loader_data, size, THREAD) InstanceClassLoaderKlass(
        vtable_len, itable_len, static_field_size, nonstatic_oop_map_size, rt,
        access_flags, is_anonymous);
    }
    // 通过InstanceKlass实例表示普通类
    else {
      ik = new (loader_data, size, THREAD) InstanceKlass(
        vtable_len, itable_len, static_field_size, nonstatic_oop_map_size, rt,
        access_flags, is_anonymous);
    }
  } else {
    // 通过InstanceRefKlass实例表示引用类型
    ik = new (loader_data, size, THREAD) InstanceRefKlass(
        vtable_len, itable_len, static_field_size, nonstatic_oop_map_size, rt,
        access_flags, is_anonymous);
  }
  ...
  return ik;
}

```

> Klass类的new运算符全部被重载，全部被new到元数据区

==通过重载new运算符开辟C++类实例的内存空间==，代码如下：

```c++
// 来源：hotspot/src/share/vm/oops/klass.cpp
void *Klass::operator new(size_t size, ClassLoaderData *loader_data, size_t word_size, TRAPS) throw()
{
  // 在元数据区分配内存空间
  return Metaspace::allocate(loader_data, word_size, /*read_only*/ false,
                             MetaspaceObj::ClassType, THREAD);
}
```

对于OpenJDK 8来说，Klass实例在元数据区分配内存。Klass一般不会卸载，因此没有放到堆中进行管理，堆是垃圾收集器回收的重点，将类的元数据放到堆中时回收的效率会降低。

# ArrayKlass类

ArrayKlass类继承自Klass类，是所有数组类的抽象基类，该类及其重要属性的定义如下：

```c++
// 来源：hotspot/src/share/vm/oops/arrayKlass.hpp
class ArrayKlass: public Klass {
    ...
 private:
  // 当前实例表示的是n维的数组，int类型，表示数组的维度，记为n
  int      _dimension;     
  // 指向n+1维的数组，Klass指针类型，表示对n+1维数组Klass的引用
  Klass* volatile _higher_dimension;
  //指向n-1维的数组，Klass指针类型，表示对n-1维数组Klass的引用
  Klass* volatile _lower_dimension; 
  // vtable的大小，int类型，保存虚函数表的长度
  int      _vtable_len; 
  // 组件类型对应的java.lang.Class对象
  // oop类型，保存数组的组件类型对应的java.lang.Class对象的oop
  oop      _component_mirror;  			
    ...
}
```

==数组元素类型（Element Type)指的是数组去掉所有维度的类型，而数组的组件类型（Component Type)指的是数组去掉一个维度的类型==。_vtable_len的值为5,因为数组是引用类型，父类为Object类，而Object类中有5个虚方法可被继承和重写，具体如下：

```java
void		finalize()
boolean		equals(Object)
String		toString()
int			hashcode()
Object		clone()
```

因此数组类型也有从Object类继承的方法。

# TypeArrayKlass类

> TypeArrayKlass表示Java基本类型的数组组件类型
>
> ObjArrayKlass表示Java对象类型的数组组件类型

ArrayKlass的子类中有表示数组组件类型是Java基本类型的TypeArrayKlass,以及表示组件类型是对象类型的ObjArrayKlass,本节将介绍TypeArrayKlass。TypeArrayKlass是ArrayKlass类的子类。类及重要属性的定义如下：

```c++
// 来源：hotspot/src/share/vm/oops/typeArrayKlass.hpp
class TypeArrayKlass : public ArrayKlass {
	...
  // _max_length属性用于保存数组允许的最大长度
  jint _max_length;  
  	...
}
```

其中，_max_length属性用于保存数组允许的最大长度。==数组类和普通类不同，数组类没有对应的Class文件，因此数组类是虚拟机直接创建的==。==HotSpot VM在初始化时就会创建Java中8个基本类型的一维数组实例TypeArrayKlass==。前面在介绍HotSpot VM启动时讲过initializeJVM()函数，这个函数会调用Universe::genesis()函数，在Universe:genesis()函数中初始化基本类型的一维数组实例TypeArrayKlass。例如，初始化boolean类型的一维数组，调用语句如下：

```c++
void Universe::genesis(TRAPS) {
    // 初始化各种基本类型的一维数组
        _boolArrayKlassObj      = TypeArrayKlass::create_klass(T_BOOLEAN, sizeof(jboolean), CHECK);
        _charArrayKlassObj      = TypeArrayKlass::create_klass(T_CHAR,    sizeof(jchar),    CHECK);
        _singleArrayKlassObj    = TypeArrayKlass::create_klass(T_FLOAT,   sizeof(jfloat),   CHECK);
        _doubleArrayKlassObj    = TypeArrayKlass::create_klass(T_DOUBLE,  sizeof(jdouble),  CHECK);
        _byteArrayKlassObj      = TypeArrayKlass::create_klass(T_BYTE,    sizeof(jbyte),    CHECK);
        _shortArrayKlassObj     = TypeArrayKlass::create_klass(T_SHORT,   sizeof(jshort),   CHECK);
        _intArrayKlassObj       = TypeArrayKlass::create_klass(T_INT,     sizeof(jint),     CHECK);
        _longArrayKlassObj      = TypeArrayKlass::create_klass(T_LONG,    sizeof(jlong),    CHECK);
        _typeArrayKlassObjs[T_BOOLEAN] = _boolArrayKlassObj;
        _typeArrayKlassObjs[T_CHAR]    = _charArrayKlassObj;
        _typeArrayKlassObjs[T_FLOAT]   = _singleArrayKlassObj;
        _typeArrayKlassObjs[T_DOUBLE]  = _doubleArrayKlassObj;
        _typeArrayKlassObjs[T_BYTE]    = _byteArrayKlassObj;
        _typeArrayKlassObjs[T_SHORT]   = _shortArrayKlassObj;
        _typeArrayKlassObjs[T_INT]     = _intArrayKlassObj;
        _typeArrayKlassObjs[T_LONG]    = _longArrayKlassObj;
		...
      }
}

```



其中，_boolArrayKlassObj是Universe类中定义的静态属性，定义如下：

```c++
  // 来源：hotspot/src/share/vm/memory/universe.hpp
  static Klass* _boolArrayKlassObj;
  static Klass* _byteArrayKlassObj;
  static Klass* _charArrayKlassObj;
  static Klass* _intArrayKlassObj;
  static Klass* _shortArrayKlassObj;
  static Klass* _longArrayKlassObj;
  static Klass* _singleArrayKlassObj;
  static Klass* _doubleArrayKlassObj;
```

调用TypeArrayKlass::create_klass()函数创建TypeArrayKlass实例，代码如下：

```c++
// 来源：hotspot/src/share/vm/oops/typeArrayKlass.hpp
static inline Klass* create_klass(BasicType type, int scale, TRAPS) {
    TypeArrayKlass* tak = create_klass(type, external_name(type), CHECK_NULL);
    return tak;
}
```

调用另外一个TypeArrayKlass::create_klass()函数创建TypeArrayKlass实例，代码如下：

```c++
// 来源：hotspot/src/share/vm/oops/typeArrayKlass.cpp
TypeArrayKlass* TypeArrayKlass::create_klass(BasicType type,
                                      const char* name_str, TRAPS) {
  // 在HotSpot中，所有的字符串都通过symbol实例来表示，以达到重用的目的
  Symbol* sym = NULL;
  if (name_str != NULL) {
    sym = SymbolTable::new_permanent_symbol(name_str, CHECK_NULL);
  }
  // 使用系统类加载器加载数组类型
  ClassLoaderData* null_loader_data = ClassLoaderData::the_null_class_loader_data();
  // 创建TypeArrayKlass并完成部分属性的初始化
  TypeArrayKlass* ak = TypeArrayKlass::allocate(null_loader_data, type, sym, CHECK_NULL);

  null_loader_data->add_class(ak);
  // 初始化TypeArrayKlass中的属性
  complete_create_array_klass(ak, ak->super(), CHECK_NULL);

  return ak;
}
```

## 1.TypeArrayKlass::allocate()函数

TypeArrayKlass::allocate()函数的实现代码如下：

```c++
// 来源：hotspot/src/share/vm/oops/typeArrayKlass.cpp
TypeArrayKlass* TypeArrayKlass::allocate(ClassLoaderData* loader_data, BasicType type, Symbol* name, TRAPS) {
  int size = ArrayKlass::static_size(TypeArrayKlass::header_size());
  return new (loader_data, size, THREAD) TypeArrayKlass(type, name);
}
```

首先获取TypeArrayKlass实例需要占用的内存，然后通过重载new运算符为对象分配内存，最后调用TypeArrayKlass的构造函数初始化相关属性。

header_size()函数的实现代码如下：

```c++
// hotspot/src/share/vm/oops/typeArrayKlass.hpp
static int header_size()  { return sizeof(TypeArrayKlass)/HeapWordSize; }
```

static_size()函数的实现代码如下：

```c++
int ArrayKlass::static_size(int header_size) {
  header_size = InstanceKlass::header_size();
  int vtable_len = Universe::base_vtable_size();
  return align_object_size(size);
}
```

注意，header_size属性的值应该是TypeArrayKlass类自身占用的内存空间，但是现在获取的是InstanceKlass类自身占用的内存空间。这是因为InstanceKlass占用的内存比TypeArrayKlass大，有足够内存来存放相关数据。==更重要的是，为了统一从固定的偏移位置获取vtable等信息，在实际操作Klass实例的过程中无须关心是数组还是类，直接偏移固定位置后就可获取==。

前面介绍过InstanceKlass实例的内存布局，相比之下TypeArrayKlass的内存布局比较简单，如图2-5所示。ObjectArrayKlass实例的布局也和TypeArrayKlass一样。

![image](https://github.com/YangLuchao/img_host/raw/master/20230521/image.3cvqcb0h5ry0.jpg)

TypeArrayKlass的构造函数如下：

```c++
// 来源：hotspot/src/share/vm/oops/typeArrayKlass.cpp
TypeArrayKlass::TypeArrayKlass(BasicType type, Symbol* name) : ArrayKlass(name) {
  // 设置所需内存空间大小
  set_layout_helper(array_layout_helper(type));
  // 设置数组的最大长度
  set_max_length(arrayOopDesc::max_array_length(type));
  ... 
}
```

以上代码对TypeArrayKlass类中的`_layout_helper`与`_max_length`属性进行了设置，调用`Klass::array_layout_helper()`函数获取`_layout_helper`属性的值。

> ==java基本类型数组`_layout_helper`属性计算具体场景==

这个函数已经在2.1.1节中介绍过，为了方便阅读，这里再次给出实现代码：

```c++
// 源码：hotspot/src/share/vm/oops/klass.cpp
jint Klass::array_layout_helper(BasicType etype)
{

  int hsize = arrayOopDesc::base_offset_in_bytes(etype);
  int esize = type2aelembytes(etype);
  bool isobj = (etype == T_OBJECT);
  int tag = isobj ? _lh_array_tag_obj_value : _lh_array_tag_type_value;
  int lh = array_layout_helper(tag, hsize, etype, exact_log2(esize));

  return lh;
}
```

==由于T_BOOLEAN为基本类型，所以tag取值为0xC0。调用arrayOopDesc:base_offset_in_bytes()函数获取hsize的值，此值为16。数组对象由对象头、对象字段数据和对齐填充组成，这里获取的就是对象头的大小。esize表示对应类型存储所需要的字节数，对于T_BOOLEAN来说，只需要一个字节即可。最后调用array_layout_helper()函数按照约定组合成一个int类型的数字并返回。==



## 2.ArrayKlass::complete_create_array_klass()函数

ArrayKlass::complete_create_array_klass()函数的实现代码如下：

```c++
// 来源：hotspot/src/share/vm/oops/arrayKlass.cpp
void ArrayKlass::complete_create_array_klass(ArrayKlass* k, KlassHandle super_klass, TRAPS) {
  ResourceMark rm(THREAD);
  // 初始化父类
  // 初始化_primary_supers、_super_check_offset等属性
  k->initialize_supers(super_klass(), CHECK);
  // 初始化虚函数表
  k->vtable()->initialize_vtable(false, CHECK);
  // 创建java.lang.class类的oop对象
  // 设置_component_mirror属性
  java_lang_Class::create_mirror(k, Handle(THREAD, k->class_loader()), Handle(NULL), CHECK);
}
```

在2.1.1节中介绍Klass类时详细介绍过`initialize_supers()`函数，该函数会初始化`_primary_supers`、`_super_check_offset`等属性。该函数还会初始化`vtable`表，`vtable`将在6.3节中介绍。调用`java_lang_Class::create_mirror()`函数对`_component_mirror`属性进行设置，代码如下：

```c++
// 来源：hotspot/src/share/vm/classfile/javaClasses.cpp
void java_lang_Class::create_mirror(KlassHandle k, Handle class_loader,
                                    Handle protection_domain, TRAPS) {
  ...
  if (SystemDictionary::Class_klass_loaded()) {
    Handle mirror = InstanceMirrorKlass::cast(SystemDictionary::Class_klass())->allocate_instance(k, CHECK);
    if (!k.is_null()) {
      java_lang_Class::set_klass(mirror(), k());
    }
    InstanceMirrorKlass* mk = InstanceMirrorKlass::cast(mirror->klass());
    java_lang_Class::set_static_oop_field_count(mirror(), mk->compute_static_oop_field_count(mirror()));
    if (k->oop_is_array()) { //k是ArrayKlass实例
      Handle comp_mirror;
      if (k->oop_is_typeArray()) { //k是TypeArrayKlass实例
        BasicType type = TypeArrayKlass::cast(k())->element_type();
        comp_mirror = Universe::java_mirror(type);
      } else { //k是objArrayKlass实例
        Klass* element_klass = ObjArrayKlass::cast(k())->element_klass();
        comp_mirror = element_klass->java_mirror();
      }
      ArrayKlass::cast(k())->set_component_mirror(comp_mirror());
      set_array_klass(comp_mirror(), k());
    } else {
      assert(k->oop_is_instance(), "Must be");
	  // 初始化本地静态字段的值，静态字段存储在java.lang.Class对象中
      initialize_mirror_fields(k, mirror, protection_domain, THREAD);
      ...
    }
    ...
  }
  ...
}

// 来源：hotspot/src/share/vm/classfile/javaClasses.cpp
void java_lang_Class::initialize_mirror_fields(KlassHandle k,
                                               Handle mirror,
                                               Handle protection_domain,
                                               TRAPS) {
  ...
  // 初始化本地静态字段的值，静态字段存储在java.lang.Class对象中
  InstanceKlass::cast(k())->do_local_static_fields(&initialize_static_field, mirror, CHECK);
}
```

当k是`TypeArrayKlass`实例时，调用`Universe:java_mirror()`函数获取对应类型type的mirror值；当k为`ObjArrayKlass`实例时，获取的是组件类型的`java_mirror`属性值。另外，上面代码中的`create_mirror()`函数还初始化了`java.lang.Class`对象中静态字段的值，这样静态字段就可以正常使用了。==基本类型的mirror值在HotSpot VM启动时就会创建==，代码如下：

```c++
// 来源：hotspot/src/share/vm/memory/universe.cpp
void Universe::initialize_basic_type_mirrors(TRAPS) {
    // 创建表示基本类型的java.lang.Class对象，该对象用oop表示，所以_bool_mirror的类型为oop
    _int_mirror     =
      java_lang_Class::create_basic_type_mirror("int",    T_INT, CHECK);
    _float_mirror   =
      java_lang_Class::create_basic_type_mirror("float",  T_FLOAT,   CHECK);
    _double_mirror  =
      java_lang_Class::create_basic_type_mirror("double", T_DOUBLE,  CHECK);
    _byte_mirror    =
      java_lang_Class::create_basic_type_mirror("byte",   T_BYTE, CHECK);
    _bool_mirror    =
      java_lang_Class::create_basic_type_mirror("boolean",T_BOOLEAN, CHECK);
    _char_mirror    =
      java_lang_Class::create_basic_type_mirror("char",   T_CHAR, CHECK);
    _long_mirror    =
      java_lang_Class::create_basic_type_mirror("long",   T_LONG, CHECK);
    _short_mirror   =
      java_lang_Class::create_basic_type_mirror("short",  T_SHORT,   CHECK);
    _void_mirror    =
      java_lang_Class::create_basic_type_mirror("void",   T_VOID, CHECK);

    _mirrors[T_INT]     = _int_mirror;
    _mirrors[T_FLOAT]   = _float_mirror;
    _mirrors[T_DOUBLE]  = _double_mirror;
    _mirrors[T_BYTE]    = _byte_mirror;
    _mirrors[T_BOOLEAN] = _bool_mirror;
    _mirrors[T_CHAR]    = _char_mirror;
    _mirrors[T_LONG]    = _long_mirror;
    _mirrors[T_SHORT]   = _short_mirror;
    _mirrors[T_VOID]    = _void_mirror;
}

```

调用create_basic_type_mirror()函数的代码如下：

```c++
// 来源：hotspot/src/share/vm/classfile/javaClasses.cpp
oop java_lang_Class::create_basic_type_mirror(const char* basic_type_name, BasicType type, TRAPS) {
  oop java_class = InstanceMirrorKlass::cast(SystemDictionary::Class_klass())->allocate_instance(NULL, CHECK_0);
  if (type != T_VOID) {
    Klass* aklass = Universe::typeArrayKlassObj(type);
    set_array_klass(java_class, aklass);
  }
  return java_class;
}
```

在以上代码中，==调用`InstanceMirrorKlass`实例（表示`java.lang.Class`类）的`allocate_instance()`函数创建oop(表示java.lang.Class对象）,`_component_mirror`最终设置的就是这个oop==。一维或多维数组的元素类型如果是对象，使用Klass实例表示，如Object[]的元素类型为Object,使用InstanceKlass实例表示；==一维或多维数组的元素类型如果是基本类型，因为没有对应的Klass实例，所以使用java.lang.Class对象描述boolean和int等类型，这样基本类型的数组就会与oop对象（表示java.lang.Class对象）产生关联==，相关属性的指向如图2-6所示。

![image](https://github.com/YangLuchao/img_host/raw/master/20230521/image.6kuzz5r2cfw0.jpg)

> 可以在oop对象中通过_array_klass_offset保存的偏移找到对应的TypeArrayKlass实例。
>
> 可以在TypeArrayKlass中通过_component_mirror属性找到对应的oop对象

# ObjArrayKlass类

`ObjArrayKlass`是`ArrayKlass`的子类，其属性用于判断数组元素是类还是数组。`ObjArrayKlass`类的重要属性定义如下：

```c++
class ObjArrayKlass : public ArrayKlass {
	...
  // 该属性保存的是数组元素的组件类型而不是元素类型,指的是数组去掉一个维度的类型
  Klass* _element_klass;      
  // 一维基本类型数组使用TypeArrayKlass表示，二维基本类型数组使用ObjArrayKlass来表示，此时的ObjArrayKlass的_bottom_klass是TypeArrayKlass
  Klass* _bottom_klass;             
	...
}
```

ObjArrayKlass类新增了以下两个属性：

- `_element_klass:`该属性保存的是数组元素的组件类型而不是元素类型(指的是数组去掉一个维度的类型)；
- `_bottom_klass:`可以是`InstanceKlass`或者`TypeArrayKlass`,因此可能是元素类型或`TypeArrayKlass`,一维基本类型数组使用`TypeArrayKlass`表示，二维基本类型数组使用`ObjArrayKlass`来表示，此时的`ObjArrayKlass`的`_bottom_klass`是`TypeArrayKlass`。

HotSpot在Universe:genesis()函数中创建Object数组，代码如下：

```c++
//来源 hotspot/src/share/vm/memory/universe.cpp
void Universe::genesis(TRAPS) {
    ...
    InstanceKlass* ik = InstanceKlass::cast(SystemDictionary::Object_klass());
	// 调用表示Object类的InstanceKlass类的array_klass()函数
	_objectArrayKlassObj = ik->array_klass(1, CHECK);
    ...
}
```

HotSpot VM调用表示Object类的`InstanceKlass()`函数创建一维数组，因此Object数组的创建要依赖于`InstanceKlass`对象（表示Object类）。

`传递的参数1表示创建Object的一维数组类型`，array_klass()函数及调用的相关函数的实现代码如下：

```c++
// 来源：hotspot/src/share/vm/oops/klass.hpp
Klass* array_klass(int rank, TRAPS)         {  
	return array_klass_impl(false, rank, THREAD); 
}

// 来源：hotspot/src/share/vm/oops/instanceKlass.cpp
Klass* InstanceKlass::array_klass_impl(bool or_null, int n, TRAPS) {
  return array_klass_impl(this_oop, or_null, n, THREAD);
}

// 来源：hotspot/src/share/vm/oops/instanceKlass.cpp
Klass* InstanceKlass::array_klass_impl(instanceKlassHandle this_oop, bool or_null, int n, TRAPS) {
  if (this_oop->array_klasses() == NULL) {
    if (or_null) return NULL;

    ResourceMark rm;
    JavaThread *jt = (JavaThread *)THREAD;
    {
      // 通过锁保证创建一维数组类型的原子性
      MutexLocker mc(Compile_lock, THREAD);   // for vtables
      MutexLocker ma(MultiArray_lock, THREAD);

      if (this_oop->array_klasses() == NULL) {
          //创建以当前InstanceKlass实例为基本类型的一维类型数组，创建成功后保存到
          //_array_klasses属性中，避免下次再重新创建
        Klass*    k = ObjArrayKlass::allocate_objArray_klass(this_oop->class_loader_data(), 1, this_oop, CHECK_NULL);
        this_oop->set_array_klasses(k);
      }
    }
  }
  // 创建了以InstanceKlass实例为基本类型的一维数组，继续调用下面的array_klass_or_null()
  // 或array_klass函数创建符合要求的n维数组
  // 如果dim+1维的objArrayKlass仍然不等于n,则会间接递归调用本函数继续创建
  // dim+2和dim+3等，直到等于n
  ObjArrayKlass* oak = (ObjArrayKlass*)this_oop->array_klasses();
  if (or_null) {
    return oak->array_klass_or_null(n);
  }
  return oak->array_klass(n, THREAD);
}

```

> HotSpot VM创建数组流程

表示Java类型的Klass实例在HotSpot VM中唯一，所以当`_array_klass`为NULL时，表示首次以Klass为组件类型创建高一维的数组。创建成功后，将高一维的数组保存到`_array_klass`属性中，这样下次直接调用`array_klasses()`函数获取即可。

现在创建Object一维数组的`ObjArrayKlass`实例，首次创建`ObjTypeKlass`时，`InstanceKlass::_array_klasses`属性的值为NULL,这样就会调用`ObjArrayKlass::allocate_objArray_klass()`函数创建一维对象数组并保存到`InstanceKlass::_array_klasses`属性中。有了一维的对象类型数组后就可以接着调用`array_klass_or_null()`或`array_klass()`函数创建n维的对象类型数组了。

## 1.ObjArrayKlass::allocate_objArray_klass()创建一维类型数组

实现如下：

```c++
// 来源: hotspot/src/share/vm/oops/objArrayKlass.cpp
Klass* ObjArrayKlass::allocate_objArray_klass(ClassLoaderData* loader_data,
                                                int n, KlassHandle element_klass, TRAPS) {
	...
  // 为n维数组的objArrayKlass创建名称
  Symbol* name = NULL;
  if (!element_klass->oop_is_instance() ||
      (name = InstanceKlass::cast(element_klass())->array_name()) == NULL) {

    ResourceMark rm(THREAD);
    char *name_str = element_klass->name()->as_C_string();
    int len = element_klass->name()->utf8_length();
    char *new_str = NEW_RESOURCE_ARRAY(char, len + 4);
    int idx = 0;
    new_str[idx++] = '[';
    if (element_klass->oop_is_instance()) { // it could be an array or simple type
      new_str[idx++] = 'L';
    }
    memcpy(&new_str[idx], name_str, len * sizeof(char));
    idx += len;
    if (element_klass->oop_is_instance()) {
      new_str[idx++] = ';';
    }
    new_str[idx++] = '\0';
    name = SymbolTable::new_permanent_symbol(new_str, CHECK_0);
    if (element_klass->oop_is_instance()) {
      InstanceKlass* ik = InstanceKlass::cast(element_klass());
      ik->set_array_name(name);
    }
  }

  // 创建组合类型为element_klass、维度为n、名称为name的数组
  ObjArrayKlass* oak = ObjArrayKlass::allocate(loader_data, n, element_klass, name, CHECK_0);

  // 将创建的类型加到类加载器列表中，在垃圾回收时会当作强根处理
  loader_data->add_class(oak);

  ArrayKlass::complete_create_array_klass(oak, super_klass, CHECK_0);

  return oak;
}

```

`allocate_objArray_klass()`函数的参数`element_klass`有可能为TypeArrayKlass、InstanceKlass或ObjArrayKlass。最终会调用`ObjArrayklass::allocate()`函数==创建一个组合类型为element_klass、维度为n、名称为name的ObjArrayKlass==。例如，TypeArrayKlass表示一维字节数组，n为2,name为[[B,最终会创建出对应的ObjArrayKlass实例。==最后还会调用ArrayKlass::complete_create_array_klass()函数完成_component_mirror等属性设置==，complete_create_array_klass()函数在前面已经介绍过，这里不再介绍。

调用`ObjArrayKlass::allocate()`函数的实现代码如下：

```c++
// 来源：hotspot/src/share/vm/oops/objArrayKlass.cpp
ObjArrayKlass* ObjArrayKlass::allocate(ClassLoaderData* loader_data, int n, KlassHandle klass_handle, Symbol* name, TRAPS) {
  // ObjArrayKlass实例所需要的内存
  int size = ArrayKlass::static_size(ObjArrayKlass::header_size());
  // 调用new重载运算符从元空间中分配指定大小的内存
  return new (loader_data, size, THREAD) ObjArrayKlass(n, klass_handle, name);
}
```

首先需要调用`ArrayKlass::static_size()`计算出ObjArrayKlass实例所需要的内存，然后调用new重载运算符从元空间中分配指定大小的内存，最后调用ObjArrayKlass的构造函数初始化相关属性。调用`ArrayKlass::static_size()`函数的实现代码如下：

```c++
// 来源：hotspot/src/share/vm/oops/arrayKlass.cpp
int ArrayKlass::static_size(int header_size) {
  header_size = InstanceKlass::header_size();
  int vtable_len = Universe::base_vtable_size();
  int size = header_size + align_object_offset(vtable_len);
  return align_object_size(size);
}
```

==需要注意ArrayKlass实例所需要的内存的计算逻辑，以上函数在计算header_size时获取的是InstanceKlass本身占用的内存空间，而不是ArrayKlass本身占用的内存空间。这是因为InstanceKlass本身占用的内存空间比ArrayKlass大，所以以InstanceKlass本身占用的内存空间为标准进行统一操作，在不区分Klass实例的具体类型时，只要偏移`InstanceKlass:header_size()`后就可以获取vtable等信息。==

ArrayKlass::complete_create_array_klass()函数的实现代码如下：

```c++
// 来源：hotspot/src/share/vm/oops/arrayKlass.cpp
void ArrayKlass::complete_create_array_klass(ArrayKlass* k, KlassHandle super_klass, TRAPS) {
  ResourceMark rm(THREAD);
  // 初始化父类
  // 初始化_primary_supers、_super_check_offset等属性
  k->initialize_supers(super_klass(), CHECK);
  // 初始化虚函数表
  k->vtable()->initialize_vtable(false, CHECK);
  // 创建java.lang.class类的oop对象
  // 设置_component_mirror属性
  java_lang_Class::create_mirror(k, Handle(THREAD, k->class_loader()), Handle(NULL), CHECK);
}
```

在上面的代码中，调用initialize_vtable()函数完成了虚函数表的初始化，虚函数表将在6.3节中详细介绍。调用java_lang_Class::create_mirror()函数完成当前ObjTypeArray对象对应的java.lang.Class对象的创建并设置相关属性。

举个例子，表示Object类的InstanceKlass与表示一维数组Object[]的ObjArrayKlass之间的相关属性指向如图2-7所示。

![image](https://github.com/YangLuchao/img_host/raw/master/20230521/image.67v735285u00.jpg)

> 表示组件类型为对象类型的一维数组(ObjArrayKlass)通过element_klass_bottom_klass可找到对应的InstanceKlass表示的Java类
>
> InstanceKlass表示的Java类通过_array_klasses可以找到对应的ObjArrayKlass(表示组件类型为对象类型的一维数组)

如果InstanceKlass实例表示`java.lang.Object`类，那么_array_name的值为`[Ljava/lang/Object;`。

## 2.调用ObjArrayKlass:array_klass()函数创建n维类型数组

在`InstanceKlass::array_klass_impl()`函数中，如果创建好了一维类型的数组，依据这个一维类型数组就可以创建出n维类型数组，==无论调用array_klass()还是array_klass_or_null()函数，最终都会调用array_klass_impl()函数==，该函数的实现代码如下：

```c++
//来源： hotspot/src/share/vm/oops/objArrayKlass.cpp
Klass* ObjArrayKlass::array_klass_impl(bool or_null, TRAPS) {
  // 创建比当前维度多一个维度的数组
  return array_klass_impl(or_null, dimension() +  1, THREAD);
}

Klass* ObjArrayKlass::array_klass_impl(bool or_null, int n, TRAPS) {

  int dim = dimension();
  if (dim == n) return this;

  if (higher_dimension() == NULL) {
    if (or_null)  return NULL;

    ResourceMark rm;
    JavaThread *jt = (JavaThread *)THREAD;
    {
      MutexLocker mc(Compile_lock, THREAD);   
      
      MutexLocker mu(MultiArray_lock, THREAD);

      if (higher_dimension() == NULL) {
		// 以当前的objArrayKlass实例为组件类型，创建比当前dim维度多一维度的数组
        Klass* k =
          ObjArrayKlass::allocate_objArray_klass(class_loader_data(), dim + 1, this, CHECK_NULL);
        ObjArrayKlass* ak = ObjArrayKlass::cast(k);
        ak->set_lower_dimension(this);
        OrderAccess::storestore();
        set_higher_dimension(ak);
        assert(ak->oop_is_objArray(), "incorrect initialization of ObjArrayKlass");
      }
    }
  }

  // 如果dim+1维的objArrayKlass仍然不是n维的，则会间接递归调用当前的array_klass_impl()函数
  // 继续创建dim+2和dim+3等objArrayKlass实例，直到此实例的维度等于n
  ObjArrayKlass *ak = ObjArrayKlass::cast(higher_dimension());
  if (or_null) {
    return ak->array_klass_or_null(n);
  }
  return ak->array_klass(n, THREAD);
}
```

多维数组会间接递归调用以上函数创建符合维度要求的数组类型。表示Java类的InstanceKlass实例与以此Java类为元素类型的一维与二维数组之间的关系如图2-8所示。

![image](https://github.com/YangLuchao/img_host/raw/master/20230521/image.6ciomn6oz480.jpg)

二维数组Object[][]、一维数组Object[]和Object类之间的关系就符合图2-8所示的关系。
