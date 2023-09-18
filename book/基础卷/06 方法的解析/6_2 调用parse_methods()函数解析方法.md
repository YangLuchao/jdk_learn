[toc]

# 调用parse_methods()函数解析方法

==在`ClassFileParser::parseClassFile()`函数中解析完字段并完成每个字段的布局后，会继续调用`parse_methods()`函数对Java方法进行解析。==调用`parse_methods()`函数的代码实现如下：

```cpp
// 来源：openjdk/hotspot/src/share/vm/classfile/classFileParser.cpp

Array<Method*>* ClassFileParser::parse_methods(
   bool is_interface,
   AccessFlags* promoted_flags,
   bool* has_final_method,
   bool* has_default_methods,
   TRAPS
) {
  ClassFileStream* cfs = stream();
  u2 length = cfs->get_u2_fast();
  if (length == 0) {
   _methods = Universe::the_empty_method_array();
  } else {
   _methods = MetadataFactory::new_array<Method*>(_loader_data, length, NULL, CHECK_NULL);

   HandleMark hm(THREAD);
   for (int index = 0; index < length; index++) {
     // 调用parse_method()函数解析每个Java方法
     methodHandle method = parse_method(is_interface,promoted_flags, CHECK_NULL);

     if (method->is_final()) {
       // 如果定义了final方法，那么has_final_method变量的值为true
       *has_final_method = true;
     }
     if (is_interface
       && !(*has_default_methods)
       && !method->is_abstract()
       && !method->is_static()
       && !method->is_private()) {
        // 如果定义了默认的方法，则has_default_methods变量的值为true
        *has_default_methods = true;
     }
     // 将方法存入_methods数组中
     _methods->at_put(index, method());
   }
  }
  return _methods;
}
```

以上代码中，`has_final_method`与`has_default_methods`属性的值最终会保存到表示方法所属类的`InstanceKlass`实例的`_misc_flags`和`_access_flags`属性中，供其他地方使用。

==调用`parse_method()`函数解析每个`Java`方法，该函数会返回表示方法的`Method`实例，但`Method`实例需要通过`methodHandle`句柄来操作，因此最终会封装为`methodHandle`句柄，然后存储到`_methods`数组中==。

Java虚拟机规范规定的方法的格式如下：

```c
method_info {
   u2             access_flags;
   u2             name_index;
   u2             descriptor_index;
   u2             attributes_count;
   attribute_info attributes[attributes_count];
}

attribute_info {
   u2 attribute_name_index;
   u4 attribute_length;
   u1 info[attribute_length];
}
```

`parse_method()`函数会按照以上格式读取各个属性值。首先读取`access_flags`、`name_index`与`descriptor_index`属性的值，代码如下：

```cpp
// 来源：openjdk/hotspot/src/share/vm/classfile/classFileParser.cpp

methodHandle ClassFileParser::parse_method(bool is_interface,AccessFlags *promoted_flags,TRAPS) {
  ClassFileStream* cfs = stream();
  methodHandle nullHandle;
  ResourceMark rm(THREAD);

  // 读取access_flags属性值
  int flags = cfs->get_u2_fast();            

  // 读取name_index属性值
  u2 name_index = cfs->get_u2_fast();        
  Symbol*  name = _cp->symbol_at(name_index);

  // 读取descriptor_index属性值
  u2 signature_index = cfs->get_u2_fast();   
  Symbol*  signature = _cp->symbol_at(signature_index);
  ...
}
```

接下来在`parse_method()`函数中对属性进行解析，由于方法的属性较多且有些属性并不影响程序的运行，所以我们只对重要的Code属性进行解读。Code属性的格式如下：

```cpp
Code_attribute {
   u2 attribute_name_index;
   u4 attribute_length;
   u2 max_stack;
   u2 max_locals;
   u4 code_length;
   u1 code[code_length];
   u2 exception_table_length;
   {
      u2 start_pc;
      u2 end_pc;
      u2 handler_pc;
      u2 catch_type;
   } exception_table[exception_table_length];
   u2 attributes_count;
   attribute_info attributes[attributes_count];
}
```

在parse_method()函数中会按照以上格式读取属性值，相关的代码如下：

