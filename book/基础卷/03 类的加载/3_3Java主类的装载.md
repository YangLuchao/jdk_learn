[toc]

# Java主类的装载

我们在1.4节中曾经介绍过HotSpot VM的启动过程，==启动完成后会调用`JavaMain()`函数执行Java应用程序，也就是执行Java主类的`main()`方法之前需要先在JavaMain()函数（定义在openjdk/jdk/src/share/bin/java.c文件中）中调用`LoadMainClass()`函数装载Java主类==，代码如下：

```cpp
// 来源：openjdk/jdk/src/share/bin/java.c

static jclass LoadMainClass(JNIEnv *env, int mode, char *name){

   jmethodID   mid;
   jstring     str;
   jobject     result;
   jlong       start, end;

   // 加载sun.launcher.LauncherHelper类
   jclass cls = GetLauncherHelperClass(env);

   // 获取sun.launcher.LauncherHelper类中定义的checkAndLoadMain()方法的指针
   NULL_CHECK0(mid = (*env)->GetStaticMethodID(
                    env,cls,"checkAndLoadMain","(ZILjava/lang/String;) Ljava/lang/Class;"));

   // 调用sun.launcher.LauncherHelper类中的checkAndLoadMain()方法
   str = NewPlatformString(env, name);
   result = (*env)->CallStaticObjectMethod(env, cls, mid, USE_STDERR, mode, str);

   return (jclass)result;
}
```

下面介绍以上代码中调用的一些函数。

## GetLauncherHelperClass()函数

调用`GetLauncherHelperClass()`函数的代码如下：

```cpp
// 来源：openjdk/jdk/src/share/bin/java.c
// 加载sun.launcher.LauncherHelper类
jclass GetLauncherHelperClass(JNIEnv *env){
   if (helperClass == NULL) {
       NULL_CHECK0(helperClass = FindBootStrapClass(env, "sun/launcher/LauncherHelper"));
   }
   return helperClass;
}
```

调用`FindBootStrapClass()`函数的代码如下：

```cpp
// 来源：openjdk/jdk/src/solaris/bin/java_md_commons.c
static FindClassFromBootLoader_t *findBootClass = NULL;
// 参数classname的值为"sun/launcher/LauncherHelper"。
jclass FindBootStrapClass(JNIEnv *env, const char* classname){
  if (findBootClass == NULL) {
      // 返回指向JVM_FindClassFromBootLoader()函数的函数指针
      findBootClass = (FindClassFromBootLoader_t *)dlsym(
                          RTLD_DEFAULT,"JVM_FindClassFromBootLoader");
  }
  return findBootClass(env, classname);
}
```

通过函数指针`findBootClass`来调用`JVM_FindClassFromBootLoader()`函数，代码如下：

```cpp
// 来源：openjdk/hotspot/src/share/vm/prims/jvm.cpp

JVM_ENTRY(jclass, JVM_FindClassFromBootLoader(JNIEnv* env,const char* name))

  TempNewSymbol h_name = SymbolTable::new_symbol(name, CHECK_NULL);
  Klass* k = SystemDictionary::resolve_or_null(h_name, CHECK_NULL);
  if (k == NULL) {
   return NULL;
  }

  return (jclass) JNIHandles::make_local(env, k->java_mirror());
JVM_END
```

==调用`SystemDictionary::resolve_or_null()`函数查找类`sun.launcher.LauncherHelper`，如果查找不到还会加载类==。该函数在前面已经详细介绍过，这里不再赘述。

## GetStaticMethodID()函数

==通过JNI的方式调用Java方法时，首先要获取方法的`methodID`。调用`GetStaticMethodID()`函数可以查找Java启动方法(Java主类中的main()方法)的`methodID`==。调用`GetStaticMethodID()`函数其实调用的是`jni_GetStaticMethodID()`函数，代码如下：

```cpp
// 来源：openjdk/hotspot/src/share/vm/prims/jni.cpp

// 传递的参数name为"checkAndLoadMain"，
// sig为"(ZILjava/lang/String;) Ljava/lang/Class;"，也就是checkAndLoadMain()方法的签名
JNI_ENTRY(jmethodID, jni_GetStaticMethodID(JNIEnv *env, jclass clazz,
const char *name, const char *sig))
  jmethodID ret = get_method_id(env, clazz, name, sig, true, thread);
  return ret;
JNI_END
```

