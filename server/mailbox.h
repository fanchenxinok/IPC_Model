#ifndef __S_MAILBOX_H__
#define __S_MAILBOX_H__

/************************************************
*   Author:  Fanchenxin
*   Date:  2018/05/17
*************************************************/
#include "private_def.h"
#include "message.h"
/*************************************************/

#define MSG_MEM_SIZE      (sizeof(stMsg))
#define MAX_MAILBOX_NUM   (8 + 20) // services + clients
#define MAX_MAILBOX_SIZE  (128)
#define MSG_BUFFER_NUM    (MAX_MAILBOX_SIZE * MAX_MAILBOX_NUM)

typedef enum
{
	AVAIL_STATUS_FREE,
	AVAIL_STATUS_USED
}enAvailableStatus;

typedef enum
{
	LOOP_QUEUE_NORMAL,
	LOOP_QUEUE_FULL,
	LOOP_QUEUE_EMPTY
}enLoopQueueStatus;

typedef struct
{
	void *pAddr;
	struct stMsgBuffNode *pNext;
}stMsgBuffNode;

typedef struct
{
	stMsgBuffNode *pFreeList;
	stMsgBuffNode *pUsedList;
	stMsgBuffNode *pFreeListTail; // the tail pointer of free list
	volatile Int32 freeCnt;
	pthread_mutex_t mutex;
}stMsgBuffListMng;

typedef struct
{
	volatile Int32 readIdx;
	volatile Int32 writeIdx;
	void** ppQueue;
	Int32 maxCnt;
}stLoopQueue;

typedef struct{
	enAvailableStatus state;
	stLoopQueue *pLoopQueue;
	pthread_mutex_t mutex;
	pthread_cond_t  pushCond;
	pthread_cond_t  popCond;
}stMsgQueueMng;


Int32 mailbox_get_msgbuff(void **ppMsgBuff);
void mailbox_free_msgbuff(void *pMsgBuff);

Int32 mailbox_all_init();
void mailbox_all_destory();
Int32 mailbox_create(Int32 *pMailboxId, Int32 queueMaxCnt);
Int32 mailbox_destory(Int32 mailboxId);
uInt32 mailbox_get_free_space(Int32 mailboxId);
/* 	function: put the fb data into display cache
	timeOutMs :   -1  waitforever
			     >0  wait timeOutMs ms
	return: -1 (fail), 0(success)
*/
Int32 mailbox_send_msg(Int32 mailboxId, void *pFbMem, Int32 timeOutMs);

/* timeOutMs :   -1  waitforever
			     >0  wait timeOutMs ms
  return: -1 (fail), 0(success)
*/
Int32 mailbox_recv_msg(Int32 mailboxId, void **ppFbMem, Int32 timeOutMs);

#endif