```cpp
// 来源：openjdk/hotspot/src/share/vm/classfile/classFileParser.cpp

methodHandle ClassFileParser::parse_method(bool is_interface,AccessFlags *promoted_flags,TRAPS) {
    ...
    // 读取attributes_count属性
    u2   method_attributes_count = cfs->get_u2_fast();
    
    // 循环读取多个属性
    while (method_attributes_count--) {
       u2      method_attribute_name_index = cfs->get_u2_fast();
       u4      method_attribute_length = cfs->get_u4_fast();
       Symbol* method_attribute_name = _cp->symbol_at(method_attribute_name_index);
       // 解析Code属性
       if (method_attribute_name == vmSymbols::tag_code()) {
    
        // 读取max_stack、max_locals和code_length属性
        if (_major_version == 45 && _minor_version <= 2) {
         max_stack = cfs->get_u1_fast();
         max_locals = cfs->get_u1_fast();
         code_length = cfs->get_u2_fast();
       } else {
         max_stack = cfs->get_u2_fast();
         max_locals = cfs->get_u2_fast();
         code_length = cfs->get_u4_fast();
       }
    
       // 读取code[code_length]数组的首地址
       code_start = cfs->get_u1_buffer();
       // 跳过code_length个u1类型的数据，也就是跳过整个code[code_length]数组
       cfs->skip_u1_fast(code_length);
    
       // 读取exception_table_length属性并处理exception_table[exception_table_length]
       exception_table_length = cfs->get_u2_fast();
       if (exception_table_length > 0) {
         exception_table_start = parse_exception_table(code_length, exception_table_length, CHECK_(nullHandle));
       }
    
        // 读取attributes_count属性并处理attribute_info_attributes[attributes_count]数组
        u2 code_attributes_count = cfs->get_u2_fast();
        ...
        while (code_attributes_count--) {
         u2 code_attribute_name_index = cfs->get_u2_fast();
         u4 code_attribute_length = cfs->get_u4_fast();
         calculated_attribute_length += code_attribute_length +
                                   sizeof(code_attribute_name_index) +
                                   sizeof(code_attribute_length);
    
         if (LoadLineNumberTables &&
            _cp->symbol_at(code_attribute_name_index) == vmSymbols::tag_line_number_table()) {
              ...
            } else if (LoadLocalVariableTables &&
                     _cp->symbol_at(code_attribute_name_index) == vmSymbols::tag_local_variable_table()) {
              ...
            } else if (_major_version >= Verifier::STACKMAP_ATTRIBUTE_MAJOR_VERSION &&
                     _cp->symbol_at(code_attribute_name_index) == vmSymbols::tag_stack_map_table()) {
              ...
            } else {
              // Skip unknown attributes
              cfs->skip_u1(code_attribute_length, CHECK_(nullHandle));
            }
          } // end while
        } // end if
        ...
    } // end while
    ...
}
```

代码相对简单，只需要按照规定的格式从Class文件中读取信息即可，这些读取出来的信息最终会存储到`Method`或`ConstMethod`实例中，供HotSpot VM运行时使用。

# 创建Method与ConstMethod实例

`ClassFileParser::parse_method()`函数解析完方法的各个属性后，接着会创建`Method`与`ConstMethod`实例保存这些属性信息，调用语句如下：

```cpp
// 来源：openjdk/hotspot/src/share/vm/classfile/classFileParser.cpp

InlineTableSizes sizes(
        total_lvt_length,
        linenumber_table_length,
        exception_table_length,
        checked_exceptions_length,
        method_parameters_length,
        generic_signature_index,
        runtime_visible_annotations_length + runtime_invisible_annotations_length,
        runtime_visible_parameter_annotations_length + runtime_invisible_parameter_annotations_length,
        runtime_visible_type_annotations_length + runtime_invisible_type_annotations_length,
        annotation_default_length,
        0
);
Method* m = Method::allocate(
             _loader_data, code_length, access_flags, &sizes,
             ConstMethod::NORMAL, CHECK_(nullHandle));
```

其中，`InlineTableSizes`类中定义了保存方法中相关属性的字段，具体如下：

```cpp
// 来源：openjdk/hotspot/src/share/vm/oops/constMethod.hpp

class InlineTableSizes : StackObj {
  // 本地变量表
  int _localvariable_table_length;
  // 压缩的代码行号表
  int _compressed_linenumber_size;
  // 异常表
  int _exception_table_length;
  // 异常检查表
  int _checked_exceptions_length;
  // 方法参数
  int _method_parameters_length;
  // 方法签名
  int _generic_signature_index;
  // 方法注解
  int _method_annotations_length;
  int _parameter_annotations_length;
  int _type_annotations_length;
  int _default_annotations_length;
  ...
}
```

在创建`ConstMethod`实例时，上面的一些属性值会保存到`ConstMethod`实例中，因此需要开辟相应的存储空间。`ConstMethod`实例的内存布局如图6-2所示。

