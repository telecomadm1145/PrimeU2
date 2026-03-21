#pragma once
#include "MemoryManager.h"
#include "common.h"
#include <cstdint>

struct RTTIInfo {
private:
  VirtPtr2<void> __impl_class_type_info; // +0x00
  VirtPtr2<char> __impl_class_name;      // +0x04
  VirtPtr d;
  VirtPtr e;
  VirtPtr f;

public:
  RTTIInfo *type() const {
    return __GET(RTTIInfo *, *__GET(VirtPtr *, __impl_class_type_info - 0x08));
  }
  const char *name() const { return __GET(char *, __impl_class_name); }
  std::vector<RTTIInfo *> parents() const {
    std::vector<RTTIInfo *> c;
    auto name = std::string_view(type()->name());
    if (name.contains("si_class_type_info"))
      c.push_back(__GET(RTTIInfo *, d));
    else if (name.contains("vmi_class_type_info"))
      for (size_t i = 0; i < e; i++)
        c.push_back(__GET(RTTIInfo *, (&f)[i * 2]));
    return c;
  }
};
struct VTable {
public:
  RTTIInfo *rtti_info() const {
    return __GET(RTTIInfo *, *((VirtPtr *)this - 1));
  }
};
struct TextInputEvent { // size = 0x24 (36 bytes)
  /* +0  */ uint32_t unknown_0;
  /* +4  */ VirtPtr2<wchar_t> text; // pointer to UTF-16 input string
  /* +8  */ uint32_t flags;         // event type / modifier flags
  /* +12 */ int16_t extra;
  /* +14 */ uint8_t _pad[2];
  /* +16 */ int8_t cursor_delta;
  /* +17 */ int8_t delete_flag;
  /* +18 */ uint8_t _pad2[18]; // remainder to reach 36 bytes
};
struct PrimeObj {
private:
  VirtPtr2<VTable> __impl_vtable; // +0x00
public:
  VTable *vtbl() const { return __GET(VTable *, __impl_vtable); }
  RTTIInfo *rtti() const { return vtbl()->rtti_info(); }
  const char *type_name() const { return rtti()->name(); }
};
struct CComponent : PrimeObj {
  int32_t x;      // +0x04: 相对父组件的 X 坐标 (a3)
  int32_t y;      // +0x08: 相对父组件的 Y 坐标 (a4)
  int32_t width;  // +0x0C: 控件宽度 (a5)
  int32_t height; // +0x10: 控件高度 (a6)

  VirtPtr texture;                   // +0x14: 组件资源 ID 或内部状态ID
  VirtPtr2<CComponent> parent;       // +0x18
  VirtPtr2<CComponent> first_child;  // +0x1C
  VirtPtr2<CComponent> next_sibling; // +0x20
  uint16_t child_count;              // +0x24
  VirtPtr2<CComponent> unk; // +0x28: 指向同一父级下的下一个兄弟组件

  uint32_t
      flags; // +0x2C: 控件属性和状态标记位 (例如 a1[11], 控制可见性/重绘掩码等)
  void SetPendingEvent() { flags |= 0x80; }
  // ---- STL 类型定义 ----
  using value_type = CComponent;
  using size_type = uint16_t;
  using difference_type = std::ptrdiff_t;

  // ---- 单向链表迭代器 ----
  struct iterator {
    using iterator_category = std::forward_iterator_tag;
    using value_type = CComponent;
    using difference_type = std::ptrdiff_t;
    using pointer = value_type *;
    using reference = value_type &;

    pointer current;
    iterator(pointer p = nullptr) : current(p) {}
    reference operator*() const { return *current; }
    pointer operator->() const { return current; }
    iterator &operator++() {
      if (current) {
        auto nv = current->next_sibling;
        current = nv ? __GET(CComponent *, nv) : nullptr;
      }
      return *this;
    }
    iterator operator++(int) {
      auto t = *this;
      ++(*this);
      return t;
    }
    friend bool operator==(const iterator &a, const iterator &b) {
      return a.current == b.current;
    }
    friend bool operator!=(const iterator &a, const iterator &b) {
      return a.current != b.current;
    }
  };

