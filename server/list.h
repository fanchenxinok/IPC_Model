#ifndef __S_LIST_H__
#define __S_LIST_H__
#include <pthread.h>

typedef struct listNode
{
    void *data;
    struct listNode *pNext;
}stListNode;

typedef struct
{
    int cnt;
    stListNode *pHead;
    stListNode *pRear;
	pthread_mutex_t lock;
}stList;

stList* list_create();
void list_destory(stList *pList);
void list_insert_first(stList* pList, void** data);
void list_insert_last(stList* pList, void **data);
void list_delete_node(stList* pList, void **data);

#endif