![image](https://github.com/YangLuchao/img_host/raw/master/20230524/image.4luvggk7zcm0.jpg)

图6-2　`ConstMethod`实例的内存布局

方法的字节码存储在紧挨着`ConstMethod`本身占用的内存空间之后，在方法解释运行时会频繁从此处读取字节码信息。

调用`Method::allocate()`函数分配内存，代码如下：

```cpp
// 来源：openjdk/hotspot/src/share/vm/oops/method.cpp

Method* Method::allocate(ClassLoaderData* loader_data,
                     int byte_code_size,
                     AccessFlags access_flags,
                     InlineTableSizes* sizes,
                     ConstMethod::MethodType method_type,
                     TRAPS) {
  // 为ConstMethod在元数据区Metaspace分配内存并创建ConstMethod实例
  ConstMethod* cm = ConstMethod::allocate(loader_data,
                                    byte_code_size,
                                    sizes,
                                    method_type,
                                    CHECK_NULL);
  // 为Method在元数据区Metaspace分配内存并创建Method实例
  // 此实例中保存有对ConstMethod实例的引用
  int size = Method::size(access_flags.is_native());
  return new (loader_data, size, false, MetaspaceObj::MethodType, THREAD) Method(cm, access_flags, size);
}
```

在`Method::allocate()`函数中调用`ConstMethod::allocate()`函数的实现代码如下：

```cpp
// 来源：openjdk/hotspot/src/share/vm/oops/constMethod.cpp
ConstMethod* ConstMethod::allocate(ClassLoaderData* loader_data,
                              int byte_code_size,
                              InlineTableSizes* sizes,
                              MethodType method_type,
                              TRAPS) {
  int size = ConstMethod::size(byte_code_size, sizes);
  return new (loader_data, size, true, MetaspaceObj::ConstMethodType,
THREAD) ConstMethod(byte_code_size, sizes, method_type, size);
}
```

在使用`new`关键字创建`ConstMethod`和`Method`实例时，需要分别调用`ConstMethod::size()`和`Method::size()`函数获取需要的内存空间。

方法的属性是不可变部分，会存储到`ConstMethod`实例中，因此在调用`ConstMethod::size()`函数时需要传递字节码大小`byte_code_size`与其他属性的`sizes`，代码如下：

```cpp
// 来源：openjdk/hotspot/src/share/vm/oops/constMethod.cpp

int ConstMethod::size(int code_size,InlineTableSizes* sizes) {
  int extra_bytes = code_size;
  if (sizes->compressed_linenumber_size() > 0) {
   extra_bytes += sizes->compressed_linenumber_size();
  }
  if (sizes->checked_exceptions_length() > 0) {
   extra_bytes += sizeof(u2);
   extra_bytes += sizes->checked_exceptions_length() * sizeof(CheckedExceptionElement);
  }
  if (sizes->localvariable_table_length() > 0) {
   extra_bytes += sizeof(u2);
   extra_bytes += sizes->localvariable_table_length() * sizeof(LocalVariableTableElement);
  }
  if (sizes->exception_table_length() > 0) {
   extra_bytes += sizeof(u2);
   extra_bytes += sizes->exception_table_length() * sizeof(ExceptionTableElement);
  }
  if (sizes->generic_signature_index() != 0) {
   extra_bytes += sizeof(u2);
  }
  if (sizes->method_parameters_length() > 0) {
   extra_bytes += sizeof(u2);
   extra_bytes += sizes->method_parameters_length() * sizeof(MethodParametersElement);
  }

  extra_bytes = align_size_up(extra_bytes, BytesPerWord);

  if (sizes->method_annotations_length() > 0) {
   extra_bytes += sizeof(AnnotationArray*);
  }
  if (sizes->parameter_annotations_length() > 0) {
   extra_bytes += sizeof(AnnotationArray*);
  }
  if (sizes->type_annotations_length() > 0) {
   extra_bytes += sizeof(AnnotationArray*);
  }
  if (sizes->default_annotations_length() > 0) {
   extra_bytes += sizeof(AnnotationArray*);
  }

  int extra_words = align_size_up(extra_bytes, BytesPerWord) / BytesPerWord;
  // 内存大小的单位为字
  return align_object_size(header_size() + extra_words);
}

static int header_size() {
   return sizeof(ConstMethod)/HeapWordSize;
}
```

调用`header_size()`函数获取`ConstMethod`本身需要占用的内存空间，然后加上`extra_words`就是需要开辟的内存空间，单位为字。

通过调用重载的`new`运算符函数分配内存，代码如下：

```cpp
// 来源：openjdk/hotspot/src/share/vm/memory/allocation.cpp

void* MetaspaceObj::operator new(size_t size, ClassLoaderData* loader_data,
                            size_t word_size, bool read_only,
                            MetaspaceObj::Type type, TRAPS) throw() {
  return Metaspace::allocate(loader_data, word_size, read_only,type,
CHECK_NULL);
}
```

HotSpot VM中的`ConstMethod`实例存储在元数据区。==调用`ConstMethod`类的构造函数初始化相关属性==，代码如下：

```cpp
// 来源：openjdk/hotspot/src/share/vm/oops/constMethod.cpp

ConstMethod::ConstMethod(int byte_code_size,
                     InlineTableSizes* sizes,
                     MethodType method_type,
                     int size) {

  ..
  set_code_size(byte_code_size);
  // 将`_constMethod_size`属性的值设置为`ConstMethod`实例的大小
  set_constMethod_size(size);
  set_inlined_tables_length(sizes);
  set_method_type(method_type);
  ..
}
```

将`_constMethod_size`属性的值设置为`ConstMethod`实例的大小，其他大部分属性的值初始化为0或NULL。

调用`Method::size()`函数计算`Method`实例所需要分配的内存空间，代码如下：

```cpp
// 来源：openjdk/hotspot/src/share/vm/oops/method.cpp
int Method::size(bool is_native) {
  // 如果是本地方法，还需要为本地方法开辟保存native_function和signature_handler属性值的内存空间
  int extra_bytes = (is_native) ? 2*sizeof(address*) : 0;
  int extra_words = align_size_up(extra_bytes, BytesPerWord) / BytesPerWord;
  // 返回的是字大小
  return align_object_size(header_size() + extra_words);     
}

// 来源：openjdk/hotspot/src/share/vm/oops/method.hpp
static int header_size() {
  return sizeof(Method)/HeapWordSize;
}
```

如果是本地方法，`Method`还需要负责保存`native_function`和`signature_handler`属性的信息，因此需要在`Method`本身占用的内存空间之后再开辟两个指针大小的存储空间。Method实例在表示本地方法时的内存布局如图6-3所示。

![image](https://github.com/YangLuchao/img_host/raw/master/20230524/image.6ev9j34qh0o0.jpg)

图6-3　表示本地方法的`Method`实例的内存布局

计算出`Method`实例需要的内存空间后同样会在元数据区分配内存并调用构造函数创建`Method`实例。构造函数的实现代码如下：

```cpp
// 来源：openjdk/hotspot/src/share/vm/oops/method.cpp

Method::Method(ConstMethod* xconst, AccessFlags access_flags, int size) {
  set_constMethod(xconst);
  set_access_flags(access_flags);
  set_method_size(size);
  set_intrinsic_id(vmIntrinsics::_none);
  ...
  // 表示vtable
  set_vtable_index(Method::garbage_vtable_index);

  set_interpreter_entry(NULL);
  set_adapter_entry(NULL);
  clear_code();

  ...
}
```

==`Method`实例中通过`_constMethod`属性保存对`ConstMethod`实例的引用；通过`_method_size`属性保存`Method`实例的大小；设置`_vtable_index`为`garbage_vtable_index`，表示`vtable`还不可用。==

# 保存方法解析信息

在创建完`Method`与`ConstMethod`实例后，会在`ClassFileParser::parse_method()`函数中设置相关的属性，代码如下：

```cpp
// 来源：openjdk/hotspot/src/share/vm/classfile/classFileParser.cpp

methodHandle ClassFileParser::parse_method(bool is_interface,AccessFlags *promoted_flags,TRAPS) {
    ...
  m->set_constants(_cp);
  m->set_name_index(name_index);
  m->set_signature_index(signature_index);
  
  if (args_size >= 0) {
    m->set_size_of_parameters(args_size);
  } else {
    m->compute_size_of_parameters(THREAD);
  }
  
  m->set_max_stack(max_stack);
  m->set_max_locals(max_locals);
  
  m->set_code(code_start);
    ...
}
```

以上代码将前面从`Class`文件中解析出的相关属性信息设置到`Method`实例中进行保存。其中调用了`m->set_code()`函数，其代码实现如下：

```cpp
// 来源：openjdk/hotspot/src/share/vm/oops/constMethod.hpp

void  set_code(address code) {
   return constMethod()->set_code(code);
}

void set_code(address code) {
   if (code_size() > 0) {
      memcpy(code_base(), code, code_size());
   }
}
address code_base() const {
   // 存储在ConstMethod本身占用的内存之后
   return (address) (this+1);       
}
```

当字节码的大小不为0时，调用`memcpy()`函数将字节码内容存储在紧挨着`ConstMethod`本身占用的内存空间之后。除了字节码之外，还会填充`ConstMethod`中的其他属性，因为前面已经开辟好了存储空间，所以根据解析的结果得到相应的属性值后填充即可。
