/* subset of the Linux Kernel source file: "include/linux/list.h" CPLv2 */
#ifndef LISTX_H_INCLUDED
#define LISTX_H_INCLUDED

#include <stddef.h>

/* Circular doubly linked list implementation. */
struct list_head {
    struct list_head *next, *prev;
};

/* container_of require GNU extensions, see 'Statements and Declarations in Expressions'
 * in https://gcc.gnu.org/onlinedocs/gcc/Syntax-Extensions.html */
#define container_of(ptr, type, member) \
    ({ \
        const typeof(((type *)0)->member) *__mptr = (ptr); \
        (type *)((char *)__mptr - offsetof(type, member)); \
    })


/**
 * LIST_HEAD_INIT - initialize a &struct list_head's links to point to itself
 * @name: name of the list_head
 */
#define LIST_HEAD_INIT(name) { &(name), &(name) }

/**
 * LIST_HEAD - definition of a &struct list_head with initialization values
 * @name: name of the list_head
 */
#define LIST_HEAD(name) struct list_head name = LIST_HEAD_INIT(name)

/**
 * INIT_LIST_HEAD - Initialize a list_head structure
 * @list: list_head structure to be initialized.
 *
 * Initializes the list_head to point to itself.  If it is a list header,
 * the result is an empty list.
 */
static inline void INIT_LIST_HEAD(struct list_head *list) {
    list->next = list;
    list->prev = list;
}

/*
 * Insert a new entry between two known consecutive entries.
 *
 * This is only for internal list manipulation where we know
 * the prev/next entries already!
 */
static inline void __list_add(struct list_head *_new, struct list_head *prev, struct list_head *next) {
    next->prev = _new;
    _new->next  = next;
    _new->prev  = prev;
    prev->next = _new;
}

/**
 * list_add - add a new entry
 * @new: new entry to be added
 * @head: list head to add it after
 *
 * Insert a new entry after the specified head.
 * This is good for implementing stacks.
 */
static inline void list_add(struct list_head *_new, struct list_head *head) {
    __list_add(_new, head, head->next);
}

/**
 * list_add_tail - add a new entry
 * @new: new entry to be added
 * @head: list head to add it before
 *
 * Insert a new entry before the specified head.
 * This is useful for implementing queues.
 */
static inline void list_add_tail(struct list_head *_new, struct list_head *head) {
    __list_add(_new, head->prev, head);
}

/*
 * Delete a list entry by making the prev/next entries
 * point to each other.
 *
 * This is only for internal list manipulation where we know
 * the prev/next entries already!
 */
static inline void __list_del(struct list_head *prev, struct list_head *next) {
    next->prev = prev;
    prev->next = next;
}

/**
 * list_del - deletes entry from list.
 * @entry: the element to delete from the list.
 * Note: list_empty() on entry does not return true after this, the entry is
 * in an undefined state.
 */
static inline void list_del(struct list_head *entry) {
    __list_del(entry->prev, entry->next);
    entry->next = entry;
    entry->prev = entry;
}

/**
 * list_is_last - tests whether @list is the last entry in list @head
 * @list: the entry to test
 * @head: the head of the list
 */
static inline int list_is_last(const struct list_head *list, const struct list_head *head) {
    return list->next == head;
}

/**
 * list_empty - tests whether a list is empty
 * @head: the list to test.
 */
static inline int list_empty(const struct list_head *head) {
    return head->next == head;
}

/**
 * list_next - get the next list_head in list
 * @current: list_head whose next item is requested.
 */
static inline struct list_head *list_next(const struct list_head *current) {
    return list_empty(current) ? NULL : current->next;
}
/**
 * list_prev - get the prev list_head in list
 * @current: list_head whose previous list_head is requested.
 */
static inline struct list_head *list_prev(const struct list_head *current) {
    return list_empty(current) ? NULL : current->prev;
}

/**
 * list_for_each - iterate over a list
 * @pos: the &struct list_head to use as a loop cursor.
 * @head: the head for your list.
 */
#define list_for_each(pos, head) \
    for (pos = (head)->next; pos != (head); pos = pos->next)

/**
 * list_for_each_prev - iterate over a list backwards
 * @pos: the &struct list_head to use as a loop cursor.
 * @head: the head for your list.
 */
#define list_for_each_prev(pos, head) \
    for (pos = (head)->prev; pos != (head); pos = pos->prev)

/**
 * list_for_each_entry - iterate over list of given type
 * @pos: the type * to use as a loop cursor.
 * @head: the head for your list.
 * @member: the name of the list_head within the struct.
 */
#define list_for_each_entry(pos, head, member) \
    for (pos = container_of((head)->next, typeof(*pos), member); \
         &pos->member != (head); \
         pos = container_of(pos->member.next, typeof(*pos), member)) \

/**
 * list_for_each_entry_reverse - iterate backwards over list of given type.
 * @pos: the type * to use as a loop cursor.
 * @head: the head for your list.
 * @member: the name of the list_head within the struct.
 */
#define list_for_each_entry_reverse(pos, head, member) \
    for (pos = container_of((head)->prev, typeof(*pos), member); \
         &pos->member != (head); \
         pos = container_of(pos->member.prev, typeof(*pos), member))


/**
 * list_for_each_entry_safe - iterate over list of given type safe against removal of list entry
 * @pos:	the type * to use as a loop cursor.
 * @n:		another type * to use as temporary storage
 * @head:	the head for your list.
 * @member:	the name of the list_head within the struct.
 */
#define list_for_each_entry_safe(pos, n, head, member) \
    for (pos = container_of((head)->next, typeof(*pos), member), \
         n = container_of((pos)->member.next, typeof(*(pos)), member); \
         &(pos)->member != (head); \
         pos = n, n = container_of((pos)->member.next, typeof(*pos), member))

/**
 * list_for_each_entry_safe_reverse - iterate backwards over list safe against removal
 * @pos:	the type * to use as a loop cursor.
 * @n:		another type * to use as temporary storage
 * @head:	the head for your list.
 * @member:	the name of the list_head within the struct.
 *
 * Iterate backwards over list of given type, safe against removal
 * of list entry.
 */
#define list_for_each_entry_safe_reverse(pos, n, head, member) \
    for (pos = container_of((head)->prev, typeof(*pos), member), \
         n = container_of((pos)->member.prev, typeof(*(pos)), member); \
         &pos->member != (head); \
         pos = n, n = container_of((pos)->member.prev, typeof(*(pos)), member))

#define list_release(head, free, type, member) \
    do { \
        struct list_head *iter = (head)->next; \
        while (iter != (head)) { \
            struct list_head *next = iter->next; \
            free(container_of(iter, type, member)); \
            iter = next; \
        } \
    } while (0)

#endif
