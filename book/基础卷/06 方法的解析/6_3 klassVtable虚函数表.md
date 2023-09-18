

[toc]

==`klassVtable`与`klassItable`类用来实现Java方法的多态，也可以称为动态绑定，是指在应用执行期间通过判断接收对象的实际类型，然后调用对应的方法。C++为了实现多态，在对象中嵌入了虚函数表`vtable`，通过虚函数表来实现运行期的方法分派，Java也通过类似的虚函数表实现Java方法的动态分发。==

# klassVtable类

C++中的`vtable`只包含虚函数，非虚函数在编译期就已经解析出正确的方法调用了。==Java的vtable除了虚方法之外还包含其他的非虚方法。==

访问`vtable`需要通过`klassVtable`类，`klassVtable`类的定义及属性声明如下：

```cpp
// 来源：openjdk/hotspot/src/share/vm/oops/klassVtable.hpp

class klassVtable : public ResourceObj {
  // 该`vtable`所属的`Klass`，`klassVtable`操作的是`_klass`的`vtable`
  KlassHandle  _klass;
  // vtable在Klass实例内存中的偏移量
  int         _tableOffset;
  // `vtable`的长度，即`vtableEntry`的数量
  int         _length;
  ...
}
```

属性介绍如下：

- `_klass`：该`vtable`所属的`Klass`，`klassVtable`操作的是`_klass`的`vtable`；
- `_tableOffset`：vtable在Klass实例内存中的偏移量；
- `_length`：`vtable`的长度，即`vtableEntry`的数量。因为一个`vtableEntry`实例只包含一个`Method*`，其大小等于字宽（一个指针的宽度），所以`vtable`的长度跟`vtable`以字宽为单位的内存大小相同。

==`vtable`表示由一组变长（前面会有一个字段描述该表的长度）连续的`vtableEntry`元素构成的数组。其中，每个`vtableEntry`封装了一个`Method`实例。==

`vtable`中的一条记录用`vtableEntry`表示，定义如下：

```cpp
// 来源：openjdk/hotspot/src/share/vm/oops/klassVtable.hpp

class vtableEntry VALUE_OBJ_CLASS_SPEC {
  ...
 private:
  Method* _method;
  ...
};
```

其中，`vtableEntry`类只定义了一个`_method`属性，说明只是对`Method*`做了简单包装。

==`klassVtable`类提供了操作`vtable`的方法，例如`method_at()`函数的实现代码如下：==

```cpp
// 来源：openjdk/hotspot/src/share/vm/oops/klassVtable.hpp

// 获取索引为i处存储的方法
inline Method* klassVtable::method_at(int i) const {
  return table()[i].method();
}

vtableEntry* table() const {
   return (vtableEntry*)(address(_klass()) + _tableOffset);
}
```

==以上代码是基于`vtable`的内存起始地址和内存偏移基础上实现的==。

`vtable`在`Klass`中的内存布局如图6-4所示。

![image](https://github.com/YangLuchao/img_host/raw/master/20230524/image.2vcrvlu4jk60.jpg)

图6-4　`vtable`在`Klass`中的内存布局

可以看到，在Klass本身占用的内存大小之后紧接着存储的就是`vtable`（灰色区域）。通过`klassVtable`的`_tableOffset`能够快速定位到存储`vtable`的首地址，而`_length`属性也指明了存储`vtableEntry`的数量。

==在类初始化时，HotSpot VM将复制父类的`vtable`，然后根据自己定义的方法更新`vtableEntry`实例，或向`vtable`中添加新的`vtableEntry`实例==。==当Java方法重写父类方法时，HotSpot VM将更新`vtable`中的`vtableEntry`实例，使其指向覆盖后的实现方法==；==如果是方法重载或者自身新增的方法，HotSpot VM将创建新的`vtableEntry`实例并按顺序添加到`vtable`中==。==尚未提供实现的Java方法也会放在vtable中，由于没有实现，所以HotSpot VM没有为这个`vtableEntry`项分发具体的方法==。

在7.3.3节中介绍常量池缓存时会介绍`ConstantPoolCacheEntry`。==在调用类中的方法时，HotSpot VM通过`ConstantPoolCacheEntry`的`_f2`成员获取`vtable`中方法的索引，从而取得`Method`实例以便执行。常量池缓存中会存储许多方法运行时的相关信息，包括对vtable信息的使用==。

# 计算vtable的大小

==`parseClassFile()`函数解析完`Class`文件后会创建`InstanceKlass`实例保存`Class`文件解析出的类元信息，因为`vtable`和`itable`是内嵌在`Klass`实例中的，在创建`InstanceKlass`时需要知道创建的实例的大小，因此必须要在`ClassFileParser::parseClassFile()`函数中计算`vtable`和`itable`所需要的大小。==下面介绍所需要的`vtable`大小的计算过程，6.4.2节将介绍所需要的`itable`大小的计算过程。

在`ClassFileParser::parseClassFile()`函数中计算`vtable`的大小，代码如下：

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
    int   vtable_size = 0;
    int   itable_size = 0;
    int   num_miranda_methods = 0;
    GrowableArray<Method*>  all_mirandas(20);
    InstanceKlass*          tmp = super_klass();
    // 计算虚函数表的大小和mirandas方法的数量
    klassVtable::compute_vtable_size_and_num_mirandas(
                 &vtable_size,
                 &num_miranda_methods,
                 &all_mirandas,
                 tmp,
                 methods,
                 access_flags,
                 class_loader,
                 class_name,
                 local_interfaces,
                 CHECK_(nullHandle)
              );
    ...
}
```

==调用`ClassFileParser::parseClassFile()`函数时传递的`methods`就是调用`parse_methods()`函数后的返回值，数组中存储了类或接口中定义或声明的所有方法，不包括父类和实现的接口中的任何方法。==

调用`compute_vtable_size_and_num_mirandas()`函数的实现代码如下：

```cpp
// 来源：openjdk/hotspot/src/share/vm/oops/klassVtable.cpp

