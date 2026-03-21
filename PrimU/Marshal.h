#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <memory>
#include <mutex>
#include <optional>
#include <type_traits>
#include <unordered_map>
#include <utility>

#include "InterruptHandler.h"
#include "executor.h"

// ################################################################
//  Part 1: 参数来源描述符
// ################################################################

template <int N> struct Reg {
  static_assert(N >= 0 && N <= 4);
};
template <int Offset> struct Stack {};

// ################################################################
//  Part 2: 原始值提取 (FetchRaw)
// ################################################################

template <typename Src> struct FetchRaw;

template <> struct FetchRaw<Reg<0>> {
  static uint32_t get(SystemServiceArguments *a) { return a->r0; }
};
template <> struct FetchRaw<Reg<1>> {
  static uint32_t get(SystemServiceArguments *a) { return a->r1; }
};
template <> struct FetchRaw<Reg<2>> {
  static uint32_t get(SystemServiceArguments *a) { return a->r2; }
};
template <> struct FetchRaw<Reg<3>> {
  static uint32_t get(SystemServiceArguments *a) { return a->r3; }
};
template <int Off> struct FetchRaw<Stack<Off>> {
  static uint32_t get(SystemServiceArguments *a) {
    return *__GET(uint32_t *, a->sp + Off);
  }
};

// ################################################################
//  Part 3: 参数描述符
// ################################################################

template <typename T, typename Src> struct As {
  using cpp_type = T;
  static T extract(SystemServiceArguments *a) {
    return static_cast<T>(FetchRaw<Src>::get(a));
  }
};

template <typename T, typename Src> struct VPtr {
  using cpp_type = T *;
  static T *extract(SystemServiceArguments *a) {
    VirtPtr vp = FetchRaw<Src>::get(a);
    return vp ? __GET(T *, vp) : nullptr;
  }
};

template <typename T, typename Src> struct VRef {
  using cpp_type = T &;
  static T &extract(SystemServiceArguments *a) {
    return *__GET(T *, FetchRaw<Src>::get(a));
  }
};

template <typename T, typename Src> struct VPtrDeref {
  using cpp_type = T *;
  static T *extract(SystemServiceArguments *a) {
    uint32_t raw = FetchRaw<Src>::get(a);
    if (!raw)
      return nullptr;
    VirtPtr inner = *__GET(VirtPtr *, raw);
    return inner ? __GET(T *, inner) : nullptr;
  }
};

template <typename Wrapper, typename WrapperFunc> struct CustomWrapper {
  using cpp_type = typename Wrapper::cpp_type;
  static decltype(auto) extract(SystemServiceArguments *a) {
    auto v = WrapperFunc();
    return v(Wrapper::extract(a));
  }
};

// ################################################################
//  Part 4: 便捷别名
// ################################################################

template <typename T> using R0 = As<T, Reg<0>>;
template <typename T> using R1 = As<T, Reg<1>>;
template <typename T> using R2 = As<T, Reg<2>>;
template <typename T> using R3 = As<T, Reg<3>>;

template <typename T> using VP0 = VPtr<T, Reg<0>>;
template <typename T> using VP1 = VPtr<T, Reg<1>>;
template <typename T> using VP2 = VPtr<T, Reg<2>>;
template <typename T> using VP3 = VPtr<T, Reg<3>>;

template <typename T> using VR0 = VRef<T, Reg<0>>;
template <typename T> using VR1 = VRef<T, Reg<1>>;
template <typename T> using VR2 = VRef<T, Reg<2>>;
template <typename T> using VR3 = VRef<T, Reg<3>>;

template <typename T, int Off> using SP = As<T, Stack<Off>>;
template <typename T, int Off> using SPVP = VPtr<T, Stack<Off>>;
template <typename T, int Off> using SPVR = VRef<T, Stack<Off>>;
template <typename T, int Off> using SPVPD = VPtrDeref<T, Stack<Off>>;

template <typename T> using VPD0 = VPtrDeref<T, Reg<0>>;
template <typename T> using VPD1 = VPtrDeref<T, Reg<1>>;
template <typename T> using VPD2 = VPtrDeref<T, Reg<2>>;
template <typename T> using VPD3 = VPtrDeref<T, Reg<3>>;

// ################################################################
//  Part 4b: GuestVal — 带类型的虚拟指针包装器（多级指针支持）
// ################################################################

