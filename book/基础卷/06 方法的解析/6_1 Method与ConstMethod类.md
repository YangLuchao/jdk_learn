[toc]

==HotSpot VM通过`Method`与`ConstMethod`类保存方法的元信息。`Method`用来保存方法中的一些常见信息，如运行时的解释入口和编译入口，而`ConstMethod`用来保存方法中的不可变信息，如Java方法的字节码==。本节将详细介绍这两个类。

# Method类

`Method`类没有子类，其类继承关系如图6-1所示。

![image](https://github.com/YangLuchao/img_host/raw/master/20230524/image.3kse2y38nhk0.jpg)

图6-1　`Method`类的继承关系

==`Method`实例表示一个`Java`方法，因为一个应用有成千上万个方法，所以保证`Method`类在内存中的布局紧凑非常重要。为了方便回收垃圾，`Method`把所有的指针变量和方法都放在了`Method`内存布局的前面。==

==Java方法本身的不可变数据如字节码等用`ConstMethod`表示，可变数据如`Profile`统计的性能数据等用`MethodData`表示，它们都可以在`Method`中通过指针访问。==

==如果是本地方法，`Method`实例的内存布局的最后是`native_function`和`signature_handler`属性，按照解释器的要求，这两个属性必须在固定的偏移处==。

`Method`类中声明的属性如下：

```cpp
// 来源：openjdk/hotspot/src/share/vm/oops/method.hpp

class Method : public Metadata {
 friend class VMStructs;
 private:
  // 方法的不可变数据，如字节码
  // ConstMethod指针，定义在constMethod.hpp文件中，用于表示方法的不可变的部分，如方法ID、方法的字节码大小、方法名在常量池中的索引等
  ConstMethod*      _constMethod;
    
  // 方法的可变数据，如性能统计Profile
  // MethodData指针，在methodData.hpp中定义，用于表示一个方法在执行期间收集的相关信息
  // 如方法的调用次数、在C1编译期间代码循环和阻塞的次数、Profile收集的方法性能相关的数据等。
  // MethodData的结构基础是ProfileData,记录函数运行状态下的数据。
  // MethodData分为三部分
  //  1: 分别是函数类型运行状态下的相关统计数据
  //  2: 参数类型运行状态下的相关统计数据
  //  3: extra扩展区保存的deoptimization的相关信息
  MethodData*       _method_data;
    
  // MethodCounters指针，在methodCounters.hpp中定义，用于与大量编译、优化相关的计数，
  // 例如：
  // ·解释器调用次数：
  // ·解释执行时由于异常而终止的次数：
  // ·方法调用次数（method里面有多少方法调用）;
  // ·回边个数：
  // ·Java方法运行过的分层编译的最高层级（由于HotSpot VM中存在分层编译，所以一个方法可能会被编译器编译为不同的层级，_method_counters只会记运行过的最高层级）:
  // ·热点方法计数
  // ·基于调用频率的热点方法的跟踪统计
  MethodCounters*   _method_counters;
  
  // AccessFlags类，表示方法的访问控制标识
  AccessFlags       _access_flags;
  // 当前Method实例表示的Java方法在vtable表中的索引
  int              _vtable_index;

  // 当前Method实例的大小，以字为单位
  u2              _method_size;
  
  // 固有方法的ID。
  // 固有方法（Intrinsic Method)在HotSpot VM中表示一些众所周知的方法，针对它们可以做特别处理，生成独特的代码例程。
  // HotSpot VM发现一个方法是固有方法时不会对字节码进行解析执行，而是跳到独特的代码例程上执行，这要比解析执行更高效
  u1              _intrinsic_id;

  // 以下5个属性对于Java方法的解释执行和编译执行非常重要
  // 指向字节码解释执行入口
  address _i2i_entry;
  // 指向该Java方法的签名（`signature`）所对应的`i2c2i adapter stub`
  AdapterHandlerEntry*   _adapter;
  volatile address _from_compiled_entry;
  // 当一个方法被JIT编译后会生成一个`nmethod`，`code`指向的是编译后的代码
  nmethod* volatile  _code;
  // `_from_interpreted_entry`的初始值与`_i2i_entry`一样，都是指向字节码解释执行的入口
  volatile address  _from_interpreted_entry;
  ...
}
```

==Method类中定义的最后5个属性对于方法的解释执行和编译执行非常重要==。

- `_i2i_entry`：==指向字节码解释执行的入口==。
- `_adapter`：==指向该Java方法的签名（`signature`）所对应的`i2c2i adapter stub`==。当需要`c2i adapter stub`或`i2c adapter stub`的时候，调用`_adapter`的`get_i2c_entry()`或`get_c2i_entry()`函数获取。
- `_from_compiled_entry`：==`_from_compiled_entry`的初始值指向`c2i adapter stub`，也就是以编译模式执行的`Java`方法在调用需要以解释模式执行的`Java`方法时，由于调用约定不同，所以需要在转换时进行适配，而`_from_compiled_entry`指向这个适配的例程入口==。一开始`Java`方法没有被`JIT`编译，需要在解释模式下执行。当该方法被`JIT`编译并“安装”完成后，`_from_compiled_entry`会指向编译出来的机器码入口，具体说是指向`verified entry point`。如果要抛弃之前编译好的机器码，那么`_from_compiled_entry`会恢复为指向`c2i adapter stub`。
- `code`：==当一个方法被JIT编译后会生成一个`nmethod`，`code`指向的是编译后的代码==。
- `_from_interpreted_entry`：==`_from_interpreted_entry`的初始值与`_i2i_entry`一样，都是指向字节码解释执行的入口==。但当该Java方法被`JIT`编译并“安装”之后，`_from_interpreted_entry`就会被设置为指向`i2c adapter stub`。如果因为某些原因需要抛弃之前已经编译并安装好的机器码，则`_from_interpreted_entry`会恢复为指向`_i2i_entry`。如果有`_code`，则通过_`from_interpreted_entry`转向编译方法，否则通过`_i2i_entry`转向解释方法。

除了以上5个属性以外，Method类中的其他属性如表6-1所示。

表6-1　Method类中的部分属性说明

![image](https://github.com/YangLuchao/img_host/raw/master/20230524/image.4kufx00y4qa0.jpg)

另外，方法的访问标志_access_flags的取值如表6-2所示。

表6-2　方法的访问标志

![image](https://github.com/YangLuchao/img_host/raw/master/20230524/image.5kmcnahq8wg0.jpg)

_vtable_index的取值如表6-3所示。

表6-3　_vtable_index的取值说明

![image](https://github.com/YangLuchao/img_host/raw/master/20230524/image.4s70jbm7cuq0.jpg)

# ConstMethod类

ConstMethod实例用于保存方法中不可变部分的信息，如方法的字节码和方法参数的大小等。ConstMethod类的定义如下：

```cpp
// 来源：openjdk/hotspot/src/share/vm/oops/constMethod.hpp

class ConstMethod : public MetaspaceObj {
  ...
private:
  ...
  // 保存对常量池的引用
  ConstantPool*      _constants;

  // ConstMethod实例的大小，通过调用ConstMethod:size()函数获取
  int               _constMethod_size;
  
  // 访问标识符
  u2                _flags;

  // 方法的字节码所占用的内存大小，以字节为单位
  u2                _code_size;
  // 方法名称在常量池中的索引
  u2                _name_index;
  // 方法签名在常量池中的索引
  u2                _signature_index;
  // 对于方法来说，这是唯一ID,这个ID的值通常是methods数据的下标索弓
  u2                _method_idnum;

  // 栈的最大深度
  u2                _max_stack;
  // 本地变量表的最大深度
  u2                _max_locals;
  // 方法参数的大小，以字为单位
  u2                _size_of_parameters;
  ...
}
```

类中定义的相关属性的说明如表6-4所示。

表6-4　`ConstMethod`类中的属性说明

![image](https://github.com/YangLuchao/img_host/raw/master/20230524/image.136wnvlt1t40.jpg)

通过`_constants`和`_method_idnum`这两个参数可以找到对应的`Method`实例，因为`Method`有`ConstMethod`指针，但`ConstMethod`没有`Method`指针，需要通过以下步骤查找：

==`ConstantPool → InstanceKlass → Method`数组，通过`_method_idnum`获取对应的`Method`实例的指针。==