void klassVtable::compute_vtable_size_and_num_mirandas(
          int* vtable_length_ret,
          int* num_new_mirandas,
          GrowableArray<Method*>* all_mirandas,
          Klass* super,
          Array<Method*>* methods,
          AccessFlags class_flags,
          Handle classloader,
          Symbol* classname,
          Array<Klass*>* local_interfaces,
          TRAPS
) {
  int vtable_length = 0;

  InstanceKlass* superklass = (InstanceKlass*)super;
  // 获取父类vtable的大小，并将当前类的vtable 的大小暂时设置为父类vtable的大小
  vtable_length = super == NULL ? 0 : superklass->vtable_length();

  // 循环遍历当前Java类或接口的每一个方法 ，调用needs_new_vtable_entry()函数进行判断。如果判断的结果是true ，则将vtable_length的大小加1
  int len = methods->length();
  for (int i = 0; i < len; i++) {
   methodHandle mh(THREAD, methods->at(i));
   if (needs_new_vtable_entry(mh, super, classloader, classname, class_flags, THREAD)) {
      // 需要在vtable中新增一个vtableEntry
      vtable_length += vtableEntry::size();
   }
  }

  GrowableArray<Method*> new_mirandas(20);
  // 计算miranda方法并保存到new_mirandas或all_mirandas中
  get_mirandas(&new_mirandas, all_mirandas, super, methods, NULL, local_interfaces);
  *num_new_mirandas = new_mirandas.length();

  // 只有类才需要处理miranda方法，接口不需要处理
  if (!class_flags.is_interface()) {
   // miranda方法也需要添加到vtable中
   vtable_length += *num_new_mirandas * vtableEntry::size();
  }

  // 处理数组类时，其vtable_length应该等于Object的vtable_length，通常为5，因为Object中有5个方法需要动态绑定
  if (Universe::is_bootstrapping() && vtable_length == 0) {
    vtable_length = Universe::base_vtable_size();
  }

  *vtable_length_ret = vtable_length;
}
```

==`vtable`通常由三部分组成：父类`vtable`的大小+当前方法需要的`vtable`的大小+`miranda`方法需要的大小。==

> 这5个方法分别为：`finalize()`、`equals()`、`toString()`、`hashCode()`和`clone()`

==接口的`vtable_length`等于父类`vtable_length`，接口的父类为`Object`，因此`vtable_length`为5。如果为类，还需要调用`needs_new_vtable_entry()`函数和`get_mirandas()`函数进行计算，前一个函数计算当前类或接口需要的`vtable`的大小，后一个函数计算`miranda`方法需要的大小。==

## needs_new_vtable_entry()函数

==循环处理当前类中定义的方法，调用`needs_new_vtable_entry()`函数判断此方法是否需要新的`vtableEntry`，因为有些方法可能不需要新的`vtableEntry`，如重写父类方法时，当前类中的方法只需要更新复制父类`vtable`中对应的`vtableEntry`即可==。由于`needs_new_vtalbe_entry()`函数的实现逻辑比较多，我们分为三部分解读，第一部分的代码如下：

```cpp
// 来源：openjdk/hotspot/src/share/vm/oops/klassVtable.cpp

bool klassVtable::needs_new_vtable_entry(methodHandle target_method,
                                  Klass*      super,
                                  Handle      classloader,
                                  Symbol*     classname,
                                  AccessFlags  class_flags,
                                  TRAPS) {
  // 接口不需要新增vtableEntry项
  if (class_flags.is_interface()) {
   return false;
  }
  // final方法不需要一个新的entry，因为final方法是静态绑定的。如果final方法覆写了父类方法那么只需要更新对应父类的vtableEntry即可
  if (target_method->is_final_method(class_flags) ||
      // 静态方法不需要一个新的entry
     (target_method()->is_static() ) ||  
     // <init>方法不需要被动态绑定
     (target_method()->name() ==  vmSymbols::object_initializer_name())
  ){
     return false;
  }

  ...

  // 逻辑执行到这里，说明target_method是一个非final、非<init>的实例方法
  // 如果没有父类，则一定不存在需要更新的vtableEntry一定需要一个新的vtableEntry
  if (super == NULL) {
    return true;
  }

  // 私有方法需要一个新的vtableEntry
  if (target_method()->is_private()) {
    return true;
  }

  // 暂时省略对覆写方法和miranda方法的判断逻辑

  return true;
}
```

==接口也有`vtable`，这个`vtable`是从`Object`类继承而来的，不会再向`vtable`新增任何新的`vtableEntry`项==。

接着看`needs_new_vtable_entry()`函数的==第二部分的代码，主要是对覆写逻辑的判断==，代码如下：

```cpp
// 来源：openjdk/hotspot/src/share/vm/oops/klassVtable.cpp

bool klassVtable::needs_new_vtable_entry(methodHandle target_method,
                                  Klass*      super,
                                  Handle      classloader,
                                  Symbol*     classname,
                                  AccessFlags  class_flags,
                                  TRAPS) {
  ...
  ResourceMark   rm;
  Symbol*       name = target_method()->name();
  Symbol*       signature = target_method()->signature();
  Klass*        k = super;
  Method*       super_method = NULL;
  InstanceKlass  *holder = NULL;
  Method*       recheck_method =  NULL;
  while (k != NULL) {
     // 从父类（包括直接父类和间接父类）中查找name和signature都相等的方法
     super_method = InstanceKlass::cast(k)->lookup_method(name, signature);
     if (super_method == NULL) {
        // 跳出循环，后续还有miranda逻辑判断
        break;           
     }
     // 查找到的super_method既不是静态也不是private的，如果是被覆写的方法，那么不需要新的vtableEntry，复用从父类继承的vtableEntry即可
     if ((!super_method->is_static()) && (!super_method->is_private())) {
       if (superk->is_override(super_method, classloader, classname, THREAD)) {
         return false;
       	 // else keep looking for transitive overrides
       }
     }
  
     k = superk->super();
  } // end while
...
}
```

以上代码中，==调用`lookup_method()`函数搜索父类中是否有匹配`name`和`signature`的方法。如果搜索到方法，则可能是重写的情况，在重写情况下不需要为此方法新增`vtableEntry`，只需要复用从父类继承的`vtableEntry`即可；如果搜索不到方法，也不一定说明需要一个新的`vtableEntry`，因为还有`miranda`方法的情况==。

调用`lookup_method()`函数的实现代码如下：

```cpp
// 来源：openjdk/hotspot/src/share/vm/oops/klass.hpp
Method* lookup_method(Symbol* name, Symbol* signature) const {
   return uncached_lookup_method(name, signature);
}