// 持有一个 VirtPtr，提供类型安全的延迟解引用。
// 在 AutoBind 函数签名中使用时，自动从寄存器/栈读取 VirtPtr。
// 支持多级指针链：deref<U>() 读取所指位置的 VirtPtr 并包装。
//
// 用法示例:
//   static uint32_t handler(GuestVal<char> name, uint32_t len) {
//       char* p = name.get();  // 解引用为宿主指针
//   }
//   // 多级:
//   static uint32_t handler(GuestVal<void> outer) {
//       char* inner = outer.deref<char>().get(); // 双重解引用
//   }
template <typename T> struct GuestVal {
  VirtPtr vp = 0;

  T *get() const { return vp ? __GET(T *, vp) : nullptr; }
  T &ref() const { return *get(); }
  T *operator->() const { return get(); }
  T &operator*() const { return *get(); }
  explicit operator bool() const { return vp != 0; }
  VirtPtr raw() const { return vp; }

  // 多级指针链：读取当前 vp 所指位置的 VirtPtr，包装为 GuestVal<U>
  template <typename U> GuestVal<U> deref() const {
    if (!vp)
      return {0};
    return {*__GET(VirtPtr *, vp)};
  }

  // 写入值到虚拟地址
  void set(const T &val) const {
    if (vp)
      *get() = val;
  }
};

// 便捷别名
template <typename T> using GV0 = GuestVal<T>; // 用于函数签名，从 r0 自动提取
template <typename T> using GV1 = GuestVal<T>;
template <typename T> using GV2 = GuestVal<T>;
template <typename T> using GV3 = GuestVal<T>;

// ################################################################
//  Part 4c: GuestMarshal — 自定义类型转换特性
// ################################################################

// 用户对自定义类型特化此模板，即可在 AutoBind 函数签名中
// 直接使用该类型作为参数。AutoBind 会自动读取 uint32_t
// 并调用 from_raw() 转换。
//
// 用法示例:
//   struct FileHandle { uint32_t id; };
//   template<> struct GuestMarshal<FileHandle> {
//       static constexpr bool custom = true;
//       static FileHandle from_raw(uint32_t raw) { return {raw}; }
//   };
//   // 之后即可:
//   static uint32_t handler(FileHandle fh, uint32_t size) { ... }
template <typename T, typename = void> struct GuestMarshal {
  static constexpr bool custom = false;
};

// ── 提取器实现 ──

// GuestVal<T> 提取器
template <typename T, typename Src> struct GuestValExtract {
  using cpp_type = GuestVal<T>;
  static GuestVal<T> extract(SystemServiceArguments *a) {
    return GuestVal<T>{FetchRaw<Src>::get(a)};
  }
};

// 自定义转换提取器
template <typename T, typename Src> struct CustomMarshalExtract {
  using cpp_type = T;
  static T extract(SystemServiceArguments *a) {
    return GuestMarshal<std::remove_cv_t<T>>::from_raw(FetchRaw<Src>::get(a));
  }
};

// ################################################################
//  Part 5: 句柄域 + 强类型句柄
// ################################################################

enum class HandleDomain : uint32_t {
  File = 0xDEAD'0000u,
  Find = 0xF14D'0000u,
  Device = 0xDEEF'0000u,
  Event = 0xE0EE'0000u,
  CritSec = 0xCC55'0000u,
  Generic = 0xAABB'0000u,
};

template <HandleDomain D> struct Handle {
  uint32_t value = 0;
  constexpr explicit Handle(uint32_t v = 0) : value(v) {}
  constexpr bool valid() const { return value != 0; }
  constexpr explicit operator bool() const { return valid(); }
  constexpr explicit operator uint32_t() const { return value; }
  constexpr bool operator==(const Handle &) const = default;
};

// ################################################################
//  Part 6: HandleTable<T, Domain>
// ################################################################

