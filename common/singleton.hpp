#pragma once 
#include "alias.h"

namespace Cot{

    /** @brief  Meyer's Singleton 
     *  @details Tag 和 TagIdx 用于区分实例 GetInstanceX<MyClass, A, 0> 和 GetInstanceX<MyClass, B, 0> 被视为 两个完全不同的函数。既然是两个不同的函数，它们内部的 static  s_Entity 就是 两块完全不同的内存。
     */
    template<typename T, typename Tag, typename TagIdx>
    requires std::is_default_constructible_v<T>             // 它要求类型 T 必须能被“默认构造”（即可以写 T() 或 T{}，不需要参数）。
    auto GetInstanceX() -> T&
    {
        // 线程安全：C++11 之后，static 局部变量的初始化是线程安全的（Magic Static），不需要加锁。
        static auto s_Entity = T{};
        return s_Entity;
    }

    template <typename T, typename Tag, typename TagIdx>
    requires std::is_default_constructible_v<T>
    auto GetInstanceSptr() -> Sptr<T>
    {
        static auto s_Entity = Sptr<T>(new T{});
        return s_Entity;
    }

    /**
     * @brief 单例模式封装类
     * @details T 类型
     *          Tag 为了创造多个实例对应的Tag
     *          N 同一个Tag创造多个实例索引
     */
    template <class T, class Tag = void, int N = 0>
    class Singleton
    {
    public:
        /**
         * @brief 返回单例裸指针
         */
        Singleton() = delete;

        [[nodiscard]] static T& GetInstance()
        requires std::is_default_constructible_v<T>
        {
            static auto s_Entity = T{};
            return s_Entity;
        }
        
        template<typename...Args>
        requires std::is_constructible_v<T, Args...>
        static T& GetInstance(Args&&... args)
        {
            static auto s_Entity = T{std::forward<Args>(args)...};
            return s_Entity;
        }
    };

    /**
     * @brief 单例模式智能指针封装类
     * @details T 类型
     *          X 为了创造多个实例对应的Tag
     *          N 同一个Tag创造多个实例索引
     */

     template <class T, class Tag = void, int N = 0>
     class SingletonPtr
     {
     public:
       /**
        * @brief 返回单例智能指针
        * @todo 也许这里返回值可以改为std::weak_ptr,但两者开销差不多，也无所谓吧
        *       回答：千万不要返回 std::weak_ptr。
        *       原因：s_Entity 是 static 的，它是强引用，保证了对象一直活着。
        *       如果你返回 weak_ptr，用户每次用的时候都要 .lock() 提升为 shared_ptr，这会增加无意义的原子操作开销（引用计数加减）。
        *       如果是一个单例服务，用户通常希望拿到就能用，而不是拿去观察。
        */
        // 禁止该工具类被实例化
        SingletonPtr() = delete;

        [[nodiscard]] static auto GetInstance() -> Sptr<T>
        requires std::is_default_constructible_v<T>
        {
            // 两次堆内存分配（T 一次，引用计数块 一次）
            // static auto s_Entity = Sptr<T> {new T{}};
            // 一次堆内存分配（合并分配），性能更好，且防止内存碎片。前提是T的构造函数时public
            static auto s_Entity = std::make_shared<T>();
            return s_Entity;
        }

     };


} // namespace Cot