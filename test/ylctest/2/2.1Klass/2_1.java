interface IA{}
interface IB{}

class A{}
class B extends A{}
class C extends B{}
class D extends C{}

public class Test extends D implements IA,IB {}
/**
看看这几个属性是如何存储值的
配置-XX:FastSuperclassLimit=3命令后，_primary_supers数组中最多只能存储3个类型。_primary_supers和_secondary_supers的值如下：
_primary_supers[Object,A,B]
_secondary_supers[C,D,Test,IA,IB]
由于当前类Test的继承链过长，导致C、D和Test只能存储到_secondary_supers数组中。此时_super_check_offset会指向C，也就是指向_secondary_supers中存储的第一个元素。另外，父类的继承链需要保证按顺序存储，如_primary_supers中的存储顺序必须为Object、A、B，这样有利于快速判断各个类之间的关系。
is_subtype_of()函数会利用以上属性保存的一些信息进行父子关系的判断

 */