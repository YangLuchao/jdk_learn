[toc]

==`klassItable`与`klassVtable`的作用类似，都是为了实现Java方法运行时的多态，但通过`klassItable`可以查找到某个接口对应的实现方法==。本节将详细介绍klassItable的实现。

# klassItable类

==`Java`的`itable`是`Java`接口函数表，可以方便查找某个接口对应的实现方法==。==`itable`的结构比`vtable`复杂，除了记录方法地址之外还要记录该方法所属的接口类`Klass`==，如图6-5所示。

![image](https://cdn.staticaly.com/gh/YangLuchao/img_host@master/20230524/image.1pxtyubfbsyo.jpg)

图6-5　itable的结构

如图6-5所示，==`itable`表由偏移表`itableOffset`和方法表`itableMethod`两个表组成，这两个表的长度是不固定的，即长度不一样==。==每个偏移表`itableOffset`保存的是类实现的一个接口`Klass`和该接口方法表所在的偏移位置==；==方法表`itableMethod`保存的是实现的接口方法==。==在初始化`itable`时，HotSpot VM将类实现的接口及实现的方法填写在上述两张表中==。==接口中的非`public`方法和`abstract`方法（在vtable中占一个槽位）不放入itable中==。

==调用接口方法时，HotSpot VM通过`ConstantPoolCacheEntry`的`_f1`成员拿到接口的`Klass`，在`itable`的偏移表中逐一匹配。如果匹配上则获取`Klass`的方法表的位置，然后在方法表中通过`ConstantPoolCacheEntry`的`_f2`成员找到实现的方法`Method`。==

类似于`klassVtable`，对于`itable`也有专门操作的工具类`klassItable`，类及属性的定义如下：

```cpp
// 来源：openjdk/hotspot/src/share/vm/oops/klassVtable.hpp

class klassItable : public ResourceObj {
 private:
  // `itable`所属的`Klass`
  instanceKlassHandle  _klass;
  // `itable`在所属`Klass`中的内存偏移量
  int              _table_offset;
  // `itable`中`itableOffsetEntry`的大小
  int              _size_offset_table;
  // `itable`中`itableMethodEntry`的大小
  int              _size_method_table;
  ...
}
```

`klassItable`类包含4个属性：

- `_klass`：`itable`所属的`Klass`；
- `_table_offset`：`itable`在所属`Klass`中的内存偏移量；
- `_size_offset_table`：`itable`中`itableOffsetEntry`的大小；
- `_size_method_table`：`itable`中`itableMethodEntry`的大小。

在接口表`itableOffset`中含有的项为`itableOffsetEntry`，类及属性的定义如下：

```cpp
// 来源：openjdk/hotspot/src/share/vm/oops/klassVtable.hpp

class itableOffsetEntry VALUE_OBJ_CLASS_SPEC {
 private:
  // 方法所属的接口
  Klass*   _interface;
  // 接口下的第一个方法`itableMethodEntry`相对于所属`Klass`的偏移量
  int      _offset;
  ...
}
```

其中包含两个属性：

- `_interface`：方法所属的接口；
- `_offset`：接口下的第一个方法`itableMethodEntry`相对于所属`Klass`的偏移量。

方法表`itableMethod`中含有的项为`itableMethodEntry`，类及属性的定义如下：

```cpp
// 来源：openjdk/hotspot/src/share/vm/oops/klassVtable.hpp

class itableMethodEntry VALUE_OBJ_CLASS_SPEC {
 private:
  Method*  _method;
  ...
}
```

与`vtableEntry`一样，`itableMethodEntry`也是对`Method`的一个封装。

==增加`itable`而不用`vtable`解决所有方法分派问题，是因为一个类可以实现多个接口，而每个接口的函数编号是和其自身相关的，`vtable`无法解决多个对应接口的函数编号问题。而一个子类只能继承一个父亲，子类只要包含父类`vtable`，并且和父类的函数包含部分的编号是一致的，因此可以直接使用父类的函数编号找到对应的子类实现函数==。

# 计算itable的大小

==在`ClassFileParser::parseClassFile()`函数中计算`vtable`和`itable`的大小==，前面介绍了vtable大小的计算过程，本节将详细介绍itable大小的计算过程。实现代码如下：

```cpp
instanceKlassHandle ClassFileParser::parseClassFile(Symbol* name,
                                                    ClassLoaderData* loader_data,
                                                    Handle protection_domain,
                                                    KlassHandle host_klass,
                                                GrowableArray<Handle>* cp_patches,
                                                    TempNewSymbol& parsed_name,
                                                    bool verify,
                                                    TRAPS) {
    ...
	if( access_flags.is_interface() ){
   		itable_size = 0 ;                // 当为接口时，itable_size的值为0
	}else{
	    itable_size = klassItable::compute_itable_size(_transitive_interfaces);
	}
    ...
}
```

接口不需要`itable`，只有类才有`itable`。调用`klassItable::compute_itable_size()`函数计算`itable`的大小，代码如下：

```cpp
// 来源：openjdk/hotspot/src/share/vm/oops/klassVtable.cpp

int klassItable::compute_itable_size(Array<Klass*>* transitive_interfaces) {
  // 计算接口总数和方法总数
  CountInterfacesClosure cic;
  // 调用`visit_all_interfaces()`函数计算类实现的所有接口总数（包括直接与间接实现的接口）和接口中定义的所有方法
  // 并通过`CountInterfacesClosure`类的`_nof_interfaces`与`_nof_methods`属性进行保存
  visit_all_interfaces(transitive_interfaces, &cic);
    
  // 调用`calc_itable_size()`函数计算`itable`需要占用的内存空间
  int itable_size = calc_itable_size(cic.nof_interfaces() + 1, cic.nof_methods());

  return itable_size;
}
```

在上面的代码中，调用`visit_all_interfaces()`函数计算类实现的所有接口总数（包括直接与间接实现的接口）和接口中定义的所有方法，并通过`CountInterfacesClosure`类的`_nof_interfaces`与`_nof_methods`属性进行保存。调用`visit_all_interfaces()`函数的实现代码如下：

```cpp
// 来源：openjdk/hotspot/src/share/vm/oops/klassVtable.cpp
// 计算类实现的所有接口总数（包括直接与间接实现的接口）和接口中定义的所有方法
// 并通过`CountInterfacesClosure`类的`_nof_interfaces`与`_nof_methods`属性进行保存
void visit_all_interfaces(Array<Klass*>* transitive_intf, InterfaceVisiterClosure *blk) {
  for(int i = 0; i < transitive_intf->length(); i++) {
   Klass* intf = transitive_intf->at(i);

   int method_count = 0;

   // 将Klass类型的intf转换为InstanceKlass类型后调用methods()方法
   Array<Method*>* methods = InstanceKlass::cast(intf)->methods();
   if (methods->length() > 0) {
     for (int i = methods->length(); --i >= 0; ) {
       // 当为非静态和<init>、<clinit>方法时，以下函数将返回true
       // 调用`interface_method_needs_itable_index()`函数判断接口中声明的方法是否需要在`itable`中添加一个新的`itableEntry`（指`itableOffsetEntry`和`itableMethodEntry`）
       if (interface_method_needs_itable_index(methods->at(i))) {
        method_count++;
       }
     }
   }

   // method_count表示接口中定义的方法需要添加到itable的数量
   if (method_count > 0) {
     // `doit()`函数只是对接口数量和方法进行简单的统计并保存到了`_nof_interfaces`与`_nof_methods`属性中。
     blk->doit(intf, method_count);
   }
  }
}
```

以上代码循环遍历了每个接口中的每个方法，并==调用`interface_method_needs_itable_index()`函数判断接口中声明的方法是否需要在`itable`中添加一个新的`itableEntry`（指`itableOffsetEntry`和`itableMethodEntry`）==。如果当前接口中有方法==需要新的`itableEntry`，那么会调用`CountInterfacesClosure`类中的`doit()`函数对接口和方法进行统计==。

调用`interface_method_needs_itable_index()`函数的实现代码如下：

```cpp
// 来源：openjdk/hotspot/src/share/vm/oops/klassVtable.cpp

inline bool interface_method_needs_itable_index(Method* m) {
  if (m->is_static())
     return false;
  // 当为<init>或<clinit>方法时，返回false
  if (m->is_initializer())
     // <init> or <clinit>
     return false;   
  return true;
}
```

在`visit_all_interfaces()`函数中会调用`CountInterfacesClosure`类的`doit()`函数，代码如下：

```cpp
// 来源：openjdk/hotspot/src/share/vm/oops/klassVtable.cpp

class CountInterfacesClosure : public InterfaceVisiterClosure {
 private:
  int _nof_methods;
  int _nof_interfaces;
 public:
  ...
  void doit(Klass* intf, int method_count) {
       _nof_methods += method_count;
       _nof_interfaces++;
  }
};
```

`doit()`函数只是对接口数量和方法进行简单的统计并保存到了`_nof_interfaces`与`_nof_methods`属性中。

在`klassItable::compute_itable_size()`函数中调用`calc_itable_size()`函数计算`itable`需要占用的内存空间，代码如下：

```cpp
// 来源：openjdk/hotspot/src/share/vm/oops/klassVtable.hpp

static int  calc_itable_size(int num_interfaces, int num_methods) {
     return (num_interfaces * itableOffsetEntry::size()) + (num_methods * itableMethodEntry::size());
}
```

可以清楚地看到，对于`itable`大小的计算逻辑，就是接口占用的内存加上方法占用的内存之和。但是在`compute_itable_size()`函数中调用`calc_itable_size()`函数时，`num_interfaces`为类实现的所有接口总数加1，因此最后会多出一个`itableOffsetEntry`大小的内存位置，这也是遍历接口的终止条件。

# itable的初始化

计算好`itable`需要占用的内存后就可以初始化`itable`了。`itable`中的偏移表`itableOffset`在解析类时会初始化，在`parseClassFile()`函数中有以下调用语句：

```cpp
klassItable::setup_itable_offset_table(this_klass);
```

调用`setup_itable_offset_table()`函数的实现代码如下：

```cpp
// 来源：openjdk/hotspot/src/share/vm/oops/klassVtable.cpp

void klassItable::setup_itable_offset_table(instanceKlassHandle klass) {
  if (klass->itable_length() == 0){
     return;
  }

  // 统计出接口和接口中需要存储在itable中的方法的数量
  CountInterfacesClosure cic;
  visit_all_interfaces(klass->transitive_interfaces(), &cic);
  int nof_methods    = cic.nof_methods();
  int nof_interfaces = cic.nof_interfaces();

  // 在itableOffset表的结尾添加一个Null表示终止，因此遍历偏移表时如果遇到Null就终止遍历
  nof_interfaces++;

  itableOffsetEntry* ioe = (itableOffsetEntry*)klass->start_of_itable();
  itableMethodEntry* ime = (itableMethodEntry*)(ioe + nof_interfaces);
  intptr_t* end        = klass->end_of_itable();

  // 对itableOffset表进行填充
  SetupItableClosure sic((address)klass(), ioe, ime);
  visit_all_interfaces(klass->transitive_interfaces(), &sic);
}
```

==第一次调用`visit_all_interfaces()`函数计算接口和接口中需要存储在`itable`中的方法总数，第二次调用`visit_all_interfaces()`函数初始化itable中的itableOffset信息，也就是在`visit_all_interfaces()`函数中调用`doit()`函数，不过这次调用的是`SetupItableClosure`类中定义的`doit()`函数==。`SetupItableClosure`类及`doit()`函数的定义如下：

```cpp
// 来源： openjdk/hotspot/src/share/vm/oops/klassVtable.cpp

class SetupItableClosure : public InterfaceVisiterClosure  {
 private:
  itableOffsetEntry* _offset_entry;
  itableMethodEntry* _method_entry;
  address          _klass_begin;
 public:
  SetupItableClosure(address klass_begin, itableOffsetEntry* offset_entry,
itableMethodEntry* method_entry) {
   _klass_begin  = klass_begin;
   _offset_entry = offset_entry;
   _method_entry = method_entry;
  }
  ...
  void doit(Klass* intf, int method_count) {
   int offset = ((address)_method_entry) - _klass_begin;
   // 初始化itableOffsetEntry中的相关属性
   _offset_entry->initialize(intf, offset);
   // 指向下一个itableOffsetEntry
   _offset_entry++;                      
   // 指向下一个接口中存储方法的itableMethodEntry
   _method_entry += method_count;
  }
};
```

==初始化`itableOffsetEntry`中的`_interface`与`_offset`属性==，在6.4.1节中已经介绍过itableOffsetEntry类及相关属性，这里不再赘述。

==`itable`的`itableOffset`偏移表在类解析时完成初始化，而`itable`的方法表`itableMethod`需要等到方法连接时才会初始化==。==在`InstanceKlass::link_class_impl()`函数中完成方法连接后会初始化`vtable`与`itable`==。前面已经介绍过`vtable`，接下来介绍方法表`itableMethod`的初始化过程。在`InstanceKlass::link_class_impl()`函数中的调用语句如下：

```cpp
// 来源：hotspot/src/share/vm/oops/instanceKlass.cpp
bool InstanceKlass::link_class_impl(
    instanceKlassHandle this_oop, bool throw_verifyerror, TRAPS) {
    ...
	klassItable* ki = this_oop->itable();
	ki->initialize_itable(true, CHECK_false);
	...
}
```

调用`itable()`函数及相关函数，代码分别如下：

```cpp
// 来源：openjdk/hotspot/src/share/vm/oops/instanceKlass.cpp
klassItable* InstanceKlass::itable() const {
  return new klassItable(instanceKlassHandle(this));
}

// 来源：openjdk/hotspot/src/share/vm/oops/klassVtable.cpp
klassItable::klassItable(instanceKlassHandle klass) {
  _klass = klass;

  if (klass->itable_length() > 0) {
   itableOffsetEntry* offset_entry = (itableOffsetEntry*)klass->start_of_itable();
   if (offset_entry  != NULL && offset_entry->interface_klass() != NULL) {
     intptr_t* method_entry  = (intptr_t *)(((address)klass()) + offset_entry->offset());
     intptr_t* end      = klass->end_of_itable();

     _table_offset      = (intptr_t*)offset_entry - (intptr_t*)klass();
     _size_offset_table = (method_entry - ((intptr_t*)offset_entry)) / itableOffsetEntry::size();
     _size_method_table = (end - method_entry)  / itableMethodEntry::size();
     return;
   }
  }

  _table_offset     = 0;
  _size_offset_table = 0;
  _size_method_table = 0;
}

// 来源：openjdk/hotspot/src/share/vm/oops/instanceKlass.hpp
intptr_t* start_of_itable() const         {
     return start_of_vtable() + align_object_offset(vtable_length());
}

intptr_t* end_of_itable() const           {
     return start_of_itable() + itable_length();
}
```

在`klassItable`的构造函数中会初始化`klassItable`类中定义的各个属性，这几个属性在6.4.1节中介绍过，各个属性的说明如图6-6所示。

![image](https://cdn.staticaly.com/gh/YangLuchao/img_host@master/20230524/image.14fbbp79rbj4.jpg)

图6-6　klassItable中各属性的说明

在`InstanceKlass::link_class_impl()`函数中调用`klassItable::initialize_itable()`函数对`itable`进行初始化，代码如下：

```cpp
// 来源：openjdk/hotspot/src/share/vm/oops/klassVtable.cpp

void klassItable::initialize_itable(bool checkconstraints, TRAPS) {
  if (_klass->is_interface()) {
  // 调用`klassItable::assign_itable_indices_for_interface()`函数为接口中的方法指定`itableEntry`索引
   assign_itable_indices_for_interface(_klass());
  }

  // 当HotSpot VM启动时，当前的类型为接口和itable的长度只有1时不需要添加itable。
  // 长度为1时就表示空，因为之前会为itable多分配一个内存位置作为itable遍历终止条件
  if (
     Universe::is_bootstrapping() ||
     _klass->is_interface() ||
     _klass->itable_length() == itableOffsetEntry::size()
  ){
     return;
  }

  int num_interfaces = size_offset_table() - 1;
  if (num_interfaces > 0) {
   int i;
   for(i = 0; i < num_interfaces; i++) {
     itableOffsetEntry* ioe = offset_entry(i);
     HandleMark hm(THREAD);
     KlassHandle interf_h (THREAD, ioe->interface_klass());
     initialize_itable_for_interface(ioe->offset(), interf_h, checkconstraints, CHECK);
   }
  }
}
```

`initialize_itable()`函数中的调用比较多，完成的逻辑也比较多，下面详细介绍。

## assign_itable_indices_for_interface()函数

如果当前处理的是接口，那么会==调用`klassItable::assign_itable_indices_for_interface()`函数为接口中的方法指定`itableEntry`索引==，函数的实现代码如下：

```cpp
// 来源：openjdk/hotspot/src/share/vm/oops/klassVtable.cpp

// 只有Klass实例表示的是Java接口时才会调用此函数
int klassItable::assign_itable_indices_for_interface(Klass* klass) {
  // 接口不需要itable表，不过方法需要编号
  Array<Method*>*  methods = InstanceKlass::cast(klass)->methods();
  int            nof_methods = methods->length();
  int            ime_num = 0;
  for (int i = 0; i < nof_methods; i++) {
   Method* m = methods->at(i);
    // 当为非静态和<init>、<clinit>方法时，以下函数将返回true
    if (interface_method_needs_itable_index(m)) {
     // 当_vtable_index>=0时，表示指定了vtable index，如果没有指定，则指定itable index
     if (!m->has_vtable_index()) {
       assert(m->vtable_index() == Method::pending_itable_index, "set by initialize_vtable");
       m->set_itable_index(ime_num);
       ime_num++;
     }
   }
  }
  return ime_num;
}
```

==对于需要`itableMethodEntry`的方法来说，需要为对应的方法指定编号，也就是为`Method`的`_itable_index`赋值==。`interface_method_needs_itable_index()`函数在6.4.2节中已经介绍过，这里不再赘述。

接口默认也继承了`Object`类，因此也会继承来自`Object`的5个方法。不过这5个方法并不需要`itableEntry`，已经在`vtable`中有对应的`vtableEntry`，因此这些方法调用`has_vtable_index()`函数将返回true，不会再指定itable index。

## initialize_itable_for_interface()函数

调用`klassItable::initialize_itable_for_interface()`函数处理类实现的每个接口，代码如下：

```cpp
// 来源：openjdk/hotspot/src/share/vm/oops/klassVtable.cpp

void klassItable::initialize_itable_for_interface(int method_table_offset,
KlassHandle interf_h, bool checkconstraints, TRAPS) {
  Array<Method*>* methods = InstanceKlass::cast(interf_h())->methods();
  int nof_methods = methods->length();
  HandleMark hm;
  Handle interface_loader (THREAD, InstanceKlass::cast(interf_h())->class_loader());

  // 获取interf_h()接口中需要添加到itable中的方法的数量
  int ime_count = method_count_for_interface(interf_h());
  for (int i = 0; i < nof_methods; i++) {
   Method* m = methods->at(i);
   methodHandle target;
   if (m->has_itable_index()) {
     LinkResolver::lookup_instance_method_in_klasses(target, _klass, m->name(), m->signature(), CHECK);
   }
   if (target == NULL || !target->is_public() || target->is_abstract()) {
       // Entry does not resolve. Leave it empty for AbstractMethodError.
       if (!(target == NULL) && !target->is_public()) {
        // Stuff an IllegalAccessError throwing method in there instead.
        itableOffsetEntry::method_entry(_klass(), method_table_offset)
[m->itable_index()].initialize(Universe::throw_illegal_access_error());
       }
   } else {
     int ime_num = m->itable_index();
     assert(ime_num < ime_count, "oob");
     itableOffsetEntry::method_entry(_klass(), method_table_offset)[ime_
num].initialize(target());
   }
  }
}

// 方法的参数interf一定是一个表示接口的InstanceKlass实例
int klassItable::method_count_for_interface(Klass* interf) {
  Array<Method*>* methods = InstanceKlass::cast(interf)->methods();
  int nof_methods = methods->length();
  while (nof_methods > 0) {
   Method* m = methods->at(nof_methods-1);
   if (m->has_itable_index()) {
     int length = m->itable_index() + 1;
     return length;
   }
   nof_methods -= 1;
  }
  return 0;
}
```

遍历接口中的每个方法，如果方法指定了`_itable_index`，调用`LinkResolver::lookup_instance_method_in_klasses()`函数进行处理，代码如下：

```cpp
// 来源：openjdk/hotspot/src/share/vm/interpreter/linkResolver.cpp

void LinkResolver::lookup_instance_method_in_klasses(methodHandle& result,
            KlassHandle klass, Symbol* name, Symbol* signature, TRAPS) {
  Method* result_oop = klass->uncached_lookup_method(name, signature);
  result = methodHandle(THREAD, result_oop);
  // 循环查找方法的接口实现
  while (!result.is_null() && result->is_static() && result->method_holder()->
super() != NULL) {
   KlassHandle super_klass = KlassHandle(THREAD, result->method_holder()->
super());
   result = methodHandle(THREAD, super_klass->uncached_lookup_method
(name, signature));
  }

  // 当从拥有Itable的类或父类中找到接口中方法的实现方法时，result不为NULL
  // 否则为NULL这时候就要查找默认的方法了
if (result.is_null()) {
   Array<Method*>* default_methods = InstanceKlass::cast(klass())->default_methods();
   if (default_methods != NULL) {
     result = methodHandle(InstanceKlass::find_method(default_methods,
name, signature));
     assert(result.is_null() || !result->is_static(), "static defaults not allowed");
   }
  }
}
```

在`klassItable::initialize_itable_for_interface()`函数中调用`initialize()`函数，代码如下：

```cpp
// 来源：openjdk/hotspot/src/share/vm/oops/klassVtable.cpp

void itableMethodEntry::initialize(Method* m) {
  if (m == NULL)
     return;
  _method = m;
}
```

初始化`itableMethodEntry`类中定义的唯一属性`_method`。