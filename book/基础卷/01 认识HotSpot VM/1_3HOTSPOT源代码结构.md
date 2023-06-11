

在介绍HotSpot VM的源代码之前，首先需要介绍一下HotSpot项目的目录结构。

![image](https://cdn.staticaly.com/gh/YangLuchao/img_host@master/20230511/image.6v9r4cbl1sk0.webp)HotSpot目录主要由agent、make、src和test4个子目录构成。其中，agent目录下包含Serviceability Agent的客户端实现；
make目录下包含用于编译HotSpot项目的各种配置文件；src目录是最重要的一个目录，本书讲解的所有源
代码都在这个目录下；test目录下包含HotSpot项目相关的一些单元测试用例。
src目录及src的子目录vm的结构分别如图1-5和图1-6所示。

![image](https://cdn.staticaly.com/gh/YangLuchao/img_host@master/20230511/image.6r80l83mjug.webp)

src目录下包含HotSpot项目的主体源代码，主要由cpu、OS、os_cpu与share 4个子目录构成。

- ==cpu目录：包含一些依赖具体处理器架构的代码==。目前主流的处理器架构主要有sparc、x86和
	zero,其中x86最为常见。笔者的计算机的CPU也是x86架构，因此当涉及相关的源代码时，只
	介绍×86目录下的源代码。
- ==os目录：包含一些依赖操作系统的代码，主要的操作系统有基于Linux内核的操作系统==、基于UNIX的操作系统（POSIX),以及Windows和Solaris。笔者的计算机是基于Linux内核的Ubuntu操作系统，在涉及相关的源代码时，只讲解Linux目录下的源代码。
- ==os_cpu目录：包含一些依赖操作系统和处理器架构的代码==，如linux_x86目录下包含的就是基于
	Linux内核的操作系统和x86处理器架构相关的代码，也是笔者要讲解的源代码。
- ==share目录：包含独立于操作系统和处理器架构的代码==，尤其是vm子目录中包含的内容比较多，HotSpot的一些主要功能模块都在这个子目录中。如表1-3所示为vm目录下一些重要的子目录。

| 目录              | 描述                         |
| ----------------- | ---------------------------- |
| adlc              | 平台描述文件                 |
| asm               | 汇编器                       |
| cl                | C1编译器，即Client编译器     |
| ci                | 动态编译器                   |
| classfile         | Class文件解析和类的链接等    |
| code              | 生成机器码                   |
| compiler          | 调用动态编译器的接口         |
| Opto              | C2编译器，即Server编译器     |
| ge                | gc interface                 |
| ge_implementation | 存放垃圾收集器的具体实现代码 |
| interpreter       | 解释器                       |
| libadt            | 抽象数据结构                 |
| memory            | 内存管理                     |
| oops              | JVM内部对象                  |
| prims             | HotSpotVM对外接口            |
| runtime           | 存放运行时的相关代码         |
| services          | JMX接口                      |
| utilizes          | 内部工具类和公共函数         |