  struct const_iterator {
    using iterator_category = std::forward_iterator_tag;
    using value_type = const CComponent;
    using difference_type = std::ptrdiff_t;
    using pointer = const CComponent *;
    using reference = const CComponent &;

    pointer current;
    const_iterator(pointer p = nullptr) : current(p) {}
    const_iterator(const iterator &it) : current(it.current) {}
    reference operator*() const { return *current; }
    pointer operator->() const { return current; }
    const_iterator &operator++() {
      if (current) {
        auto nv = current->next_sibling;
        current = nv ? static_cast<pointer>(__GET(CComponent *, nv)) : nullptr;
      }
      return *this;
    }
    const_iterator operator++(int) {
      auto t = *this;
      ++(*this);
      return t;
    }
    friend bool operator==(const const_iterator &a, const const_iterator &b) {
      return a.current == b.current;
    }
    friend bool operator!=(const const_iterator &a, const const_iterator &b) {
      return a.current != b.current;
    }
  };

  // ---- 容器接口 ----
  bool empty() const { return child_count == 0; }
  size_type size() const { return child_count; }

  CComponent *get_parent() {
    return parent ? __GET(CComponent *, parent) : nullptr;
  }
  const CComponent *get_parent() const {
    return parent ? static_cast<const CComponent *>(__GET(CComponent *, parent))
                  : nullptr;
  }

  iterator begin() {
    return iterator(first_child ? __GET(CComponent *, first_child) : nullptr);
  }
  iterator end() { return iterator(nullptr); }
  const_iterator begin() const { return cbegin(); }
  const_iterator end() const { return cend(); }
  const_iterator cbegin() const {
    return const_iterator(first_child ? static_cast<const CComponent *>(
                                            __GET(CComponent *, first_child))
                                      : nullptr);
  }
  const_iterator cend() const { return const_iterator(nullptr); }

  // ---- 单向链表：尾插 ----
  void push_back(CComponent *child) {
    if (!child)
      return;
    child->parent = (VirtPtr2<CComponent>)__ADDR(this);
    child->next_sibling = 0;

    CComponent *head = first_child ? __GET(CComponent *, first_child) : nullptr;
    if (!head) {
      first_child = (VirtPtr2<CComponent>)__ADDR(child);
    } else {
      CComponent *last = head;
      while (true) {
        CComponent *nxt = last->next_sibling
                              ? __GET(CComponent *, last->next_sibling)
                              : nullptr;
        if (!nxt)
          break;
        last = nxt;
      }
      last->next_sibling = (VirtPtr2<CComponent>)__ADDR(child);
    }
    ++child_count;
  }

  // ---- 单向链表：头插 ----
  void push_front(CComponent *child) {
    if (!child)
      return;
    child->parent = (VirtPtr2<CComponent>)__ADDR(this);
    child->next_sibling = first_child; // 旧头
    first_child = (VirtPtr2<CComponent>)__ADDR(child);
    ++child_count;
  }

  // ---- 单向链表：erase（需要找前驱） ----
  iterator erase(iterator pos) {
    CComponent *node = pos.current;
    if (!node)
      return end();

    CComponent *head = first_child ? __GET(CComponent *, first_child) : nullptr;
    CComponent *node_next =
        node->next_sibling ? __GET(CComponent *, node->next_sibling) : nullptr;

    if (head == node) {
      // 删的是第一个
      first_child = node_next ? (VirtPtr2<CComponent>)__ADDR(node_next)
                              : (VirtPtr2<CComponent>)0;
    } else {
      // 遍历找前驱
      CComponent *prev = head;
      while (prev) {
        CComponent *pn = prev->next_sibling
                             ? __GET(CComponent *, prev->next_sibling)
                             : nullptr;
        if (pn == node)
          break;
        prev = pn;
      }
      if (prev) {
        prev->next_sibling = node_next ? (VirtPtr2<CComponent>)__ADDR(node_next)
                                       : (VirtPtr2<CComponent>)0;
      }
    }

    node->parent = 0;
    node->next_sibling = 0;
    if (child_count > 0)
      --child_count;

    return iterator(node_next);
  }