// 来源：openjdk/hotspot/src/share/vm/oops/instanceKlass.cpp
Method* InstanceKlass::uncached_lookup_method(Symbol* name, Symbol* signature) const {
  Klass* klass = const_cast<InstanceKlass*>(this);
  bool dont_ignore_overpasses = true;
  while (klass != NULL) {
   Method* method = InstanceKlass::cast(klass)->find_method(name, signature);
   if ((method != NULL) && (dont_ignore_overpasses || !method->is_overpass())) {
     return method;
   }
   klass = InstanceKlass::cast(klass)->super();
   dont_ignore_overpasses = false;
  }
  return NULL;
}

Method* InstanceKlass::find_method(Symbol* name, Symbol* signature) const {
  return InstanceKlass::find_method(methods(), name, signature);
}

Method* InstanceKlass::find_method(Array<Method*>* methods, Symbol* name,
Symbol* signature) {
  int hit = find_method_index(methods, name, signature);
  return hit >= 0 ? methods->at(hit): NULL;
}
```

==`methods`数组会在方法解析完成后进行排序，因此`find_method_index()`函数会对`methods`数组进行二分查找来搜索名称为`name`和签名为`signature`的方法==。

==在`needs_new_vtable_entry()`函数代码的第二部分，如果找到`name`和`signature`都匹配的父类方法，还需要调用`is_override()`函数判断是否可覆写==，实现代码如下：

```cpp
// 来源：openjdk/hotspot/src/share/vm/oops/instanceKlass.cpp

bool InstanceKlass::is_override(methodHandle super_method, Handle targetclassloader,Symbol* targetclassname, TRAPS) {
  // 私有方法不能被覆写
  if (super_method->is_private()) {
    return false;
  }
  // 父类中的public和protected方法一定可以被覆写
  if ((super_method->is_protected()) || (super_method->is_public())) {
    return true;
  }
  // default访问权限的方法必须要和目标方法处在同一个包之下
  return(is_same_class_package(targetclassloader(), targetclassname));
}
```

【实例6-1】　在com.classloading.test1包下创建Test2类，代码如下：

```Java
package com.classloading.test1;

import com.classloading.test2.Test3;

public class Test2 extends Test3 {}
```

在com.classloading.test2包下创建Test1和Test3类，代码如下：

```Java
package com.classloading.test2;
import com.classloading.test2.Test2;
class Test3 {
   void md(){}
}

public class Test1 extends Test2{
   void md(){}
}
```

`Test1`类中的`md()`方法覆写了`Test3`类中的`md()`方法，两个类处在同一个包下，所以不需要在`Test1`类的`vtable`中新增加`vtableEntry`。如果Test2类的定义如下：

```java
package com.classloading.test1;

public class Test2  {
  void md(){}
}
```

那么需要为`Test1`类中的`md()`方法新增一个`vtableEntry`，因为`Test1`与`Test2`类不在同一个包下。

接着看`needs_new_vtable_entry()`函数代码的第三部分，主要是对`miranda`方法进行判断。首先介绍一下`miranda`方法。

有一类特殊的`miranda`方法也需要实现“晚绑定”，==因此也会有`vtableEntry`。`miranda`方法是为了解决早期的HotSpot VM的一个Bug，因为早期的虚拟机在遍历Java类的方法时只会遍历类及所有父类的方法，不会遍历Java类所实现的接口里的方法，这会导致一个问题，即如果Java类没有实现接口里的方法，那么接口中的方法将不会被遍历到。为了解决这个问题，Javac等前端编译器会向Java类中添加方法，这些方法就是miranda方法。==

> 在Java中，Miranda方法是由编译器自动生成的，并且在代码中是不可见的。它是为了保证现有的实现类在接口添加新方法时不会出现编译错误。以下是一个示例代码来说明Miranda方法的概念：
>
> ```java
> interface MyInterface {
>     void method1();
>     
>     default void method2() {
>         System.out.println("Default implementation of method2");
>     }
> }
> 
> class MyClass implements MyInterface {
>     @Override
>     public void method1() {
>         System.out.println("Implementation of method1");
>     }
> }
> 
> public class Main {
>     public static void main(String[] args) {
>         MyClass obj = new MyClass();
>         obj.method1(); // Output: Implementation of method1
>         obj.method2(); // Output: Default implementation of method2
>     }
> }
> ```
>
> 在上面的示例中，`MyInterface`接口定义了两个方法：`method1`和`method2`。`method2`是一个默认方法，它提供了一个默认的实现。
>
> `MyClass`类实现了`MyInterface`接口，并重写了`method1`方法。注意，在`MyClass`类中没有实现`method2`方法，但编译器会自动生成一个Miranda方法来实现`method2`的默认行为。
>
> 在`Main`类的`main`方法中，我们创建了`MyClass`对象并调用了`method1`和`method2`方法。正如预期的那样，`method1`输出了自己的实现，而`method2`输出了默认的实现。
>
> 需要注意的是，`Miranda`方法是编译器生成的，我们无法直接在代码中访问或调用它们。它们只是编译器为了确保接口的兼容性而生成的中间方法。

【实例6-2】　有以下代码：

```java
public interface IA{
  void test();
}

public abstract class CA implements IA{
   public CA(){
      test();
  }
}
```

CA类实现了IA接口，但是并没有实现接口中定义的test()方法。以上源代码并没有任何问题，但是假如只遍历类及父类，那么是无法查找到`test()`方法的，因此早期的HotSpot VM需要Javac等编译器为CA类合成一个miranda方法，代码如下：

```java
public interface IA{
  void test();
}

public abstract class CA implements IA{
   public CA(){
      test();
  }

