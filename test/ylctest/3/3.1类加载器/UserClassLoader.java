import java.net.URL;
import java.net.URLClassLoader;

/*
 * UserClassLoader类继承了URLClassLoader类并覆写了loadClass()方法，
 * 调用super.loadClass()方法其实就是在调用java.lang.ClassLoader类中实现的loadClass()方法。
 * 在UserClassLoader类的构造函数中，调用super()方法会设置当前类加载器的parent字段值为AppClassLoader对象，
 * 因此也会严格遵守双亲委派逻辑。
 */
public class UserClassLoader extends URLClassLoader {

   public UserClassLoader(URL[] urls) {
       super(urls);
   }

   @Override
   protected Class<?> loadClass(String name, boolean resolve) throws ClassNotFoundException {
       return super.loadClass(name, resolve);
   }
}

/**
 * 通过UserClassLoader类加载器加载Student类，并调用class.newInstance()方法获取Student对象。
 * 下面详细介绍java.lang.ClassLoader类的loadClass()方法调用的findLoadedClass()、
 * findBootstrapClassOrNull()与findClass()方法的实现过程
 * findLoadedClass():源代码位置：openjdk/jdk/src/share/classes/java/lang/ClassLoader.java
 * 
 */
class TestClassLoader {
    public static void main(String[] args) throws Exception {
        URL url[] = new URL[1];
        url[0] = Thread.currentThread().getContextClassLoader().getResource("");
 
        try (UserClassLoader ucl = new UserClassLoader(url)) {
            Class clazz = ucl.loadClass("com.classloading.Student");
 
            Object obj = clazz.newInstance();
        }
    }
 }