template <typename T, HandleDomain Domain> class HandleTable {
public:
  using HandleType = Handle<Domain>;
  using StoredType = std::unique_ptr<T>;

  explicit HandleTable(uint32_t base = static_cast<uint32_t>(Domain))
      : m_next(base + 1) {}

  [[nodiscard]] HandleType insert(StoredType obj) {
    if (!obj)
      return HandleType{0};
    std::lock_guard lk(m_mtx);
    uint32_t id = alloc_locked();
    m_map.emplace(id, std::move(obj));
    return HandleType{id};
  }

  template <typename... Args> [[nodiscard]] HandleType emplace(Args &&...args) {
    return insert(std::make_unique<T>(std::forward<Args>(args)...));
  }

  T *get(HandleType h) {
    std::lock_guard lk(m_mtx);
    return get_locked(h.value);
  }

  template <typename Fn>
  auto with(HandleType h, Fn &&fn)
      -> std::optional<std::invoke_result_t<Fn, T &>> {
    std::lock_guard lk(m_mtx);
    T *p = get_locked(h.value);
    if (!p)
      return std::nullopt;
    return std::optional{fn(*p)};
  }

  template <typename Fn>
    requires std::is_invocable_r_v<void, Fn, T &>
  bool with_void(HandleType h, Fn &&fn) {
    std::lock_guard lk(m_mtx);
    T *p = get_locked(h.value);
    if (!p)
      return false;
    fn(*p);
    return true;
  }

  template <typename Fn>
  auto with_and_erase(HandleType h, Fn &&fn)
      -> std::optional<std::invoke_result_t<Fn, T &>> {
    std::lock_guard lk(m_mtx);
    auto it = m_map.find(h.value);
    if (it == m_map.end() || !it->second)
      return std::nullopt;
    using R = std::invoke_result_t<Fn, T &>;
    if constexpr (std::is_void_v<R>) {
      fn(*it->second);
      m_map.erase(it);
      return std::optional<R>{std::in_place};
    } else {
      auto r = fn(*it->second);
      m_map.erase(it);
      return std::optional<R>{std::move(r)};
    }
  }

  bool erase(HandleType h) {
    std::lock_guard lk(m_mtx);
    auto it = m_map.find(h.value);
    if (it == m_map.end())
      return false;
    m_map.erase(it);
    return true;
  }

  [[nodiscard]] StoredType release(HandleType h) {
    std::lock_guard lk(m_mtx);
    auto it = m_map.find(h.value);
    if (it == m_map.end())
      return nullptr;
    auto obj = std::move(it->second);
    m_map.erase(it);
    return obj;
  }

  size_t size() const {
    std::lock_guard lk(m_mtx);
    return m_map.size();
  }

  template <typename Fn> void for_each(Fn &&fn) {
    std::lock_guard lk(m_mtx);
    for (auto &[id, obj] : m_map)
      if (obj)
        fn(HandleType{id}, *obj);
  }

  void clear() {
    std::lock_guard lk(m_mtx);
    m_map.clear();
  }

private:
  mutable std::mutex m_mtx;
  std::unordered_map<uint32_t, StoredType> m_map;
  uint32_t m_next;

  uint32_t alloc_locked() {
    while (m_map.contains(m_next) || m_next == 0)
      ++m_next;
    return m_next++;
  }

  T *get_locked(uint32_t id) {
    auto it = m_map.find(id);
    return (it != m_map.end() && it->second) ? it->second.get() : nullptr;
  }
};

// ################################################################
//  Part 7: HandleObject CRTP 基类
// ################################################################

template <typename Derived, HandleDomain Domain> class HandleObject {
public:
  using HandleType = Handle<Domain>;
  using TableType = HandleTable<Derived, Domain>;

  static TableType &table() {
    static TableType s;
    return s;
  }
};
// ################################################################
//  Part 8: 自动参数绑定 + REGISTER_FOR 宏
// ################################################################

// ----------------------------------------------------------------
// 8.1  位置 -> 参数源描述符映射
//      第 0~3 个参数 -> Reg<0>~Reg<3>
//      第 4+  个参数 -> Stack<(N-4)*4>
// ----------------------------------------------------------------

template <std::size_t N> struct PositionToSrc {
  // N >= 4: 栈上，偏移 (N-4)*4
  using type = Stack<static_cast<int>((N - 4) * 4)>;
};
template <> struct PositionToSrc<0> {
  using type = Reg<0>;
};
template <> struct PositionToSrc<1> {
  using type = Reg<1>;
};
template <> struct PositionToSrc<2> {
  using type = Reg<2>;
};
template <> struct PositionToSrc<3> {
  using type = Reg<3>;
};

template <std::size_t N>
using PositionToSrc_t = typename PositionToSrc<N>::type;

// ----------------------------------------------------------------
// 8.2  C++ 类型 -> 描述符自动推导
//
//  优先级（由高到低）:
//    GuestVal<T>   -> GuestValExtract    (延迟解引用)
//    T&            -> VRef               (引用)
//    T*            -> VPtr               (单级指针)
//    GuestMarshal  -> CustomMarshalExtract(自定义转换)
//    其余          -> As                 (标量)
// ----------------------------------------------------------------

// 主模板：检查 GuestMarshal 特化，否则用 As
template <typename T, typename Src> struct MakeDescriptor {
  using type = std::conditional_t<GuestMarshal<std::remove_cv_t<T>>::custom,
                                  CustomMarshalExtract<T, Src>, As<T, Src>>;
};

// 单级指针
template <typename T, typename Src> struct MakeDescriptor<T *, Src> {
  using type = VPtr<T, Src>;
};

// 引用类型
template <typename T, typename Src> struct MakeDescriptor<T &, Src> {
  using type = VRef<std::remove_cv_t<T>, Src>;
};