  // 合法的miranda方法
  public abstract void test();
}
```

==这样就解决了HotSpot VM不搜索接口的Bug。现在的虚拟机版本并不需要合成miranda方法（Class文件中不存在`miranda`方法），但是在填充类的`vtable`时，如果这个类实现的接口中存在没有被实现的方法，仍然需要在`vtable`中新增`vtableEntry`，其实也是起到了和之前一样的效果==。

`needs_new_vtable_entry()`函数代码的第三部分如下：

```cpp
// 来源：openjdk/hotspot/src/share/vm/oops/klassVtable.cpp

bool klassVtable::needs_new_vtable_entry(methodHandle target_method,
                                  Klass*      super,
                                  Handle      classloader,
                                  Symbol*     classname,
                                  AccessFlags  class_flags,
                                  TRAPS) {
  	...
	InstanceKlass *sk = InstanceKlass::cast(super);
	if (sk->has_miranda_methods()) {
   		if (sk->lookup_method_in_all_interfaces(name, signature, false) != NULL) {
      		return false;
   		}
	}
    ...
}
```

==当父类有`miranda`方法时，由于`miranda`方法会使父类有对应`miranda`方法的`vtableEntry`，而在子类中很可能不需要这个`vtableEntry`，因此调用`lookup_method_in_all_interfaces()`函数进一步判断==，代码如下：

```cpp
// 来源：openjdk/hotspot/src/share/vm/oops/instanceKlass.cpp

Method* InstanceKlass::lookup_method_in_all_interfaces(
  Symbol* name,
  Symbol* signature,
  bool skip_default_methods
) const {
  Array<Klass*>* all_ifs = transitive_interfaces();
  int num_ifs = all_ifs->length();
  InstanceKlass *ik = NULL;
  for (int i = 0; i < num_ifs; i++) {
   ik = InstanceKlass::cast(all_ifs->at(i));
   // 调用`InstanceKlass::lookup_method()`函数查找父类实现的接口中是否有名称为name、签名为signature的方法
   Method* m = ik->lookup_method(name, signature);
   if (m != NULL && m->is_public() && !m->is_static() &&
       (!skip_default_methods || !m->is_default_method())) {
     // 如果查找到了public和非静态方法则直接返回，也就是说父类的vtable中已经存在名称为`name`、签名为`signature`的方法对应的`vtableEntry`，所以当前类中可重用这个`vtableEntry`，不需要新增`vtableEntry`
     return m;
   }
  }
  return NULL;
}
```

调用`InstanceKlass::lookup_method()`函数查找父类实现的接口中是否有名称为`name`、签名为`signature`的方法，如果查找到了public和非静态方法则直接返回，也就是说父类的vtable中已经存在名称为`name`、签名为`signature`的方法对应的`vtableEntry`，所以当前类中可重用这个`vtableEntry`，不需要新增`vtableEntry`。举个例子如下：

【实例6-3】　有以下代码：

```java
interface IA {
   void test();
}

abstract class CA implements IA{ }

public abstract class MirandaTest  extends CA {
   public abstract void test();
}
```

在处理MirandaTest类的test()方法时，从CA和Object父类中无法搜索到test()方法，但是在处理CA时，由于CA类没有实现IA接口中的test()方法，所以CA类的vtable中含有代表test()方法的vtableEntry，那么MirandaTest类中的test()方法此时就不需要一个新的`vtableEntry`了，因此方法最终返回false。

## get_mirandas()函数

调用`get_mirandas()`函数的实现代码如下：

```cpp
// 来源：openjdk/hotspot/src/share/vm/oops/klassVtable.cpp

void klassVtable::get_mirandas(
  GrowableArray<Method*>* new_mirandas,
  GrowableArray<Method*>* all_mirandas,
  Klass* super,
  Array<Method*>* class_methods,
  Array<Method*>* default_methods,
  Array<Klass*>* local_interfaces
) {
  assert((new_mirandas->length() == 0) , "current mirandas must be 0");

  // 枚举当前类直接实现的所有接口
  int num_local_ifs = local_interfaces->length();
  for (int i = 0; i < num_local_ifs; i++) {
   InstanceKlass *ik = InstanceKlass::cast(local_interfaces->at(i));
   add_new_mirandas_to_lists(new_mirandas, all_mirandas,
                         ik->methods(), class_methods,
                         default_methods, super);
   // 枚举当前类间接实现的所有接口
   Array<Klass*>* super_ifs = ik->transitive_interfaces();
   int num_super_ifs = super_ifs->length();
   for (int j = 0; j < num_super_ifs; j++) {
     InstanceKlass *sik = InstanceKlass::cast(super_ifs->at(j));
     add_new_mirandas_to_lists(new_mirandas, all_mirandas,
                           sik->methods(), class_methods,
                           default_methods, super);
   }
  }
}
```

`get_mirandas()`函数遍历当前类实现的所有直接和间接的接口，然后调用`add_new_mirandas_to_lists()`函数进行处理，代码如下：

```cpp
// 来源：openjdk/hotspot/src/share/vm/oops/klassVtable.cpp