调用`get_method_id()`函数的代码如下：

```cpp
// 来源：openjdk/hotspot/src/share/vm/prims/jni.cpp

static jmethodID get_method_id(JNIEnv *env, jclass clazz, const char* name_str,
                         const char *sig, bool is_static, TRAPS) {
  const char *name_to_probe = (name_str == NULL)
                     ? vmSymbols::object_initializer_name()->as_C_string()
                     : name_str;
  TempNewSymbol name = SymbolTable::probe(name_to_probe, (int)strlen(name_to_probe));
  TempNewSymbol signature = SymbolTable::probe(sig, (int)strlen(sig));
  // 确保sun.launcher.LauncherHelper类已经初始化完成
  KlassHandle klass(THREAD,java_lang_Class::as_Klass(JNIHandles::resolve_non_null(clazz)));
  klass()->initialize(CHECK_NULL);

  Method* m;
  if (// name为<init>
      name == vmSymbols::object_initializer_name() || 
     // name为<clinit>
     name == vmSymbols::class_initializer_name()) {     
   // 在查找构造函数时，只查找当前类中的构造函数，不查找超类构造函数
   if (klass->oop_is_instance()) {
     m = InstanceKlass::cast(klass())->find_method(name, signature);
   } else {
     m = NULL;
   }
  } else {
   // 在特定类中查找方法
   m = klass->lookup_method(name, signature);      
   if (m == NULL &&  klass->oop_is_instance()) {
     m = InstanceKlass::cast(klass())->lookup_method_in_ordered_interfaces(name, signature);
   }
  }
  // 获取方法对应的methodID，methodID指定后不会变，所以可以重复使用methodID
  return m->jmethod_id();
}
```

==在查找构造方法时调用了`InstanceKlass`类中的`find_method()`函数，这个函数不会查找超类；在查找普通方法时调用了Klass类中的`lookup_method()`或`InstanceKlass`类中的`lookup_method_in_ordered_interfaces()`函数，这两个函数会从父类和接口中查找==，`lookup_method()`函数的实现代码如下：

```cpp
// 来源：openjdk/hotspot/src/share/vm/oops/klass.hpp

Method* lookup_method(Symbol* name, Symbol* signature) const {
   return uncached_lookup_method(name, signature);
}

Method* InstanceKlass::uncached_lookup_method(Symbol* name, Symbol* signature) const {
  Klass* klass = const_cast<InstanceKlass*>(this);
  bool dont_ignore_overpasses = true;
  while (klass != NULL) {
   // 调用find_method()函数从当前InstanceKlass的_methods数组中查找名称和签名相同的方法
   Method* method = InstanceKlass::cast(klass)->find_method(name, signature);
   if ((method != NULL) && (dont_ignore_overpasses || !method->is_overpass())) {
     return method;
   }
   klass = InstanceKlass::cast(klass)->super();
   dont_ignore_overpasses = false;
  }
  return NULL;
}
```

==如果调用`find_method()`函数无法从当前类中找到对应的方法，那么就通过while循环一直从继承链往上查找，如果找到就直接返回，否则返回NULL==。`find_method()`函数的实现代码如下：

```cpp
// 来源：openjdk/hotspot/src/share/vm/oops/instanceklass.cpp

// 从当前的InstanceKlass中的_methods数组中查找，这个数组中只存储了当前类中定义的方法
Method* InstanceKlass::find_method(Symbol* name, Symbol* signature) const {
  return InstanceKlass::find_method(methods(), name, signature);
}

Method* InstanceKlass::find_method(Array<Method*>* methods, Symbol* name, Symbol* signature) {
  int hit = find_method_index(methods, name, signature);
  return hit >= 0 ? methods->at(hit): NULL;
}
```

==方法解析完成后会返回存储所有方法的数组，调用`ClassFileParser::sort_methods()`函数对数组排序后保存在`InstanceKlass`类的`_methods`属性中==。==调用`find_method_index()`函数使用二分查找算法在`_methods`属性中查找方法，如果找到方法，则返回数组的下标位置，否则返回`-1`==。正常情况下肯定能==找到`sun.launcher.LauncherHelper`类中的`checkAndLoadMain()`方法。找到以后，就可以在`LoadMainClass()`函数中通过`CallStaticObjectMethod()`函数调用`checkAndLoadMain()`方法==了。

## CallStaticObjectMethod()函数

