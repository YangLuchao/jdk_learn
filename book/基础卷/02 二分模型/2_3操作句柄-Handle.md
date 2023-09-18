[toc]



==可以将Handle理解成访问对象的“句柄”。垃圾回收时对象可能被移动（对象地址发生改变）,通过Handle访问对象可以对使用者屏蔽垃圾回收细节==。Handle涉及的相关类的继承关系如图2-13所示。

![image](https://github.com/YangLuchao/img_host/raw/master/20230521/image.6gc8kwmfyt80.jpg)

==HotSpot会通过Handle对oop和某些Klass进行操作==。如图2-14所示为直接引用的情况，图2-15所示为间接引用的情况。

![image](https://github.com/YangLuchao/img_host/raw/master/20230521/image.375xwrlmfvi0.jpg)

![image](https://github.com/YangLuchao/img_host/raw/master/20230521/image.39fv3qu2f2w0.jpg)

可以看到，==当对oop直接引用时，如果oop的地址发生变化，那么所有的引用都要更新==，图2-14中有3处引用都需要更新；==当通过Handle对oop间接引用时，如果oop的地址发生变化，那么只需要更新Handle中保存的对oop的引用即可==。==每个oop都有一个对应的Handle,Handle继承关系与oop继承关系类似，也有对应的关系，如通过instanceHandle操作instanceOopDesc,通过objArrayHandle操作objArrayOopDesc==。oop涉及的相关类的继承关系可以参考图2-9,这里不再给出。

# 句柄Handle的定义与创建

下面具体看一下Handle的定义，代码如下：

```cpp
// 来源：hotspot/src/share/vm/runtime/handles.hpp
class Handle VALUE_OBJ_CLASS_SPEC {
 private:
  oop* _handle; 	// 对oop的封装

 protected:
  oop     obj() const                            
  { 
      return _handle == NULL ? (oop)NULL : *_handle; 
  }
  oop     non_null_obj() const                   
  { 
      return *_handle; 
  }
    ...
};
```

获取被封装的oop对象，并不会直接调用Handle对象的obj()或non_null_obj()函数，而是通过C++的运算符重载来获取。Handle类重载了`()`和`->`运算符，代码如下：

```cpp
// 来源：hotspot/src/share/vm/runtime/handles.hpp 
oop     operator () () const                   
{ 
    return obj(); 
}
  
oop     operator -> () const                   
{ 
    return non_null_obj();
}
  
bool    operator == (oop o) const              
{ 
    return obj() == o; 
}
  
bool    operator == (const Handle& h) const          
{ 
    return obj() == h.obj();                                                      
}
// 就可以这样使用：
oop		obj = ...;
Handle 	h1(obj);
oop 	obj1 = h1();
h1->print();
```

由于重载了运算符`()`,所以h1()会调用`()`运算符的重载方法，然后在重载方法中调用`obj()`函数获取被封装的`oop`对象。`h1->print()`同样会通过`->`运算符的重载方法调用`oop`对象的`print()`方法。==另外还需要知道，Handle被分配在本地线程的HandleArea中，这样在进行垃圾回收时只需要扫描每个线程的HandleArea即可找出所有Handle,进而找出所有引用的活跃对象。====每次创建句柄对象时都会调用Handle类的构造函数，其中一个构造函数如下：==

```cpp
// 来源：hotspot/src/share/vm/runtime/handles.inline.hpp
inline Handle::Handle(oop obj) {
  if (obj == NULL) {
    _handle = NULL;
  } else {
      // Handle被分配在本地线程的HandleArea中，这样在进行垃圾回收时只需要扫描每个线程的HandleArea即可找出所有Handle,进而找出所有引用的活跃对象
    _handle = Thread::current()->handle_area()->allocate_handle(obj);
  }
}
```

参数`obj`是要通过句柄操作的对象。通过调用当前线程的`handle_area()`函数获取`HandleArea`,然后调用`allocate_handle()`函数在`HandleArea`中分配存储`obj`的空间并存储`obj`。每个线程都会有一个`_handle_area`属性，定义如下：

```cpp
// 来源：hotspot/src/share/vm/runtime/thread.hpp
class Thread: public ThreadShadow {
  ...
  HandleArea* _handle_area;
  ...
}
```

在创建线程时初始化`_handle_area`属性，然后通过`handle_area()`函数获取该属性的值。`allocate_handle()`函数为对象`obj`分配了一个新的句柄，代码如下：

```cpp
// 来源：hotspot/src/share/vm/runtime/handles.hpp
oop* allocate_handle(oop obj) 
{ 
	return real_allocate_handle(obj); 
}

oop* real_allocate_handle(oop obj) {
    oop* handle = (oop*) Amalloc_4(oopSize);
    *handle = obj;
    return handle;
}
```

在代码中分配了一个新的空间并存储`obj`。句柄的释放要通过`HandleMark`来完成。在介绍`HandleMark`之前需要先了解`HandleArea`、`Area`及`Chunk`等类的实现方法，下一节内容中将会详细介绍。

# 句柄Handle的释放

==封装关系是：==

```
thread
	->	HandleMark 	->	_previous_handle_mark(形成单链表)
			->	HandleArea	->	_prev(形成单链表)
					->	Chunk	->	_next(形成单链表)
							->	Chunk
```



本节首先介绍几个与句柄分配和释放密切相关的类，然后重点介绍句柄的释放。

## 1.HandleArea、Area与Chunk

==句柄都是在HandleArea中分配并存储的==，类的定义代码如下：

```cpp
// 来源：hotspot/src/share/vm/runtime/handles.hpp
class HandleArea: public Arena {
  ...
  HandleArea* _prev;         //HandleArea通过_prev连接成单链表
 public:
  // Constructor
  HandleArea(HandleArea* prev) : Arena(mtThread, Chunk::tiny_size) {
    debug_only(_handle_mark_nesting    = 0);
    debug_only(_no_handle_mark_nesting = 0);
    _prev = prev;
  }

  // Handle allocation
  // 分配内存并存储obj对象
 private:
  oop* real_allocate_handle(oop obj) {
    oop* handle = (oop*) Amalloc_4(oopSize);
    *handle = obj;
    return handle;
  }
};
```

`real_allocate_handle()`函数在`HandleArea`中分配内存并存储`obj`对象，该函数调用父类`Arena`中定义的`Amalloc_4()`函数分配内存。Arena类的定义如下：

```cpp
class Arena: public CHeapObj {
	... 
  Chunk *_first;                //单链表的第一个Chunk
  Chunk *_chunk;                //当前正在使用的Chunk
  char *_hwm, *_max;            // High water mark and max in current chunk
public:
    ...
  void *Amalloc_4(size_t x) {
    assert( (x&(sizeof(char*)-1)) == 0, "misaligned size" );
    if (_hwm + x > _max) {
        // 分配新的Chunk块，在新的chunk块中分配内存
      return grow(x);
    } else {
      char *old = _hwm;
      _hwm += x;
      return old;
    }
  }
...
};
```

`Amalloc_4()`函数会在当前的`Chunk`块中分配内存，如果当前块的内存不够，则调用`grow()`方法分配新的`Chunk`块，然后在新的`Chunk`块中分配内存。

`Arena`类通过`_first`、`_chunk`等属性管理一个连接成单链表的`Chunk`,其中，`_frst`指向单链表的第一个`Chunk`,而`_chunk`指向的是当前可提供内存分配的`Chunk`,通常是单链表的最后一个`Chunk`。`_hwm`与`_max`指示当前可分配内存的`Chunk`的一些分配信息。`Chunk`类的定义代码如下：

```cpp
class Chunk: CHeapObj<mtChunk> {
    ...
 protected:
  Chunk*       _next;   	//单链表的下一个Chunk 
  const size_t _len;     	//当前Chunk的大小
	...
  char* bottom() const          
  { 
      return ((char*) this) + aligned_overhead_size();  
  }
    
  char* top()    const          
  { 
      return bottom() + _len; 
  }
  ...
};

```

HandleArea与Chunk类之间的关系如图2-16所示。

![image](https://github.com/YangLuchao/img_host/raw/master/20230521/image.3w4bsgypa660.jpg)

图2-16已经清楚地展示了`HandleArea`与`Chunk`的关系，灰色部分表示在`Chunk`中已经分配的内存，那么新的内存分配就可以从`_hwm`开始。现在看`Amalloc_4()`函数的逻辑就非常容易理解了，这个函数还会调用`grow()`函数分配新的`Chunk`块，代码如下：

```cpp
// 来源：hotspot/src/share/vm/adlc/arena.cpp
void* Arena::grow( size_t x ) {

  size_t len = max(x, Chunk::size);

  register Chunk *k = _chunk;   
  _chunk = new (len) Chunk(len);

  if( k ) k->_next = _chunk;    
  else _first = _chunk;
  _hwm  = _chunk->bottom();     
  _max =  _chunk->top();
  set_size_in_bytes(size_in_bytes() + len);
  void* result = _hwm;
  _hwm += x;
  return result;
}
```

在代码中分配新的`Chunk`块后加入单链表，然后在新的`Chunk`块中分配×大小的内存。

## 2.HandleMark

==每一个Java线程都有一个私有的句柄区`_handle_area`用来存储其运行过程中的句柄信息，这个句柄区会随着Java线程的栈帧而变化。==Java线程每调用一个Java方法就会创建一个对应的`HandleMark`保存创建的对象句柄，然后等调用返回后释放这些对象句柄，此时释放的仅是调用当前方法创建的句柄，因此`HandleMark`只需要恢复到调用方法之前的状态即可。

`HandleMark`主要用于记录当前线程的`HandleArea`的内存地址top,当相关的作用域执行完成后，当前作用域之内的`HandleMark`实例会自动销毁。

==在`HandleMark`的析构函数中会将`HandleArea`当前的内存地址到方法调用前的内存地址`top`之间所有分配的地址中存储的内容都销毁，然后恢复当前线程的`HandleArea`的内存地址`top`为方法调用前的状态。==

一般情况下，`HandleMark`直接在线程栈内存上分配内存，应该继承自`StackObj`,但有时`HandleMark`也需要在堆内存上分配，因此没有继承自StackObj,并且==`HandleMark`为了支持在堆内存上分配内存，重载了new和delete方法==。HandleMark类的定义代码如下：

```cpp
// 来源：hotspot/src/share/vm/runtime/handles.hpp
class HandleMark {
 private:
    
  Thread *_thread;              //拥有当前HandleMark实例的线程
  HandleArea *_area;            
  Chunk *_chunk;                //Chunk和Area配合，获得准确的内存地址
  char *_hwm, *_max;            
  size_t _size_in_bytes;        
  // 通过如下属性让HandleMark形成单链表
  HandleMark* _previous_handle_mark;

  void initialize(Thread* thread);                // common code for constructors
  void set_previous_handle_mark(HandleMark* mark) { _previous_handle_mark = mark; }
  HandleMark* previous_handle_mark() const        { return _previous_handle_mark; }

  size_t size_in_bytes() const { return _size_in_bytes; }
 public:
  HandleMark();                            // see handles_inline.hpp
  HandleMark(Thread* thread)                      { initialize(thread); }
  ~HandleMark();

  void push();
  
  void pop_and_restore();
  // new delete重载
  void* operator new(size_t size) throw();
  void* operator new [](size_t size) throw();
  void operator delete(void* p);
  void operator delete[](void* p);
};
```

`HandleMark`也会通过`_previous_handle_mark`属性形成一个单链表。

在`HandleMark`的构造方法中会调用`initialize()`方法，代码如下：

```cpp
// 来源：hotspot/src/share/vm/runtime/handles.cpp
void HandleMark::initialize(Thread* thread) {
  _thread = thread;
  _area  = thread->handle_area();
  _chunk = _area->_chunk;
  _hwm   = _area->_hwm;
  _max   = _area->_max;
  _size_in_bytes = _area->_size_in_bytes;

  // 将当前HandleMark实例同线程关联起来
  set_previous_handle_mark(thread->last_handle_mark());
  // 注意，线程中的_last_handle_mark属性用来保存HandleMark对象
  thread->set_last_handle_mark(this);
}
```

上面的`initialize()`函数主要用于初始化一些属性。在Thread类中定义的`_last_handle_mark`属性如下：

```cpp
// 来源：hotspot/src/share/vm/runtime/thread.hpp
class Thread: public ThreadShadow {
	...
  HandleMark* _last_handle_mark;
  	...
}
```

`HandleMark`的析构函数如下：

```cpp
// 来源：hotspot/src/share/vm/runtime/handles.cpp
HandleMark::~HandleMark() {
  HandleArea* area = _area;   

  if( _chunk->next() ) {
    area->set_size_in_bytes(size_in_bytes());
    // 删除当前Chunk以后的所有Chunk,即在方法调用期间新创建的Chunk
    _chunk->next_chop();
  } else {
    // 如果没有下一个Chunk,说明未分配新的Chunk,则area的大小应该保持不变
    assert(area->size_in_bytes() == size_in_bytes(), "Sanity check");
  }
  // Roll back arena to saved top markers
  // 恢复area的属性至HandleMark构造时的状态
  area->_chunk = _chunk;
  area->_hwm = _hwm;
  area->_max = _max;
  // 解除当前HandleMark与线程的关联
  _thread->set_last_handle_mark(previous_handle_mark());
}
```

==创建一个新的`HandleMark`以后，它保存当前线程的`area`的`_chunk`、`_hwm`和`_max`等属性，代码执行期间新创建的`Handle`实例是在当前线程的`area`中分配内存，这会导致当前线程的`area`的`_chunk`、`_hwm`和`_max`等属性发生变化，因此代码执行完成后需要将这些属性恢复至之前的状态，并释放代码执行过程中新创建的`Handle`实例的内存。==