void klassVtable::add_new_mirandas_to_lists(
   GrowableArray<Method*>* new_mirandas,
   GrowableArray<Method*>* all_mirandas,
   Array<Method*>*        current_interface_methods,
   Array<Method*>*        class_methods,
   Array<Method*>*        default_methods,
   Klass*              super
){

  // 扫描当前接口中的所有方法并查找miranda方法
  int num_methods = current_interface_methods->length();
  for (int i = 0; i < num_methods; i++) {
       Method* im = current_interface_methods->at(i);
       bool is_duplicate = false;
       int num_of_current_mirandas = new_mirandas->length();

       // 如果不同接口中需要相同的miranda方法，则is_duplicate变量的值为true
       for (int j = 0; j < num_of_current_mirandas; j++) {
        Method* miranda = new_mirandas->at(j);
        if ((im->name() == miranda->name()) && (im->signature() == miranda->signature()) ){
          is_duplicate = true;
          break;
        }
       }

       // 重复的miranda方法不需要重复处理
       if (!is_duplicate) {
        // 调用`is_miranda()`函数判断此方法是否为`miranda`方法
        if (is_miranda(im, class_methods, default_methods, super)) {
          InstanceKlass *sk = InstanceKlass::cast(super);
          // 如果父类（包括直接和间接的）已经有了相同的miranda方法，则不需要再添加
          if (sk->lookup_method_in_all_interfaces(im->name(), im->signature(), false) == NULL) {
             new_mirandas->append(im);
          }
          // 为了方便miranda方法的判断，需要将所有的miranda方法保存到all_mirandas数组中
          if (all_mirandas != NULL) {
             all_mirandas->append(im);
          }
        }
       }
  } // end for
}
```

`add_new_mirandas_to_lists()`函数遍历当前类实现的接口（直接或间接）中定义的所有方法，如果这个方法还没有被判定为`miranda`方法（就是在`new_mirandas`数组中不存在），则调用`is_miranda()`函数判断此方法是否为`miranda`方法。如果是，那么还需要调用当前类的父类的`lookup_method_in_all_interfaces()`函数进一步判断父类是否也有名称为`name`、签名为`signature`的方法，如果有，则不需要向`new_mirandas`数组中添加当前类的`miranda`方法，也就是不需要在当前类中新增一个`vtableEntry`。

【实例6-4】　有以下代码：

```java
interface IA {
   void test();
}

abstract class CA implements IA{ }
```

==在处理CA类时，由于CA实现的接口IA中的`test()`方法没有对应的实现方法，所以接口中定义的`test()`方法会添加到`new_mirandas`数组中，意思就是需要在当前`CA`类的`vtable`中添加对应的`vtableEntry`。==

【实例6-5】　有以下代码：

```java
interface IA {
   void test();
}

abstract class CA implements IA{  }

interface IB {
   void test();
}
public abstract class MirandaTest  extends CA  implements IB{

}
```

==如果当前类为`MirandaTest`，那么实现的IB接口中的`test()`方法没有对应的实现方法，但是并不一定会添加到`new_mirandas`数组中，这就意味着不一定会新增`vtableEntry`，还需要调用`lookup_method_in_all_interfaces()`函数进一步判断。由于当前类的父类CA中已经有名称和签名都相等的`test()`方法对应的`vtableEntry`了，所以只需要重用此vtableEntry即可。==

调用`is_miranda()`函数的实现代码如下：

```cpp
// 来源：openjdk/hotspot/src/share/vm/oops/klassVtable.cpp

bool klassVtable::is_miranda(
  Method* m,
  Array<Method*>* class_methods,
  Array<Method*>* default_methods,
  Klass* super
) {
  if (m->is_static() || m->is_private() || m->is_overpass()) {
   return false;
  }
  Symbol* name = m->name();
  Symbol* signature = m->signature();

  if (InstanceKlass::find_instance_method(class_methods, name, signature) == NULL) {
   // 如果miranda方法没有提供默认的方法
   if ((default_methods == NULL) ||
       InstanceKlass::find_method(default_methods, name, signature) == NULL) {
     // 当前类没有父类，那么接口中定义的方法肯定没有对应的实现方法，此接口中的方法是miranda方法
     if (super == NULL) {
       return true;
     }

     // 需要从父类中找一个非静态的名称为name、签名为signauture的方法，如果是静态方法
     // 则需要继续查找，因为静态方法不参与动态绑定，也就不需要判断是否重写与实现等特性
     Method* mo = InstanceKlass::cast(super)->lookup_method(name, signature);
     while (
             mo != NULL &&
             mo->access_flags().is_static() &&
             mo->method_holder() != NULL &&
             mo->method_holder()->super() != NULL
     ){
       mo = mo->method_holder()->super()->uncached_lookup_method(name, signature);
     }
     // 如果找不到或找到的是私有方法
     // 那么说明接口中定义的方法没有对应的实现方法此接口中的方法是miranda方法
     if (mo == NULL || mo->access_flags().is_private() ) {
       return true;
     }
   }
  }
  return false;
}
```

==接口中的静态、私有等方法一定是非`miranda`方法，直接返回`false`==。==从`class_methods`数组中查找名称为`name`、签名为`signature`的方法，其中的`class_methods`就是当前分析的类中定义的所有方法==，==找不到说明没有实现对应接口中定义的方法，有可能是`miranda`方法，还需要继续进行判断==。==在判断miranda方法时传入的`default_methods`为`NULL`，因此需要继续在父类中判断==。==如果没有父类或在父类中找不到对应的方法实现，那么`is_miranda()`函数会返回true，表示是`miranda`方法==。

# vtable的初始化

6.3.2节介绍了==类在解析过程中会完成`vtable`大小的计算并且为相关信息的存储开辟对应的内存空间，也就是在`Klass`本身需要占用的内存空间之后紧接着存储`vtable`，`vtable`后接着存储`itable`。====在`InstanceKlass::link_class_impl()`函数中完成方法连接后就会继续初始化`vtable`与`itable`==，相关的调用语句如下：

```cpp
bool InstanceKlass::link_class_impl(
    instanceKlassHandle this_oop, bool throw_verifyerror, TRAPS) {
    ...
	if (!this_oop()->is_shared()) {
       // 创建并初始化klassVtable
       ResourceMark rm(THREAD);
       klassVtable* kv = this_oop->vtable();
       kv->initialize_vtable(true, CHECK_false);

       // 创建并初始化klassItable，在6.4节中将详细介绍
       klassItable* ki = this_oop->itable();
       ki->initialize_itable(true, CHECK_false);
	}
    ...
}
```

调用`vtable()`函数的实现代码如下：

```cpp
// 来源：openjdk/hotspot/src/share/vm/oops/instanceKlass.cpp

klassVtable* InstanceKlass::vtable() const {
  intptr_t* base = start_of_vtable();
  int length = vtable_length() / vtableEntry::size();
  return new klassVtable(this, base, length);
}
```

==`start_of_vtable()`函数用于获取`vtable`的起始地址，因为`vtable`存储在紧跟Klass`本身`占用的内存空间之后，所以可以轻易获取。`vtable_length()`函数用于获取`Klass`中`_vtable_len`属性的值，这个值在解析`Class`文件、创建`Klass`实例时已经计算好，这里只需要获取即可==。

6.3.1节已经介绍过klassVtable类，为了方便阅读，这里再次给出该类的定义，具体如下：

```cpp
// 来源：openjdk/hotspot/src/share/vm/oops/klassVtable.hpp