在`LoadMainClass()`函数中调用`CallStaticObjectMethod()`函数会执行`sun.launcher.LauncherHelper`类的`checkAndLoadMain()`方法。`CallStaticObjectMethod()`函数会通过`jni_invoke_static()`函数执行`checkAndLoadMain()`方法。`jni_invoke_static()`函数的实现代码如下：

```cpp
// 来源：openjdk/hotspot/src/share/vm/prims/jni.cpp

static void jni_invoke_static(JNIEnv *env, JavaValue* result, jobject receiver, JNICallType call_type, jmethodID method_id, JNI_ArgumentPusher *args, TRAPS) { 
  methodHandle method(THREAD, Method::resolve_jmethod_id(method_id));

  ResourceMark rm(THREAD);
  int number_of_parameters = method->size_of_parameters();
  // 将要传给Java的参数转换为JavaCallArguments实例传下去
  JavaCallArguments java_args(number_of_parameters);
  args->set_java_argument_object(&java_args);

  // 填充JavaCallArguments实例
  args->iterate( Fingerprinter(method).fingerprint() );
  // 初始化返回类型
  result->set_type(args->get_ret_type());

  // 供C/C++程序调用Java方法
  JavaCalls::call(result, method, &java_args, CHECK);

  // 转换结果类型
  if (result->get_type() == T_OBJECT || result->get_type() == T_ARRAY) {
    result->set_jobject(JNIHandles::make_local(env, (oop) result->get_jobject()));
  }
}
```

最终==通过`JavaCalls::call()`函数调用Java方法==，在后续介绍方法执行引擎时会详细介绍`JavaCalls::call()`函数的实现细节。`jni_invoke_static()`函数看起来比较复杂，是因为JNI调用时需要对参数进行转换，在JNI环境下只能使用句柄访问HotSpot VM中的实例，因此在每次函数的开始和结束时都需要调用相关函数对参数进行转换，如调用`Method::resolve_jmethod_id()`、调用`JNIHandles::make_local()`等函数。

最后看一下调用`JavaCalls::call()`函数执行的Java方法`checkAndLoadMain()`方法的实现代码：

```java
// 来源：openjdk/jdk/src/share/classes/sun/launcher/LauncherHelper.java

public static Class<?> checkAndLoadMain(boolean printToStderr,int mode, String what) {
       initOutput(printToStderr);
       // 获取类名称
       String cn = null;
       switch (mode) {
          case LM_CLASS:
             cn = what;
             break;
          case LM_JAR:
             cn = getMainClassFromJar(what);
             break;
          default:
             throw new InternalError("" + mode + ": Unknown launch mode");
       }
       cn = cn.replace('/', '.');
       Class<?> mainClass = null;
       try {
          // 根据类名称加载主类
          mainClass = scloader.loadClass(cn);  
       } catch (NoClassDefFoundError | ClassNotFoundException cnfe) {
          ...
       }
       appClass = mainClass;
       return mainClass;
}
```

如果我们为虚拟机指定运行主类为`com.classloading/Test`，那么这个参数会被传递到`checkAndLoadMain()`方法中作为`what`参数的值。这个类最终会通过应用类加载器进行加载，也就是`AppClassLoader`。

`scloader`是全局变量，定义如下：

```java
// 来源：openjdk/jdk/src/share/classes/sun/launcher/LauncherHelper.java

private static final ClassLoader scloader = ClassLoader.getSystemClassLoader();
```

==调用`scloader`的`loadClass()`方法会调用`java.lang.ClassLoader`的l`oadClass()`方法，前面已经介绍过该方法，首先通过`findLoadedClass()`方法判断当前加载器是否已经加载了指定的类，如果没有加载并且`parent`不为NULL，则调用`parent.loadClass()`方法完成加载。而`AppClassLoader`的父加载器是`ExtClassLoader`，这是加载JDK中的扩展类，并不会加载Java主类，最终根类加载器也不会加载Java的主类（因为这个主类不在根类加载器负责加载的范围之内），因此只能调用`findClass()`方法完成主类的加载==。对于`AppClassLoader`来说，调用的是`URLClassLoader`类中实现的`findClass()`方法，该方法在前面已经详细介绍过，这里不再赘述。

==在`checkAndLoadMain()`方法中调用`AppClassLoader`类加载器的`loadClass()`方法就完成了主类的加载，后续HotSpot VM会在主类中查找main()方法然后运行main()方法。==
