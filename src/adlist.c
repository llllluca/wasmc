#include <stdlib.h>
#include "adlist.h"

/* Create a new list. The created list can be freed with
 * listRelease(), but private value of every node need to be freed
 * by the user before to call listRelease(), or by setting a free method using
 * listSetFreeMethod.
 *
 * On error, NULL is returned. Otherwise the pointer to the new list. */
list *listCreate(void)
{
    struct list *list;

    if ((list = malloc(sizeof(*list))) == NULL)
        return NULL;
    list->head = list->tail = NULL;
    list->len = 0;
    list->free = NULL;
    return list;
}

/* Remove all the elements from the list without destroying the list itself. */
void listEmpty(list *list)
{
    unsigned long len;
    listNode *current, *next;

    current = list->head;
    len = list->len;
    while(len--) {
        next = current->next;
        if (list->free) list->free(current->value);
        free(current);
        current = next;
    }
    list->head = list->tail = NULL;
    list->len = 0;
}

/* Free the whole list.
 *
 * This function can't fail. */
void listRelease(list *list)
{
    if (!list)
        return;
    listEmpty(list);
    free(list);
}


/* Add a new node to the list, to head, containing the specified 'value'
 * pointer as value.
 *
 * On error, NULL is returned and no operation is performed (i.e. the
 * list remains unaltered).
 * On success the 'list' pointer you pass to the function is returned. */
list *listAddNodeHead(list *list, void *value)
{
    listNode *node;

    if ((node = malloc(sizeof(*node))) == NULL)
        return NULL;
    node->value = value;
    listLinkNodeHead(list, node);
    return list;
}

/*
 * Add a node that has already been allocated to the head of list
 */
void listLinkNodeHead(list* list, listNode *node) {
    if (list->len == 0) {
        list->head = list->tail = node;
        node->prev = node->next = NULL;
    } else {
        node->prev = NULL;
        node->next = list->head;
        list->head->prev = node;
        list->head = node;
    }
    list->len++;
}

/* Add a new node to the list, to tail, containing the specified 'value'
 * pointer as value.
 *
 * On error, NULL is returned and no operation is performed (i.e. the
 * list remains unaltered).
 * On success the 'list' pointer you pass to the function is returned. */
list *listAddNodeTail(list *list, void *value)
{
    listNode *node;

    if ((node = malloc(sizeof(*node))) == NULL)
        return NULL;
    node->value = value;
    listLinkNodeTail(list, node);
    return list;
}

/*
 * Add a node that has already been allocated to the tail of list
 */
void listLinkNodeTail(list *list, listNode *node) {
    if (list->len == 0) {
        list->head = list->tail = node;
        node->prev = node->next = NULL;
    } else {
        node->prev = list->tail;
        node->next = NULL;
        list->tail->next = node;
        list->tail = node;
    }
    list->len++;
}

listNode *listUnlinkNodeHead(list *list) {

    if (list->head == NULL) return NULL;
    listNode *node = list->head;
    if (list->len == 1) {
        list->head = NULL;
        list->tail = NULL;
    } else {
        list->head = node->prev;
        list->head->prev = NULL;
    }
    node->next = NULL;
    node->prev = NULL;
    list->len--;
    return node;

}

listNode *listUnlinkNodeTail(list *list) {

    if (list->tail == NULL) return NULL;
    listNode *node = list->tail;
    if (list->len == 1) {
        list->head = NULL;
        list->tail = NULL;
    } else {
        list->tail = node->prev;
        list->tail->next = NULL;
    }
    node->next = NULL;
    node->prev = NULL;
    list->len--;
    return node;
}

void listDelNodeHead(list *list) {
    listNode *head = listUnlinkNodeHead(list);
    if (list->free) list->free(head->value);
    free(head);
}

void listDelNodeTail(list *list) {
    listNode *head = listUnlinkNodeHead(list);
    if (list->free) list->free(head->value);
    free(head);
}

/*
 * Remove the specified node from the list without freeing it.
 */
void listUnlinkNode(list *list, listNode *node) {
    if (node->prev)
        node->prev->next = node->next;
    else
        list->head = node->next;
    if (node->next)
        node->next->prev = node->prev;
    else
        list->tail = node->prev;

    node->next = NULL;
    node->prev = NULL;

    list->len--;
}


/* Remove the specified node from the specified list.
 * The node is freed. If free callback is provided the value is freed as well.
 *
 * This function can't fail. */
void listDelNode(list *list, listNode *node)
{
    listUnlinkNode(list, node);
    if (list->free) list->free(node->value);
    free(node);
}

listNode *listNext(listNode **iter) {
    listNode *current = *iter;
    if (current != NULL) {
        *iter = current->next;
    }
    return current;
}

listNode *listPrev(listNode **iter) {
    listNode *current = *iter;
    if (current != NULL) {
        *iter = current->prev;
    }
    return current;
}

void *listPop(list *list) {
    listNode *top = listUnlinkNodeTail(list);
    void *value = top->value;
    free(top);
    return value;
}

/* Return the element at the specified zero-based index
 * where 0 is the head, 1 is the element next to head
 * and so on. Negative integers are used in order to count
 * from the tail, -1 is the last element, -2 the penultimate
 * and so on. If the index is out of range NULL is returned. */
listNode *listIndex(list *list, long index) {
    listNode *n;

    if (index < 0) {
        index = (-index)-1;
        n = list->tail;
        while(index-- && n) n = n->prev;
    } else {
        n = list->head;
        while(index-- && n) n = n->next;
    }
    return n;
}


/* Initializes the node's value and sets its pointers
 * so that it is initially not a member of any list.
 */
void listInitNode(listNode *node, void *value) {
    node->prev = NULL;
    node->next = NULL;
    node->value = value;
}