  iterator erase(const_iterator cpos) {
    return erase(iterator(const_cast<CComponent *>(cpos.current)));
  }
};
struct CWindow : public CComponent {
  // 这时候 vtable 将变为 0x30CCE948

  int32_t field_30; // +0x30: 初始化为 0
  int32_t field_34; // +0x34: 初始化为 0

  // 扩展事件的动态多态回调绑定 (Event Delegate Callbacks)
  // 这是我们在前面事件路由分析中找到的最神圣的外部“侵入点”！
  VirtPtr on_focused_cb; // +0x38 (a1[14]): 获得焦点时的外部回调
  VirtPtr field_3C;      // +0x3C (a1[15]): 保留或内部句柄
  VirtPtr on_touch_cb; // +0x40 (a1[16]): 动态绑定的触屏事件回调 (Delegate)
  VirtPtr on_key_cb; // +0x44 (a1[17]): 动态绑定的按键事件回调 (Delegate)
  VirtPtr on_gesture_cb; // +0x48 (a1[18]): 手势系统外部回调
  VirtPtr on_nav_cb;     // +0x4C (a1[19]): 导航与动作事件外部回调

  int32_t field_50; // +0x50 (a1[20])

  VirtPtr ui_ctx_manager; // +0x54 (a1[21]): ui context / 渲染上下文数据缓冲管理
                          // (由 sub_3068FA24 生成)

  // a1[22] 到 a1[31] (从偏移 0x58 到 0x7C)
  // 根据分析此处是一段大小为 10*4 Bytes 的保留对象区或数组空间，初始被置空。
  int32_t reserved_buffer[10]; // +0x58 到 +0x7C

  // --- (下面可能是继承自 CWindow
  // 的具体实现类的额外内存空间，比如弹窗会扩展到128位以上) ---
  CComponent *prev_focus; // 某些组件在 +0x68 (a1[26])
                          // 存放失去本页面前的老焦点实例，比如弹框机制
  VirtPtr2<TextInputEvent> event_slot;
}; // --------------------------------------------------------------------------------
   // //

// off_30D58568 = base address of the global UI state object (guest VirtPtr)
// CCommandLine object lives at off_30D58568 + 0x64 (confirmed by sub_30B9340C:
// sub_30AFA3D0((int)off_30D58568 + 100, ...)) TextInputEvent is stored inline
// at CCommandLine + 0x88
constexpr VirtPtr system_context_vaddr = 0x30D58568;

inline VirtPtr get_text_focus() {
  // Return the CCommandLine guest address directly.
  // The object at this address owns the inline TextInputEvent slot at +0x88.
  return *__GET(uint32_t*,system_context_vaddr) + 0x64 +0xac;
}
// CWindow 完整虚函数表 (VTable offsets) Reference
// --------------------------------------------------------------------------------
// //
/*
  vtable[0]  (+0x00): ~CWindow()  [Destructor]
  vtable[9]  (+0x24): ui_component_handle_event(Event*) -> EventDispatcher 核心
  vtable[10] (+0x28): ui_window_draw() -> 重绘渲染入口
  vtable[11] (+0x2C): ui_window_draw_unfocused() -> 失焦重绘
  vtable[15] (+0x3C): ui_window_handle_touch_evt(Event*)
  vtable[19] (+0x4C): ui_window_handle_key_evt(Event*) -> 按键事件消费
  vtable[20] (+0x50): ui_window_handle_gesture_evt(Event*)
  vtable[21] (+0x54): ui_window_handle_nav_evt(Event*)
*/