// GuestVal<T> — 延迟虚拟指针
template <typename T, typename Src> struct MakeDescriptor<GuestVal<T>, Src> {
  using type = GuestValExtract<T, Src>;
};

template <typename T, typename Src>
using MakeDescriptor_t = typename MakeDescriptor<T, Src>::type;

// ----------------------------------------------------------------
// 修复核心逻辑：AutoBind
// ----------------------------------------------------------------

// 1. 定义核心逻辑 Helper，它只关心 Ret 和 Args...，不关心原来的 Callable
// 是什么类型
template <typename Ret, typename... Args> struct AutoBindHelper {
private:
  // 为第 I 个参数生成描述符
  template <std::size_t I>
  using Desc = MakeDescriptor_t<std::tuple_element_t<I, std::tuple<Args...>>,
                                PositionToSrc_t<I>>;

  // 核心调用实现
  // 修改点：Fn 作为非类型模板参数直接传入，不再作为参数传递，
  // 这样既支持函数指针，也支持 C++20 的 constexpr lambda
  template <auto Fn, std::size_t... Is>
  static uint32_t invoke_impl(SystemServiceArguments *a,
                              std::index_sequence<Is...>) {
    if constexpr (std::is_void_v<Ret>) {
      // 直接调用 Fn
      Fn(Desc<Is>::extract(a)...);
      return 0;
    } else {
      auto result = Fn(Desc<Is>::extract(a)...);
      return static_cast<uint32_t>(result);
    }
  }

public:
  // 真正的包装入口
  template <auto Fn> static uint32_t thunk(SystemServiceArguments *a) {
    return invoke_impl<Fn>(a, std::index_sequence_for<Args...>{});
  }
};

// ----------------------------------------------------------------
// 2. AutoBind 接口与特化推导
// ----------------------------------------------------------------

// 前置声明
template <typename T> struct AutoBind;

// (A) 针对普通函数类型 Ret(Args...)
template <typename Ret, typename... Args>
struct AutoBind<Ret(Args...)> : AutoBindHelper<Ret, Args...> {};

// (B) 针对函数指针 Ret(*)(Args...)
template <typename Ret, typename... Args>
struct AutoBind<Ret (*)(Args...)> : AutoBindHelper<Ret, Args...> {};

// (C) 针对 noexcept 函数指针
template <typename Ret, typename... Args>
struct AutoBind<Ret (*)(Args...) noexcept> : AutoBindHelper<Ret, Args...> {};

// (D) 针对 Lambda / Functor (关键修复)
// 如果传入的是一个类类型（如 Lambda），我们继承自对其 operator() 的推导结果
template <typename T> struct AutoBind : AutoBind<decltype(&T::operator())> {};

// (E) 针对成员函数指针（常量版本，通常对应 Lambda 的 operator()）
// 通过这个特化，我们将 Lambda 转换为了 Ret 和 Args...，并桥接到 AutoBindHelper
template <typename ClassType, typename Ret, typename... Args>
struct AutoBind<Ret (ClassType::*)(Args...) const>
    : AutoBindHelper<Ret, Args...> {};

// (F) 针对成员函数指针（非常量版本，对应 mutable Lambda）
template <typename ClassType, typename Ret, typename... Args>
struct AutoBind<Ret (ClassType::*)(Args...)> : AutoBindHelper<Ret, Args...> {};

// (G) 针对成员函数指针 noexcept 版本 (C++17+)
template <typename ClassType, typename Ret, typename... Args>
struct AutoBind<Ret (ClassType::*)(Args...) const noexcept>
    : AutoBindHelper<Ret, Args...> {};

template <typename ClassType, typename Ret, typename... Args>
struct AutoBind<Ret (ClassType::*)(Args...) noexcept>
    : AutoBindHelper<Ret, Args...> {};

// ----------------------------------------------------------------
// 3. 导出宏与辅助定义
// ----------------------------------------------------------------

using SvcHandler = uint32_t (*)(SystemServiceArguments *);

// 注意：为了支持 Lambda 作为非类型模板参数 (NTTP)，需要 C++20 支持
// 或者传入的 Lambda 必须是无捕获的（Stateless），以便编译器将其视为常量地址
template <auto Fn>
inline constexpr SvcHandler wrap_fn_f =
    &AutoBind<std::remove_pointer_t<decltype(Fn)>>::template thunk<Fn>;

// 如果 decltype(Fn) 是函数指针，remove_pointer_t 转为函数类型，命中 (A)
// 如果 decltype(Fn) 是 Lambda 类，命中 (D) -> (E)

#define EXPORT(name, func) inline constexpr SvcHandler name = wrap_fn_f<func>;