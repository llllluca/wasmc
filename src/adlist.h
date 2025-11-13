#ifndef __ADLIST_H__
#define __ADLIST_H__

typedef struct listNode {
    struct listNode *prev;
    struct listNode *next;
    void *value;
} listNode;

typedef struct list {
    listNode *head;
    listNode *tail;
    void (*free)(void *ptr);
    unsigned long len;
} list;

/* Functions implemented as macros */
#define listLength(l) ((l)->len)
#define listFirst(l) ((l)->head)
#define listLast(l) ((l)->tail)
#define listPrevNode(n) ((n)->prev)
#define listNextNode(n) ((n)->next)
#define listNodeValue(n) ((n)->value)
#define listPush(l,v) listAddNodeTail((l), (v))

#define listSetFreeMethod(l,m) ((l)->free = (m))
#define listGetFreeMethod(l) ((l)->free)

/* Prototypes */
list *listCreate(void);
void listEmpty(list *list);
void listRelease(list *list);
list *listAddNodeHead(list *list, void *value);
list *listAddNodeTail(list *list, void *value);
void listLinkNodeHead(list *list, listNode *node);
void listLinkNodeTail(list *list, listNode *node);
listNode *listUnlinkNodeHead(list *list);
listNode *listUnlinkNodeTail(list *list);
void listDelNodeHead(list *list);
void listDelNodeTail(list *list);
void listInitNode(listNode *node, void *value);
listNode *listNext(listNode **iter);
listNode *listPrev(listNode **iter);
void listUnlinkNode(list *list, listNode *node);
void listDelNode(list *list, listNode *node);
void *listPop(list *list);
listNode *listIndex(list *list, long index);
list *listInsertNodeBefore(list *list, listNode *old_node, void *value);
list *listInsertNodeAfter(list *list, listNode *old_node, void *value);
list *listLinkNodeBefore(list *list, listNode *old_node, listNode *node);


#endif /* __ADLIST_H__ */