class klassVtable : public ResourceObj {
  KlassHandle  _klass;
  // `_tableOffset`表示相对`Klass`实例首地址的偏移量
  int         _tableOffset;
  // `length`表示`vtable`中存储的`vtableEntry`的数量
  int         _length;
  ...
 public:
  // 构造函数
  klassVtable(KlassHandle h_klass, void* base, int length) : _klass(h_klass) {
   _tableOffset = (address)base - (address)h_klass();
   _length = length;
  }
  ...
}
```

可以看到，==调用的构造函数中会初始化`_tableOffset`与`_length`属性。`_tableOffset`表示相对`Klass`实例首地址的偏移量，`length`表示`vtable`中存储的`vtableEntry`的数量==。

==在`vtable()`函数中调用`klassVtable`类的构造函数获取`klassVtable`实例后，再在`InstanceKlass::link_class_impl()`函数中调用`klassVtable`实例的`initialize_vtable()`函数初始化`vtable`虚函数表==，代码如下：

```cpp
// 来源：openjdk/hotspot/src/share/vm/oops/klassVtable.cpp

void klassVtable::initialize_vtable(bool checkconstraints, TRAPS) {

  KlassHandle  super(THREAD, klass()->java_super());
  int nofNewEntries = 0;
  ...
  // 父类的vtable复制到子类vtable的前面， 并返回父类vtable的长度
  int super_vtable_len = initialize_from_super(super);
  if (klass()->oop_is_array()) {
   assert(super_vtable_len == _length, "arrays shouldn't introduce new methods");
  } else {
   assert(_klass->oop_is_instance(), "must be InstanceKlass");
   InstanceKlass*   ikl = ik();
   Array<Method*>*  methods = ikl->methods();
   int              len = methods->length();
   int              initialized = super_vtable_len;

   // 第1部分：将当前类中定义的每个方法和父类比较，如果是覆写父类方法
   // 只需要更改从父类中继承的vtable对应的vtableEntry即可，否则新追加一个vtableEntry

   // 第2部分：通过接口中定义的默认方法更新vtable

   // 第3部分：添加miranda方法

   ...
  }
}
```

如果`klassVtable`所属的`Klass`实例表示非数组类型，那么执行的逻辑有三部分，需要分别处理

1. 当前类或接口中定义的普通方法
2. 默认方法
3. miranda方法。

调用`initialize_from_super()`函数的实现代码如下：

```cpp
// 来源：openjdk/hotspot/src/share/vm/oops/klassVtable.cpp

int klassVtable::initialize_from_super(KlassHandle super) {
  if (super.is_null()) {           // Object没有父类，因此直接返回
   return 0;
  } else {
   // super一定是InstanceKlass实例，不可能为ArrayKlass实例
   assert(super->oop_is_instance(), "must be instance klass");
   InstanceKlass* sk = (InstanceKlass*)super();
   klassVtable*  superVtable = sk->vtable();
   assert(superVtable->length() <= _length, "vtable too short");

   vtableEntry*  vte = table();
   // 将父类的vtable复制到子类vtable的前面
   superVtable->copy_vtable_to(vte);

   return superVtable->length();
  }
}
```

==将父类的`vtable`复制一份存储到子类`vtable`的前面，以完成继承==。调用`vtable()`与`table()`函数的实现代码如下：

```cpp
// 来源：openjdk/hotspot/src/share/vm/oops/instanceKlass.cpp
// 调用`vtable()`函数会生成一个操作`vtable`的工具类`klassVtable`，调用该类的`copy_vtable_to()`函数执行复制操作
klassVtable* InstanceKlass::vtable() const {
  intptr_t* base = start_of_vtable();
  int length = vtable_length() / vtableEntry::size();
  return new klassVtable(this, base, length);
}

// 获取vable指针
vtableEntry* table() const{
  // klass地址+偏移，强转为vtableEntry*类型
  return (vtableEntry*)( address(_klass()) + _tableOffset );
}
```

代码比较简单，调用`vtable()`函数会生成一个操作`vtable`的工具类`klassVtable`，调用该类的`copy_vtable_to()`函数执行复制操作，代码如下：

```cpp
// 来源：openjdk/hotspot/src/share/vm/oops/klassVtable.cpp
// 父类的vtable复制给子类
void klassVtable::copy_vtable_to(vtableEntry* start) {
  Copy::disjoint_words(
                     (HeapWord*)table(),
                     (HeapWord*)start,
                     _length * vtableEntry::size()
                  );
}

// 来源：openjdk/hotspot/src/share/vm/utilities/copy.hpp
static void disjoint_words(HeapWord* from, HeapWord* to, size_t count) {
   pd_disjoint_words(from, to, count);
}
// 复制
static void pd_disjoint_words(HeapWord* from, HeapWord* to, size_t count) {
  switch (count) {
  case 8:  to[7] = from[7];
  case 7:  to[6] = from[6];
  case 6:  to[5] = from[5];
  case 5:  to[4] = from[4];
  case 4:  to[3] = from[3];
  case 3:  to[2] = from[2];
  case 2:  to[1] = from[1];
  case 1:  to[0] = from[0];
  case 0:  break;
  default:
   (void)memcpy(to, from, count * HeapWordSize);
   break;
  }
}
```

代码比较简单，为了高效进行复制，还采用了一些复制技巧。

下面解读`klassVtable::initialize_vtable()`中的重要逻辑，主要分为三部分。

## 对普通方法的处理

在`klassVtable::initialize_vtable()`函数中，==复制完父类`vtable`后，接下来就是遍历当前类中的方法，然后更新或填充当前类的`vtable`==。第一部分代码如下：

```cpp
// 来源：openjdk/hotspot/src/share/vm/oops/klassVtable.cpp

