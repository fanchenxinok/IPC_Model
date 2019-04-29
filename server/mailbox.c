#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <pthread.h>
#include <sys/epoll.h>
#include <stdarg.h>
#include <errno.h>
#include "mailbox.h"
#include "log.h"

#define MAILBOX_LOG_ERR(msg, ...)     LOG_ERR(LOG_COL_RED_YLW "[MAILBOX_ERR]"msg LOG_COL_END"\n", ##__VA_ARGS__)
#define MAILBOX_LOG_WARNING(msg, ...) LOG_WARNING(LOG_COL_RED_WHI "[MAILBOX_WARNING]"msg LOG_COL_END"\n", ##__VA_ARGS__)
#define MAILBOX_LOG_NOTIFY(msg, ...)   LOG_NOTIFY(LOG_COL_WHI_GRN "[MAILBOX_NOTIFY]"msg LOG_COL_END"\n", ##__VA_ARGS__)
#define MAILBOX_LOG_NONE

#define CHECK_POINTER_NULL(pointer, ret) \
    do{\
        if(pointer == NULL){ \
            MAILBOX_LOG_ERR("Check '%s' is NULL at:%u\n", #pointer, __LINE__);\
            return ret; \
        }\
    }while(0)


/*************************************************/
static stLoopQueue* CreateLoopQueue(Int32 queueMaxCnt)
{
	if(queueMaxCnt <= 0) return NULL;
	stLoopQueue *pLoopQueue = (stLoopQueue*)malloc(sizeof(stLoopQueue));
	if(pLoopQueue){
		pLoopQueue->readIdx = 0;
		pLoopQueue->writeIdx = 0;
		pLoopQueue->ppQueue = (void**)malloc(sizeof(void*) * (queueMaxCnt + 1));
		pLoopQueue->maxCnt = queueMaxCnt + 1;
	}
	return pLoopQueue;
}

static void DeleteLoopQueue(stLoopQueue *pLoopQueue)
{
	if(pLoopQueue){
		free(pLoopQueue->ppQueue);
		free(pLoopQueue);
		pLoopQueue = NULL;
	}
	return;
}

static void calcLoopQueueNextIdx(volatile int *pIndex, int maxCnt)
{
	CHECK_POINTER_VALID(pIndex);
	int newIndex = __sync_add_and_fetch(pIndex, 1);
	if (newIndex >= maxCnt){
		/*bool __sync_bool_compare_and_swap (type *ptr, type oldval type newval, ...)
		   if *ptr == oldval, use newval to update *ptr value */
		__sync_bool_compare_and_swap(pIndex, newIndex, newIndex % maxCnt);
	}
	return;
}

static enBOOL checkLoopQueueEmpty(stLoopQueue *pLoopQueue)
{
	CHECK_POINTER_NULL(pLoopQueue, FALSE);
	return (pLoopQueue->readIdx == pLoopQueue->writeIdx) ? TRUE : FALSE;
}

static enBOOL checkLoopQueueFull(stLoopQueue *pLoopQueue)
{
	CHECK_POINTER_NULL(pLoopQueue, FALSE);
	return ((pLoopQueue->writeIdx+1)%pLoopQueue->maxCnt== pLoopQueue->readIdx) ? TRUE : FALSE;
}

static enLoopQueueStatus checkLoopQueueStatus(stLoopQueue *pLoopQueue)
{
	CHECK_POINTER_NULL(pLoopQueue, LOOP_QUEUE_EMPTY);
	if(pLoopQueue->readIdx == pLoopQueue->writeIdx) {
		return LOOP_QUEUE_EMPTY;
	}
	else if(((pLoopQueue->writeIdx+1) % pLoopQueue->maxCnt) == pLoopQueue->readIdx) {
		return LOOP_QUEUE_FULL;
	}
	else {
		return LOOP_QUEUE_NORMAL;
	}
}

static uInt32 getLoopQueueNums(stLoopQueue *pLoopQueue)
{
	CHECK_POINTER_NULL(pLoopQueue, 0);
	return (pLoopQueue->writeIdx - pLoopQueue->readIdx + pLoopQueue->maxCnt) % pLoopQueue->maxCnt;
}

static enBOOL WriteLoopQueue(stLoopQueue *pLoopQueue, void **ppIn)
{
	CHECK_POINTER_NULL(pLoopQueue, FALSE);
	CHECK_POINTER_NULL(ppIn, FALSE);
	if(checkLoopQueueFull(pLoopQueue)){
		MAILBOX_LOG_NONE("loop queue have not enough space to write, readIdx=%d,writeIdx=%d",
			pLoopQueue->readIdx,pLoopQueue->writeIdx);
		return FALSE;
	}

	pLoopQueue->ppQueue[pLoopQueue->writeIdx] = *ppIn;
	MAILBOX_LOG_NOTIFY("write[%d]: 0x%x", pLoopQueue->writeIdx, *ppIn);
	calcLoopQueueNextIdx(&pLoopQueue->writeIdx, pLoopQueue->maxCnt);
	return TRUE;
}

static enBOOL ReadLoopQueue(stLoopQueue *pLoopQueue, void **ppOut)
{
	CHECK_POINTER_NULL(pLoopQueue, FALSE);
	CHECK_POINTER_NULL(ppOut, FALSE);
	if(checkLoopQueueEmpty(pLoopQueue)){
		//MAILBOX_LOG_WARNING("Loop queue is empty!!!");
		return FALSE;
	}
	*ppOut = pLoopQueue->ppQueue[pLoopQueue->readIdx];
	MAILBOX_LOG_NOTIFY("read[%d]: 0x%x", pLoopQueue->readIdx, *ppOut);
	calcLoopQueueNextIdx(&pLoopQueue->readIdx, pLoopQueue->maxCnt);
	return TRUE;
}

static void *pMsgBuffers = NULL;  // define for free memery
static stMsgBuffNode *pMsgBuffList = NULL; // define for free memery
static stMsgBuffListMng s_msg_list_mng;
static stMsgQueueMng s_msg_queue_mngs[MAX_MAILBOX_NUM];
static pthread_mutex_t s_msg_queue_mutex = PTHREAD_MUTEX_INITIALIZER;

static enBOOL mailbox_msgbuff_list_create()
{
	if(pMsgBuffers) return TRUE;
	pMsgBuffers = (void*)malloc(MSG_MEM_SIZE * MSG_BUFFER_NUM);
	if(!pMsgBuffers) return FALSE;
	
	pMsgBuffList = (stMsgBuffNode*)malloc(sizeof(stMsgBuffNode) * MSG_BUFFER_NUM);
	if(!pMsgBuffList){
		free(pMsgBuffers);
		pMsgBuffers = NULL;
		return FALSE;
	}

	uInt8 *pAddr = (uInt8*)pMsgBuffers;
	stMsgBuffNode *pNode = pMsgBuffList;
	
	s_msg_list_mng.freeCnt = MSG_BUFFER_NUM;
	s_msg_list_mng.pFreeList = pNode;
	s_msg_list_mng.pFreeList->pAddr = pAddr;
	s_msg_list_mng.pFreeList->pNext = NULL;

	pAddr += MSG_MEM_SIZE;
	pNode++;
	stMsgBuffNode* pCur = s_msg_list_mng.pFreeList;
	int i = 0;
	for(; i < MSG_BUFFER_NUM - 1; i++){
		stMsgBuffNode* pNew = pNode;
		pNew->pAddr = pAddr;
		pNew->pNext = NULL;
		pAddr += MSG_MEM_SIZE;
		pNode++;
		pCur->pNext = pNew;
		pCur = pNew;
	}
	s_msg_list_mng.pUsedList = NULL;
	s_msg_list_mng.pFreeListTail = pCur;
	
	pthread_mutex_init(&s_msg_list_mng.mutex, NULL);

	MAILBOX_LOG_NOTIFY("Msg buffer list create success, create %d msg buffers", MSG_BUFFER_NUM);
	return TRUE;
}


static void mailbox_msgbuff_list_destory()
{
	if(pMsgBuffers){
		free(pMsgBuffers);
		pMsgBuffers = NULL;
	}

	if(pMsgBuffList){
		free(pMsgBuffList);
		pMsgBuffList = NULL;
	}
	return;
}

Int32 mailbox_get_msgbuff(void **ppMsgBuff)
{
	Int32 ret = 0;
	SAFETY_MUTEX_LOCK(s_msg_list_mng.mutex);
	CHECK_POINTER_NULL(ppMsgBuff, FALSE);
	MAILBOX_LOG_NOTIFY("s_msg_list_mng.freeCnt = %d", s_msg_list_mng.freeCnt);
	if(s_msg_list_mng.freeCnt <= 0){
		MAILBOX_LOG_NONE("All the msg buffer has been used!!!");
		ret = -1;
	}
	else{
		*ppMsgBuff = s_msg_list_mng.pFreeList->pAddr;
		stMsgBuffNode *pUsed = s_msg_list_mng.pFreeList;
		s_msg_list_mng.pFreeList = s_msg_list_mng.pFreeList->pNext;
		s_msg_list_mng.freeCnt--;
		if(!s_msg_list_mng.pUsedList){
			pUsed->pNext = NULL;
			pUsed->pAddr = NULL;
			s_msg_list_mng.pUsedList = pUsed;
		}
		else{
			pUsed->pAddr = NULL;
			pUsed->pNext = s_msg_list_mng.pUsedList->pNext;
			s_msg_list_mng.pUsedList->pNext = pUsed;
		}
	}
	SAFETY_MUTEX_UNLOCK(s_msg_list_mng.mutex);
	MAILBOX_LOG_NOTIFY("Get Buffer : 0x%x", *ppMsgBuff);
	return ret;
}

void mailbox_free_msgbuff(void *pMsgBuff)
{
	CHECK_POINTER_NULL(pMsgBuff, FALSE);
	SAFETY_MUTEX_LOCK(s_msg_list_mng.mutex);
	stMsgBuffNode *pFree = s_msg_list_mng.pUsedList;
	s_msg_list_mng.pUsedList = s_msg_list_mng.pUsedList->pNext;
	pFree->pAddr = pMsgBuff;
	pFree->pNext = NULL;
	if(!s_msg_list_mng.pFreeList){
		s_msg_list_mng.pFreeList = pFree;
		s_msg_list_mng.pFreeListTail = s_msg_list_mng.pFreeList;
		s_msg_list_mng.pFreeListTail->pNext = NULL;
	}
	else{
		#if 1
		/* add to the free list tail */
		s_msg_list_mng.pFreeListTail->pNext = pFree;
		s_msg_list_mng.pFreeListTail = pFree;
		#else
		/* add to the free list head */
		pFree->pNext = s_msg_list_mng.pFreeList->pNext;
		s_msg_list_mng.pFreeList->pNext = pFree;
		#endif
	}
	s_msg_list_mng.freeCnt++;
	SAFETY_MUTEX_UNLOCK(s_msg_list_mng.mutex);
	MAILBOX_LOG_NOTIFY("Free Buffer : 0x%x", pMsgBuff);
	return;
}

Int32 mailbox_all_init()
{
	if(mailbox_msgbuff_list_create() == FALSE) { 
		MAILBOX_LOG_ERR("Mailbox all init fail");
		return -1;
	}
	int i = 0;
	for(i = 0; i < MAX_MAILBOX_NUM; i++){
		s_msg_queue_mngs[i].state = AVAIL_STATUS_FREE;
		s_msg_queue_mngs[i].pLoopQueue = NULL;
	}
	MAILBOX_LOG_NOTIFY("Mailbox init success, mailbox max num: %d", MAX_MAILBOX_NUM);
	return 0;
}

void mailbox_all_destory()
{
	mailbox_msgbuff_list_destory();

	int i= 0;
	for(i = 0; i < MAX_MAILBOX_NUM; i++) {
		if(s_msg_queue_mngs[i].state != AVAIL_STATUS_FREE) {
			s_msg_queue_mngs[i].state = AVAIL_STATUS_FREE;
			DeleteLoopQueue(s_msg_queue_mngs[i].pLoopQueue);
			pthread_cond_destroy(&s_msg_queue_mngs[i].pushCond);
			pthread_cond_destroy(&s_msg_queue_mngs[i].popCond);
		}
	}
}

Int32 mailbox_create(Int32 *pMailboxId, Int32 queueMaxCnt)
{
	Int32 ret = 0;
	Int32 mailboxId = 0;
	SAFETY_MUTEX_LOCK(s_msg_queue_mutex);
	for(; mailboxId < MAX_MAILBOX_NUM; mailboxId++) {
		if(s_msg_queue_mngs[mailboxId].state == AVAIL_STATUS_FREE) break;
	}
	
	if(mailboxId >= MAX_MAILBOX_NUM) {
		MAILBOX_LOG_ERR("Sorry all mailbox has been used, create mailbox FAIL!!!");
		*pMailboxId = -1;
		ret = -1;
	}
	else{
		pthread_condattr_t attrCond;
		s_msg_queue_mngs[mailboxId].state = AVAIL_STATUS_USED;
		pthread_mutex_init(&s_msg_queue_mngs[mailboxId].mutex, NULL);
		pthread_condattr_init(&attrCond);
		pthread_condattr_setclock(&attrCond, CLOCK_MONOTONIC);
		pthread_cond_init(&s_msg_queue_mngs[mailboxId].pushCond, &attrCond);
		pthread_cond_init(&s_msg_queue_mngs[mailboxId].popCond, &attrCond);
		queueMaxCnt = (queueMaxCnt > MAX_MAILBOX_SIZE) ? MAX_MAILBOX_SIZE : queueMaxCnt;
		s_msg_queue_mngs[mailboxId].pLoopQueue = CreateLoopQueue(queueMaxCnt);
		*pMailboxId = mailboxId;
	}
	SAFETY_MUTEX_UNLOCK(s_msg_queue_mutex);
	return ((ret != -1) && s_msg_queue_mngs[mailboxId].pLoopQueue) ? 0 : -1;
}

Int32 mailbox_destory(Int32 mailboxId)
{
	Int32 ret = 0;
	SAFETY_MUTEX_LOCK(s_msg_queue_mutex);
	if(mailboxId < 0 || mailboxId >= MAX_MAILBOX_NUM || 
		s_msg_queue_mngs[mailboxId].state != AVAIL_STATUS_USED){
		ret = -1;
	}
	else{
		s_msg_queue_mngs[mailboxId].state = AVAIL_STATUS_FREE;
		DeleteLoopQueue(s_msg_queue_mngs[mailboxId].pLoopQueue);
		pthread_cond_destroy(&s_msg_queue_mngs[mailboxId].pushCond);
		pthread_cond_destroy(&s_msg_queue_mngs[mailboxId].popCond);
	}
	SAFETY_MUTEX_UNLOCK(s_msg_queue_mutex);
	return ret;
}

uInt32 mailbox_get_free_space(Int32 mailboxId)
{
	if(mailboxId < 0 || mailboxId >= MAX_MAILBOX_NUM || 
		s_msg_queue_mngs[mailboxId].state != AVAIL_STATUS_USED){
		return 0;
	}
	return getLoopQueueNums(s_msg_queue_mngs[mailboxId].pLoopQueue);
}

/* 	function: put the fb data into display cache
	timeOutMs :   -1  waitforever
			     >0  wait timeOutMs ms
*/
Int32 mailbox_send_msg(Int32 mailboxId, void *pFbMem, Int32 timeOutMs)
{
	CHECK_POINTER_NULL(pFbMem, FALSE);
	Int32 ret = 0;
	if((mailboxId < 0) || (mailboxId >= MAX_MAILBOX_NUM)) {
		MAILBOX_LOG_WARNING("mailbox ID must in the range: [%d -  %d], current ID: %d", 0, MAX_MAILBOX_NUM-1, mailboxId);
		ret = -1;
	}
	else if(s_msg_queue_mngs[mailboxId].state == AVAIL_STATUS_FREE) {
		MAILBOX_LOG_WARNING("current mailbox is not be created");
		ret = -1;	
	}
	else {
		int os_result = 0;
		SAFETY_MUTEX_LOCK(s_msg_queue_mngs[mailboxId].mutex);
		while(WriteLoopQueue(s_msg_queue_mngs[mailboxId].pLoopQueue, &pFbMem) == FALSE){
			/* if loop queue is full */
			if(timeOutMs < 0){
				os_result = pthread_cond_wait(&s_msg_queue_mngs[mailboxId].pushCond, &s_msg_queue_mngs[mailboxId].mutex);
			}
			else{
				struct timespec tv;
				clock_gettime(CLOCK_MONOTONIC, &tv);
				tv.tv_nsec += (timeOutMs%1000)*1000*1000;
				tv.tv_sec += tv.tv_nsec/(1000*1000*1000) + timeOutMs/1000;
				tv.tv_nsec = tv.tv_nsec%(1000*1000*1000);
				os_result = pthread_cond_timedwait(&s_msg_queue_mngs[mailboxId].pushCond, &s_msg_queue_mngs[mailboxId].mutex, &tv);
			}

			if(os_result == ETIMEDOUT){
				MAILBOX_LOG_ERR("mailbox_send_msg time out!");
				ret = -1;
				break;
			}
		}
		SAFETY_MUTEX_UNLOCK(s_msg_queue_mngs[mailboxId].mutex);
		pthread_cond_signal(&s_msg_queue_mngs[mailboxId].popCond);
	}
	return ret;
}

/* timeOutMs :   -1  waitforever
			     >0  wait timeOutMs ms
*/
Int32 mailbox_recv_msg(Int32 mailboxId, void **ppFbMem, Int32 timeOutMs)
{
	Int32 ret = 0;
	if((mailboxId < 0) || (mailboxId >= MAX_MAILBOX_NUM)) {
		MAILBOX_LOG_WARNING("mailbox ID must in the range: [%d -  %d], current ID: %d", 0, MAX_MAILBOX_NUM-1, mailboxId);
		ret = -1;
	}
	else if(s_msg_queue_mngs[mailboxId].state == AVAIL_STATUS_FREE) {
		MAILBOX_LOG_WARNING("current mailbox is not be created");
		ret = -1;
	}
	else {
		int os_result = 0;
		SAFETY_MUTEX_LOCK(s_msg_queue_mngs[mailboxId].mutex);
		while(ReadLoopQueue(s_msg_queue_mngs[mailboxId].pLoopQueue, ppFbMem) == FALSE){
			/* if loop queue is empty */
			//MAILBOX_LOG_WARNING("Receive from Du FB Queue waiting.....");
			if(timeOutMs == -1){
				os_result = pthread_cond_wait(&s_msg_queue_mngs[mailboxId].popCond, &s_msg_queue_mngs[mailboxId].mutex);
			}
			else{
				struct timespec tv;
				clock_gettime(CLOCK_MONOTONIC, &tv);
				tv.tv_nsec += (timeOutMs%1000)*1000*1000;
				tv.tv_sec += tv.tv_nsec/(1000*1000*1000) + timeOutMs/1000;
				tv.tv_nsec = tv.tv_nsec%(1000*1000*1000);
				os_result = pthread_cond_timedwait(&s_msg_queue_mngs[mailboxId].popCond, &s_msg_queue_mngs[mailboxId].mutex, &tv);
			}
			//MAILBOX_LOG_WARNING("Receive from Du FB Queue wait Over.....");

			if(os_result == ETIMEDOUT){
				MAILBOX_LOG_ERR("mailbox_recv_msg time out!");
				ret = -1;
				break;
			}
		}
		SAFETY_MUTEX_UNLOCK(s_msg_queue_mngs[mailboxId].mutex);
		pthread_cond_signal(&s_msg_queue_mngs[mailboxId].pushCond);
	}
	return ret;
}

