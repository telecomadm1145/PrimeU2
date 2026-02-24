#pragma once
#include <cstdint>
#include "common.h"
#include "MemoryManager.h"
struct RTTIInfo {
private:
	VirtPtr2<void> __impl_class_type_info;       // +0x00
	VirtPtr2<char> __impl_class_name;                // +0x04
	VirtPtr d;
	VirtPtr e;
	VirtPtr f;
public:
	RTTIInfo* type() const {
		return __GET(RTTIInfo*, *__GET(VirtPtr*, __impl_class_type_info - 0x08));
	}
	const char* name() const {
		return __GET(char*, __impl_class_name);
	}
	std::vector<RTTIInfo*> parents() const {
		std::vector<RTTIInfo*> c;
		auto name = std::string_view(type()->name());
		if (name.contains("si_class_type_info"))
			c.push_back(__GET(RTTIInfo*, d));
		else if (name.contains("vmi_class_type_info"))
			for (size_t i = 0; i < e; i++)
				c.push_back(__GET(RTTIInfo*, (&f)[i * 2]));
		return c;
	}
};
struct VTable
{
public:
	RTTIInfo* rtti_info() const {
		return __GET(RTTIInfo*, *((VirtPtr*)this - 1));
	}
};
struct PrimeObj {
private:
	VirtPtr2<VTable> __impl_vtable;    // +0x00
public:
	VTable* vtbl() const {
		return __GET(VTable*, __impl_vtable);
	}
	RTTIInfo* rtti() const {
		return vtbl()->rtti_info();
	}
	const char* type_name() const {
		return rtti()->name();
	}
}; struct CComponent : PrimeObj {
	uint8_t pad_0x10[0x10];            // +0x04
	VirtPtr2<void> unk_0x14;           // +0x14  (不是prev_sibling)
	VirtPtr2<CComponent> parent;       // +0x18
	VirtPtr2<CComponent> first_child;  // +0x1C
	VirtPtr2<CComponent> next_sibling; // +0x20
	uint16_t child_count;              // +0x24

	// ---- STL 类型定义 ----
	using value_type = CComponent;
	using size_type = uint16_t;
	using difference_type = std::ptrdiff_t;

	// ---- 单向链表迭代器 ----
	struct iterator {
		using iterator_category = std::forward_iterator_tag;
		using value_type = CComponent;
		using difference_type = std::ptrdiff_t;
		using pointer = value_type*;
		using reference = value_type&;

		pointer current;
		iterator(pointer p = nullptr) : current(p) {}
		reference operator*()  const { return *current; }
		pointer   operator->() const { return current; }
		iterator& operator++() {
			if (current) {
				auto nv = current->next_sibling;
				current = nv ? __GET(CComponent*, nv) : nullptr;
			}
			return *this;
		}
		iterator operator++(int) { auto t = *this; ++(*this); return t; }
		friend bool operator==(const iterator& a, const iterator& b) { return a.current == b.current; }
		friend bool operator!=(const iterator& a, const iterator& b) { return a.current != b.current; }
	};

	struct const_iterator {
		using iterator_category = std::forward_iterator_tag;
		using value_type = const CComponent;
		using difference_type = std::ptrdiff_t;
		using pointer = const CComponent*;
		using reference = const CComponent&;

		pointer current;
		const_iterator(pointer p = nullptr) : current(p) {}
		const_iterator(const iterator& it) : current(it.current) {}
		reference operator*()  const { return *current; }
		pointer   operator->() const { return current; }
		const_iterator& operator++() {
			if (current) {
				auto nv = current->next_sibling;
				current = nv ? static_cast<pointer>(__GET(CComponent*, nv)) : nullptr;
			}
			return *this;
		}
		const_iterator operator++(int) { auto t = *this; ++(*this); return t; }
		friend bool operator==(const const_iterator& a, const const_iterator& b) { return a.current == b.current; }
		friend bool operator!=(const const_iterator& a, const const_iterator& b) { return a.current != b.current; }
	};

	// ---- 容器接口 ----
	bool      empty() const { return child_count == 0; }
	size_type size()  const { return child_count; }

	CComponent* get_parent() {
		return parent ? __GET(CComponent*, parent) : nullptr;
	}
	const CComponent* get_parent() const {
		return parent ? static_cast<const CComponent*>(__GET(CComponent*, parent)) : nullptr;
	}

	iterator begin() {
		return iterator(first_child ? __GET(CComponent*, first_child) : nullptr);
	}
	iterator       end() { return iterator(nullptr); }
	const_iterator begin() const { return cbegin(); }
	const_iterator end()   const { return cend(); }
	const_iterator cbegin() const {
		return const_iterator(first_child
			? static_cast<const CComponent*>(__GET(CComponent*, first_child))
			: nullptr);
	}
	const_iterator cend() const { return const_iterator(nullptr); }

	// ---- 单向链表：尾插 ----
	void push_back(CComponent* child) {
		if (!child) return;
		child->parent = (VirtPtr2<CComponent>)__ADDR(this);
		child->next_sibling = 0;

		CComponent* head = first_child ? __GET(CComponent*, first_child) : nullptr;
		if (!head) {
			first_child = (VirtPtr2<CComponent>)__ADDR(child);
		}
		else {
			CComponent* last = head;
			while (true) {
				CComponent* nxt = last->next_sibling
					? __GET(CComponent*, last->next_sibling) : nullptr;
				if (!nxt) break;
				last = nxt;
			}
			last->next_sibling = (VirtPtr2<CComponent>)__ADDR(child);
		}
		++child_count;
	}

	// ---- 单向链表：头插 ----
	void push_front(CComponent* child) {
		if (!child) return;
		child->parent = (VirtPtr2<CComponent>)__ADDR(this);
		child->next_sibling = first_child; // 旧头
		first_child = (VirtPtr2<CComponent>)__ADDR(child);
		++child_count;
	}

	// ---- 单向链表：erase（需要找前驱） ----
	iterator erase(iterator pos) {
		CComponent* node = pos.current;
		if (!node) return end();

		CComponent* head = first_child ? __GET(CComponent*, first_child) : nullptr;
		CComponent* node_next = node->next_sibling
			? __GET(CComponent*, node->next_sibling) : nullptr;

		if (head == node) {
			// 删的是第一个
			first_child = node_next
				? (VirtPtr2<CComponent>)__ADDR(node_next)
				: (VirtPtr2<CComponent>)0;
		}
		else {
			// 遍历找前驱
			CComponent* prev = head;
			while (prev) {
				CComponent* pn = prev->next_sibling
					? __GET(CComponent*, prev->next_sibling) : nullptr;
				if (pn == node) break;
				prev = pn;
			}
			if (prev) {
				prev->next_sibling = node_next
					? (VirtPtr2<CComponent>)__ADDR(node_next)
					: (VirtPtr2<CComponent>)0;
			}
		}

		node->parent = 0;
		node->next_sibling = 0;
		if (child_count > 0) --child_count;

		return iterator(node_next);
	}

	iterator erase(const_iterator cpos) {
		return erase(iterator(const_cast<CComponent*>(cpos.current)));
	}
};