void klassVtable::initialize_vtable(bool checkconstraints, TRAPS) {
    ...
    // 将当前类中定义的每个方法和父类进行比较
    // 如果是覆写父类方法，只需要更新从父类继承的vtable中对应的vtableEntry即可
    // 否则新追加一个vtableEntry
    for (int i = 0; i < len; i++) {
         HandleMark hm(THREAD);
         methodHandle methodH(THREAD, methods->at(i));
         InstanceKlass* instanceK = ik();
         // 判断是更新父类对应的`vtableEntry`还是新添加一个`vtableEntry`
         bool needs_new_entry = update_inherited_vtable(instanceK, methodH, super_vtable_len, -1, checkconstraints, CHECK);
         if (needs_new_entry) {
           // 将Method实例存储在下标索引为initialized的vtable中
           put_method_at(methodH(), initialized);
           // 在Method实例中保存自己在vtable中的下标索引
           methodH()->set_vtable_index(initialized);
           initialized++;
         }
    }
    ...
}
```

循环处理当前类中定义的普通方法，通过调用`update_inherited_vtable()`函数判断是更新父类对应的`vtableEntry`还是新添加一个`vtableEntry`，代码如下：

```cpp
// 来源：openjdk/hotspot/src/share/vm/oops/klassVtable.cpp

// 如果是方法覆写，更新当前类中复制的父类部分中对应的vtableEntry，否则函数返回true
// 表示需要新增一个vtableEntry
bool klassVtable::update_inherited_vtable(
     InstanceKlass* klass,
     methodHandle   target_method,
     int           super_vtable_len,
     int           default_index,
     bool          checkconstraints, TRAPS
){
  ResourceMark rm;
  bool allocate_new = true;

  Array<int>* def_vtable_indices = NULL;
  bool is_default = false;

  if (default_index >= 0 ) {
   is_default = true;
   def_vtable_indices = klass->default_vtable_indices();
  } else {
   // 在对普通方法进行处理时，default_index参数的值为-1
   assert(klass == target_method()->method_holder(), "caller resp.");
   // 初始化Method类中的_vtable_index属性的值为Method::nonvirtual_vtable_index（这是个枚举常量，值为-2）如果我们分配了一个新的vtableEntry，则会更新_vtable_index为一个非负值
   target_method()->set_vtable_index(Method::nonvirtual_vtable_index);
  }

  // static和<init>方法不需要动态分派
  if (target_method()->is_static() || target_method()->name() ==  vmSymbols::object_initializer_name()) {
   return false;
  }

  // 执行这里的代码时，说明方法为非静态方法或非<init>方法
  if (target_method->is_final_method(klass->access_flags())) {
   // final方法一定不需要新的vtableEntry，如果是final方法覆写了父类方法，只需要更新vtableEntry即可
   allocate_new = false;
  } else if (klass->is_interface()) {
   // 当klass为接口时，allocate_new的值会更新为false，也就是接口中的方法不需要分配vtableEntry
   allocate_new = false;
   // 当不为默认方法或没有指定itable index时，为_vtable_index赋值
   if (!is_default || !target_method()->has_itable_index()) {
      // Method::pending_itable_index是一个枚举常量，值为-9
      target_method()->set_vtable_index(Method::pending_itable_index);
   }
  }

  // 当前类没有父类时，当前方法需要一个新的vtableEntry
  if (klass->super() == NULL) {
   return allocate_new;
  }

  // 私有方法需要一个新的vtableEntry
  if (target_method()->is_private()) {
   return allocate_new;
  }

  Symbol*  name = target_method()->name();
  Symbol*  signature = target_method()->signature();

  KlassHandle  target_klass(THREAD, target_method()->method_holder());
  if (target_klass == NULL) {
    target_klass = _klass;
  }

  Handle   target_loader(THREAD, target_klass->class_loader());
  Symbol*  target_classname = target_klass->name();

  for(int i = 0; i < super_vtable_len; i++) {
   // 在当前类的vtable中获取索引下标为i的vtableEntry，取出封装的Method
   // 循环中每次获取的都是从父类继承的Method
   Method* super_method = method_at(i);
   if (super_method->name() == name && super_method->signature() == signature) {
     InstanceKlass* super_klass =  super_method->method_holder();
     if( is_default ||
        (
             // 判断super_klass中的super_method方法是否可以被重写，如果可以，
             // 那么is_override()函数将返回true
             (super_klass->is_override(super_method, target_loader,  target_classname, THREAD)) ||
             (
               // 方法可能重写了间接父类 vtable_transitive_override_version
               ( klass->major_version() >= VTABLE_TRANSITIVE_OVERRIDE_VERSION ) &&
               ( (super_klass = find_transitive_override(super_klass,
target_method, i, target_loader,target_classname, THREAD)) != (InstanceKlass*)NULL
               )
             )
         )
      ){
       // 当前的名称为name，签名为signautre代表的方法覆写了父类方法，不需要分配新
       // 的vtableEntry
       allocate_new = false;
       // 将Method实例存储在下标索引为i的vtable中
       put_method_at(target_method(), i);
       if (!is_default) {
        target_method()->set_vtable_index(i);
       } else {
        if (def_vtable_indices != NULL) {
          // 保存在def_vtable_indices中下标为default_index的Method实例与
          // 保存在vtable中下标为i的vtableEntry的对应关系
          def_vtable_indices->at_put(default_index, i);
        }
        assert(super_method->is_default_method() ||
              super_method->is_overpass() ||
              super_method->is_abstract(), "default override error");
       }
     } else {
       // allocate_new = true; default. We might override one entry,
       // but not override another. Once we override one, not need new
     }
   }
  }                                 // 结束for循环
  return allocate_new;
}
```

【实例6-6】　有以下代码：

```java
public abstract  class TestVtable  {
   public void md(){}
}
```

在TestVtable类中会遍历两个方法：

- `<init>`方法：可以看到`update_inherited_vtable()`函数对`vmSymbols::object_initializer_name()`名称的处理是直接返回`false`，表示不需要新的`vtableEntry`。
- `md()方法`：会临时给对应的`Method::_vtable_index`赋值为`Method::nonvirtual_vtable_index`，然后遍历父类，看是否定义了名称为`name`、签名为`signature`的方法，如果有，很可能不需要新的`vtableEntry`，只需要更新已有的`vtableEntry`即可。由于`TestVtable`的默认父类为`Object`，`Object`中总共有5个方法会存储到`vtable`中（分别为`finalize()`、`equals()`、`toString()`、`hashCode()`和`clone()`），很明显`md()`并没有重写父类的任何方法，直接返回`true`，表示需要为此方法新增一个`vtableEntry`。这样`Method::vtable_index`的值会更新为`initialized`，也就是在`vtable`中下标索引为`5`（下标索引从0开始）的地方将存储`md()`方法。

【实例6-7】　有以下代码：

```java
public abstract  class TestVtable  {
   public String toString(){
       return "TestVtable";
   }
}
```

上面的TestVtable类的方法共有两个：`<init>`与`toString()`。`<init>`不需要`vtableEntry`，`toString()`方法重写了`Object`类中的`toString()`方法，因此也不需要新的`vtableEntry`。`toString()`是可被重写的，在`klassVtable::update_inherited_vtable()`函数中会调用`is_override()`函数进行判断，这个函数在6.3.2节中介绍过，这里不再介绍。

调用`put_method_at()`函数更新当前类中从父类继承的`vtable`中对应的`vtableEntry`，代码如下：

```cpp
// 来源：openjdk/hotspot/src/share/vm/oops/klassVtable.cpp
// 更新当前类中从父类继承的`vtable`中对应的`vtableEntry`
void klassVtable::put_method_at(Method* m, int index) {
  vtableEntry* vte = table();
  vte[index].set(m);
}
```

## 默认方法的处理

`klassVtable::initialize_vtable()`函数的第二部分是对接口中定义的默认方法进行处理。代码如下：

```cpp
// 来源：openjdk/hotspot/src/share/vm/oops/klassVtable.cpp

