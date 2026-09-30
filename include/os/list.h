/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * *
 * * * * * * * * * * * * Copyright (C) 2018 Institute of Computing
 * Technology, CAS Author : Han Shukai (email :
 * hanshukai@ict.ac.cn)
 * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * *
 * * * * * * * * * * * * Changelog: 2019-8 Reimplement queue.h.
 * Provide Linux-style doube-linked list instead of original
 * unextendable Queue implementation. Luming
 * Wang(wangluming@ict.ac.cn)
 * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * *
 * * * * * * * * * * * *
 *
 * Permission is hereby granted, free of charge, to any person
 * obtaining a copy of this software and associated documentation
 * files (the "Software"), to deal in the Software without
 * restriction, including without limitation the rights to use,
 * copy, modify, merge, publish, distribute, sublicense, and/or
 * sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following
 * conditions:
 *
 * The above copyright notice and this permission notice shall be
 * included in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES
 * OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT
 * HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY,
 * WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 *
 * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * *
 * * * * * * * * * * * */

#ifndef INCLUDE_LIST_H_
#define INCLUDE_LIST_H_

#include <type.h>

// double-linked list
typedef struct list_node
{
    struct list_node *next, *prev;
} list_node_t;

typedef list_node_t list_head;

// LIST_HEAD is used to define the head of a list.
#define LIST_HEAD(name) struct list_node name = {&(name), &(name)}

/* 任务1：内核自己的双向循环链表 API。
 *   用"带哑头结点"的循环链表：head 的 next 是第一个元素、prev 是最后一个，
 *   空表时 head.next == head.prev == head。这样插入/删除都不需要判空特例。 */
#define offset_of(type, member) ((size_t) &((type *)0)->member)

/* 由链表结点反查宿主结构体（PCB）的地址 */
#define list_entry(ptr, type, member) \
    ((type *)((char *)(ptr) - offset_of(type, member)))

/* 把一个裸结点初始化为"自环"的空表头（等价于 LIST_HEAD 的赋值版） */
static inline void list_init(list_node_t *node)
{
    node->next = node;
    node->prev = node;
}

/* 把 node 插到 head 之后（头插）；对称原语，本工程内当前无调用 */
static inline void list_add(list_node_t *node, list_head *head)
{
    node->next = head->next;
    node->prev = head;
    head->next->prev = node;
    head->next = node;
}

/* 把 node 插到 head 之前（即链表尾部，尾插） */
static inline void list_add_tail(list_node_t *node, list_head *head)
{
    node->next = head;
    node->prev = head->prev;
    head->prev->next = node;
    head->prev = node;
}

/* 把 node 从它所在的链表里摘下来（摘完 node 仍指向原邻居，可安全重插） */
static inline void list_del(list_node_t *node)
{
    node->prev->next = node->next;
    node->next->prev = node->prev;
    node->next = node;
    node->prev = node;
}

static inline int list_empty(const list_head *head)
{
    return head->next == head;
}

#endif
