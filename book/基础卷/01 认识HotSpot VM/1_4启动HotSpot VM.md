[toc]

HotSpot VM一般通过java.exe或javaw.exe调用/jdk/src/share/bin/main.c文件中的main()函数启动虚拟机，使用Eclipse进行调试时也会调用main()函数。main()函数会执行JVM的初始化，并调用Java程序的main()方法。main()函数的调用链如下：

```c
main()				jdk/src/share/bin/main.c
JLI_Launch() 		jdk/src/share/bin/java.c
JVMInit() java_md_solinux.c
ContinueInNewThread() java.c
ContinueInNewThread0() java_md_solinux.c
pthread_join() pthread_join.c
```

# 1.main()函数

main()函数的实现代码如下：

```c
// 来源：jdk/src/share/bin/main.c
/*
argc: 参数个数
argv：参数列表
*/
int main(int argc, char **argv)
{
    int margc;
    char** margv;
    const jboolean const_javaw = JNI_FALSE;
#ifdef _WIN32
	... // 条件编译，为win系统入口做准备
#else /* *NIXES */
    margc = argc;
    margv = argv;
#endif /* WIN32 */
    // linux macos通用入口
    return JLI_Launch(margc, margv,
                   sizeof(const_jargs) / sizeof(char *), const_jargs,
                   sizeof(const_appclasspath) / sizeof(char *), const_appclasspath,
                   FULL_VERSION,
                   DOT_VERSION,
                   (const_progname != NULL) ? const_progname : *margv,
                   (const_launcher != NULL) ? const_launcher : *margv,
                   (const_jargs != NULL) ? JNI_TRUE : JNI_FALSE,
                   const_cpwildcard, const_javaw, const_ergo_class);
```

# 2.JLI_Launch()函数

JLI_Launch()函数进行了一系列必要的操作，如==libjvm.so的加载、参数解析、Classpath的获取和设置、系统属性设置、JVM初始化==等。

```c
// 来源：openjdk/jdk/src/share/bin/java.c
int JLI_Launch(int argc, char **argv,                 /* main argc, argc */
               int jargc, const char **jargv,         /* java args */
               int appclassc, const char **appclassv, /* app classpath */
               const char *fullversion,               /* full version defined */
               const char *dotversion,                /* dot version defined */
               const char *pname,                     /* program name */
               const char *lname,                     /* launcher name */
               jboolean javaargs,                     /* JAVA_ARGS */
               jboolean cpwildcard,                   /* classpath wildcard*/
               jboolean javaw,                        /* windows-only javaw */
               jint ergo                              /* ergonomics class policy */
)
{
	...
    // 初始化一个函数调用结构体
    InvocationFunctions ifn;
	...
    // 加载javaVM
    // 将前面初始化的函数调用结构体的引用传进去
    if (!LoadJavaVM(jvmpath, &ifn))
    {
        return (6);
    }
	...
    return JVMInit(&ifn, threadStackSize, argc, argv, mode, what, ret);
}
```

该函数会调用LoadJavaVM()加载libjvm.so并初始化相关参数，调用语句如下：

```c
来源：jdk/src/solaris/bin/java_md_solinux.c
/*
jvmpath: build/linux-aarch64-normal-server-slowdebug/jdk/lib/aarch64/server/libjvm.so 
&ifn:如下
*/
LoadJavaVM(jvmpath, &ifn);

// 来源：openjdk/jdk/src/share/bin/java.h
/*
 * 指向由加载 java vm 初始化的所需 jni 调用 api
 * Pointers to the needed JNI invocation API, initialized by LoadJavaVM.
 * 可以看到，结构体InvocationFunctions中定义了3个函数指针，这3个函数的实现代码在libjvm.so动态链接库中，
 * 查看LoadJavaVM()函数后就可以看到如下代码：
 */
typedef jint(JNICALL *CreateJavaVM_t)(JavaVM **pvm, void **env, void *args);
typedef jint(JNICALL *GetDefaultJavaVMInitArgs_t)(void *args);
typedef jint(JNICALL *GetCreatedJavaVMs_t)(JavaVM **vmBuf, jsize bufLen, jsize *nVMs);

typedef struct
{
    CreateJavaVM_t CreateJavaVM;
    GetDefaultJavaVMInitArgs_t GetDefaultJavaVMInitArgs;
    GetCreatedJavaVMs_t GetCreatedJavaVMs;
} InvocationFunctions;
```



# 3.JVMInit()函数

JVMInit()函数的源代码如下：

```c
源代码位置：jdk/src/solaris/bin/java_md_solinux.c
int
JVMInit(InvocationFunctions* ifn, jlong threadStackSize,
        int argc, char **argv,
        int mode, char *what, int ret)
{
    ...
    return ContinueInNewThread(ifn, threadStackSize, argc, argv, mode, what, ret);
}
```

# 4.ContinuelnNewThread()函数

JVMlnit()函数调用ContinuelnNewThread()函数的实现代码如下：