void klassVtable::initialize_vtable(bool checkconstraints, TRAPS) {
  ...
  // 通过接口中定义的默认方法更新vtable
  Array<Method*>* default_methods = ik()->default_methods();
  if (default_methods != NULL) {
       len = default_methods->length();
       if (len > 0) {
         Array<int>* def_vtable_indices = NULL;
         if ((def_vtable_indices = ik()->default_vtable_indices()) == NULL) {
           def_vtable_indices = ik()->create_new_default_vtable_indices(len, CHECK);
         } else {
           assert(def_vtable_indices->length() == len, "reinit vtable len?");
         }
         for (int i = 0; i < len; i++) {
          HandleMark hm(THREAD);
          methodHandle mh(THREAD, default_methods->at(i));
  
          bool needs_new_entry = update_inherited_vtable(ik(), mh,
                           super_vtable_len, i, checkconstraints, CHECK);
  
          if (needs_new_entry) {
            put_method_at(mh(), initialized);
            // 通过def_vtable_indices保存默认方法在vtable中存储位置的对应信息
            def_vtable_indices->at_put(i, initialized);
            initialized++;
          }
         }
       } // end if(len > 0)
  }
  ...
}
```

同样会调用`update_inherited_vtable()`函数判断默认方法是否需要新的`vtableEntry`，不过传入的`default_index`的值是大于等于0的。

【实例6-8】　有以下代码：

```java
interface IA{
   default void test(){ }
}

public abstract  class TestVtable implements IA{ }
```

在处理`TestVtable`时有一个默认的方法`test()`，由于表示当前类的`InstanceKlass`实例的`_default_vtable_indices`属性为`NULL`，所以首先会调用`create_new_vtable_indices()`函数根据默认方法的数量`len`初始化属性，代码如下：

```cpp
Array<int>* InstanceKlass::create_new_default_vtable_indices(int len,
TRAPS) {
  Array<int>* vtable_indices = MetadataFactory::new_array<int>(class_loader_data(), len, CHECK_NULL);
  set_default_vtable_indices(vtable_indices);
  return vtable_indices;
}
```

对于实例6-8来说，调用`update_inherited_vtable()`函数时传入的`default_index`的值为0。由于没有重写任何父类方法，所以函数返回true，表示需要一个新的`vtableEntry`，不过还需要在`InstanceKlass::_default_vtable_indices`属性中记录映射关系。也就是说第0个默认方法要存储到下标索引为5的`vtableEntry`中。

## miranda方法的处理

`klassVtable::initialize_vtable()`函数代码的第三部分是对`miranda`方法的处理，代码如下：

```cpp
// 来源：openjdk/hotspot/src/share/vm/oops/klassVtable.cpp

void klassVtable::initialize_vtable(bool checkconstraints, TRAPS) {
  ...
  
  // 添加miranda方法
  if (!ik()->is_interface()) {
    initialized = fill_in_mirandas(initialized);
  }
  ...
}
```

调用`fill_in_mirandas()`函数处理`miranda`方法，代码如下：

```cpp
源代码位置：openjdk/hotspot/src/share/vm/oops/klassVtable.cpp

int klassVtable::fill_in_mirandas(int initialized) {
  GrowableArray<Method*> mirandas(20);
  get_mirandas(&mirandas, NULL,
               ik()->super(),
               ik()->methods(),
               ik()->default_methods(),
               ik()->local_interfaces());
  for (int i = 0; i < mirandas.length(); i++) {
   put_method_at(mirandas.at(i), initialized);
   ++initialized;
  }
  return initialized;
}
```

调用的`get_mirandas()`函数在6.3.2节中介绍过，这个方法会将当前类需要新加入的`miranda`方法添加到`mirandas`数组中，然后为这些`miranda`方法添加新的`vtableEntry`。

【实例6-9】　有以下代码：

```cpp
interface IA{
  int md();
}
public abstract  class TestVtable  implements IA {}
```

对于上例来说，`TestVtable`类没有实现IA接口中定义的`md()`方法，因此会添加到`fill_in_mirandas()`方法中定义的`mirandas`数组中。最后调用`put_method_at()`方法将`miranda`方法存放到下标索引为5的`vtableEntry`中。