```c
int ContinueInNewThread(InvocationFunctions *ifn, jlong threadStackSize,
                        int argc, char **argv,
                        int mode, char *what, int ret)
{
	...
    { /* Create a new thread to create JVM and invoke main method */
        JavaMainArgs args;
        int rslt;
        args.argc = argc;
        args.argv = argv;
        args.mode = mode;
        args.what = what;
        args.ifn = *ifn;
		// 调用ContinueInNewThread0()函数创建一个JVM实例并执行Java主类的main()方法
        rslt = ContinueInNewThread0(JavaMain, threadStackSize, (void *)&args);
        return (ret != 0) ? ret : rslt;
    }
}

```

在调用ContinuelnNewThread0()函数时，传递了JavaMain函数指针和调用此函数需要的参数args。

# 5.ContinuelnNewThread0()函数

ContinuelnNewThread()函数调用ContinuelnNewThread0()函数的实现代码如下：

```c
int
ContinueInNewThread0(int (JNICALL *continuation)(void *), jlong stack_size, void * args) {
    int rslt;

    pthread_t tid;
    pthread_attr_t attr;
    pthread_attr_init(&attr);
    pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_JOINABLE);

    if (stack_size > 0) {
      pthread_attr_setstacksize(&attr, stack_size);
    }
	//pthread_create函数调用的是linux系统的函数创建线程
    // 来源：/usr/include/pthread.h
    if (pthread_create(&tid, &attr, (void *(*)(void*))continuation, (void*)args) == 0) {
      void * tmp;
      pthread_join(tid, &tmp);
      // 当前线程会阻塞在这里
      rslt = (int)tmp;
    } else {
      rslt = continuation(args);
    }

    pthread_attr_destroy(&attr);
    ...
    return rslt;
}
```

在Linux系统中（后面所说的Linux系统都是指基于Linux内核的操作系统）创建一个pthread_t线程，然后使用这个新创建的线程执行JavaMain()函数。==ContinuelnNewThread0()函数的第一个参数==

==int (JNICALL\*continuation)(void\*)接收的就是JavaMain()函数的指针==。

下面看一下JavaMain()函数的实现代码。

```c
int JNICALL
JavaMain(void *_args)
{
    JavaMainArgs *args = (JavaMainArgs *)_args;
    int argc = args->argc;
    char **argv = args->argv;
    int mode = args->mode;
    char *what = args->what;
    InvocationFunctions ifn = args->ifn;

    JavaVM *vm = 0;
    JNIEnv *env = 0;
    jclass mainClass = NULL;
    jclass appClass = NULL; // actual application class being launched
    jmethodID mainID;
    jobjectArray mainArgs;
    int ret = 0;
    jlong start = 0, end = 0;

    RegisterThread();

	//InitializeJVM()函数初始化JVM,给JavaVM和JNIEnv对象正确赋值，通过调用
	//InvocationFunctions结构体下的CreateJavaVM()函数指针来实现，该指针在
	//LoadJavaVM()函数中指向libjvm.so动态链接库中的JNI CreateJavaVM()函数
    /* Initialize the virtual machine */
    start = CounterGet();
    if (!InitializeJVM(&vm, &env, &ifn))
    {
        JLI_ReportErrorMessage(JVM_ERROR1);
        exit(1);
    }
	...
    // 加载Java主类
    mainClass = LoadMainClass(env, mode, what);
	...
    appClass = GetApplicationClass(env);
    ...
    // 从Java主类中查找main方法对应的唯一id
    mainID = (*env)->GetStaticMethodID(env, mainClass, "main",
                                       "([Ljava/lang/String;)V");
   	// 创建针对平台的参数数组
    mainArgs = CreateApplicationArgs(env, argv, argc);
	...
    // 调用main方法
    (*env)->CallStaticVoidMethod(env, mainClass, mainID, mainArgs);
	...
    ret = (*env)->ExceptionOccurred(env) == NULL ? 0 : 1;
    LEAVE();
}

```

以上代码主要是找出Java主类的main()方法，然后调用并执行。

1. ==调用InitializeJVM()函数初始化JVM,主要是初始化两个非常重要的变量JavaVM与JNIEnV==。在这里不过多介绍，后面在讲解JNI调用时会详细介绍初始化过程。
2. ==调用LoadMainClass()函数获取Java程序的启动类。==对于前面举的实例来说，由于配置了参数com.classloading/Test,所以会查找com.classloading.Test类。LoadMainClass()函数最终会调用libjvm.so中实现的JVM_FindClassFromBootLoader()函数来查找启动类，因涉及的逻辑比较多，后面在讲解类型的加载时会介绍。
3. ==调用GetStaticMethodld()函数查找Java启动方法，其实就是获取Java主类中的main()方法。==
4. ==调用JNIEnv中定义的CallStaticVoidMethod()函数，最终会调用JavaCalls::call()函数执行Java主类中的main()方法==。JavaCalls:call()函数是个非常重要的方法，后面在讲解方法执行引擎时会详细介绍。

以上步骤都在当前线程的控制下。当控制权转移到Java主类中的main()方法之后，当前线程就不再做
其他事情了，等main()方法返回之后，当前线程会清理和关闭JVM,调用本地函数jni_DetachCurrentThread()断开与主线程的连接。当成功与主线程断开连接后，当前线程会一直等待程序中的所有非守护线程全部执行结束，然后调用本地函数jni_DestroyJavaVM()对JVM执行销毁。
