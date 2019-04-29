#include <stdarg.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include "log.h"
#include "private_def.h"
#include "network.h"

/*
    if LOG_THREAD_ON be set, output log by log thread,
    else output log through TCP to log receiver.
*/

#if LOG_THREAD_ON
#define LOOP_BUFF_MAX_IDX (128)
#define LOOP_BUFF_ITEAM_MAX_LEN (256)

typedef struct{
	volatile int readIdx;
	volatile int writeIdx;
	char buffer[LOOP_BUFF_MAX_IDX][LOOP_BUFF_ITEAM_MAX_LEN];
}stLoopBuff;

static stLoopBuff loopBuff;

static void initLoopBuff()
{
	loopBuff.readIdx = 0;
	loopBuff.writeIdx = 0;
	memset(loopBuff.buffer, '\0', sizeof(char)*LOOP_BUFF_MAX_IDX*LOOP_BUFF_ITEAM_MAX_LEN);
	return;
}

static enBOOL checkLoopBuffEmpty()
{
	return (loopBuff.readIdx == loopBuff.writeIdx) ? TRUE : FALSE;
}

static enBOOL checkLoopBuffFull()
{
	return ((loopBuff.writeIdx+1)%LOOP_BUFF_MAX_IDX == loopBuff.readIdx) ? TRUE : FALSE;
}

static void calcLoopBuffNextIdx(volatile int *pIndex)
{
	int newIndex = __sync_add_and_fetch(pIndex, 1);
	//printf("(1)*pIndex = %d\n", *pIndex);
    if (newIndex >= LOOP_BUFF_MAX_IDX){
		/*bool __sync_bool_compare_and_swap (type *ptr, type oldval type newval, ...)
		   if *ptr == oldval, use newval to update *ptr value */
        __sync_bool_compare_and_swap(pIndex, newIndex, newIndex % LOOP_BUFF_MAX_IDX);
		//printf("(2)*pIndex = %d\n", *pIndex);
    }
	return;
}

static void readLoopBuff(char *pOut)
{
	if(!pOut) return;
	if(checkLoopBuffEmpty()){
		printf("loop buff have not data to read, readIdx=%d,writeIdx=%d\n",
			loopBuff.readIdx,loopBuff.writeIdx);
		return;
	}
	memcpy(pOut, loopBuff.buffer[loopBuff.readIdx], sizeof(char)*LOOP_BUFF_ITEAM_MAX_LEN);
	calcLoopBuffNextIdx(&loopBuff.readIdx);
	return;
}

static void writeLoopBuff(char *pIn, int len)
{
	if(!pIn || len <= 0) return;
	if(checkLoopBuffFull()){
		printf("loop buff have not space to write, readIdx=%d,writeIdx=%d\n",
			loopBuff.readIdx,loopBuff.writeIdx);
		return;
	}
	len = (len > LOOP_BUFF_ITEAM_MAX_LEN) ? LOOP_BUFF_ITEAM_MAX_LEN : len;
	memcpy(loopBuff.buffer[loopBuff.readIdx], pIn, sizeof(char)*len);
	calcLoopBuffNextIdx(&loopBuff.writeIdx);
	return;
}

static pthread_t logThreadId;
static pthread_cond_t logTaskCond;
static pthread_mutex_t logTaskMutex = PTHREAD_MUTEX_INITIALIZER;

void* LogOutTask(void *arg)
{	
	char buffer[256] = {'\0'};
	SAFETY_MUTEX_LOCK(logTaskMutex);
	while(1){
		pthread_testcancel();
		/* if have no log to output wait.... */
        if(checkLoopBuffEmpty()){
			pthread_cond_wait(&logTaskCond, &logTaskMutex);
		}
		//printf("loopBuff.readIdx = %d\n", loopBuff.readIdx);
        strncpy(buffer, loopBuff.buffer[loopBuff.readIdx], 255);
		calcLoopBuffNextIdx(&loopBuff.readIdx);
		printf(buffer);
	}
	SAFETY_MUTEX_UNLOCK(logTaskMutex);
	return NULL;
}

void log_task_init()
{
	pthread_attr_t	attr;
	pthread_condattr_t attrCond;

	pthread_condattr_init(&attrCond);
	pthread_condattr_setclock(&attrCond, CLOCK_MONOTONIC);
	pthread_cond_init(&logTaskCond, &attrCond);
	pthread_attr_init(&attr);
	pthread_attr_setschedpolicy(&attr, SCHED_NORMAL);
    pthread_attr_setstacksize(&attr, PTHREAD_STACK_MIN);
    pthread_attr_setguardsize(&attr, getpagesize());
    pthread_create(&logThreadId, &attr, LogOutTask, NULL);
    pthread_setname_np(logThreadId, (const char*)"log_output");
	pthread_attr_destroy(&attr);
	return;
}

int log_task_fin()
{
    if (0 == pthread_cancel(logThreadId)){
        pthread_cond_broadcast(&logTaskCond);
        pthread_cond_destroy(&logTaskCond);
		pthread_join(logThreadId, NULL);
        printf("Log task finish success\n");
    } else {
        printf("Log task finish fail\n");
		return -1;
    }
    return 0;
}

#else
void log_network_init()
{
	network_create();
	network_start();
}

void log_network_fin()
{
	network_stop();
	network_destory();
}
#endif

int log_show_level_set = LOG_LEVEL_ERR;

void log_set_level(int level)
{
	log_show_level_set = level;
}

/* log out put */
void _LogOut_(int Level, const char Format[], ...)
{
	if(Level > log_show_level_set) return;
	char *pBuffer = NULL;
	#if LOG_THREAD_ON
	if(checkLoopBuffFull()){
		pthread_cond_signal(&logTaskCond);
		return;
	}
	//printf("loopBuff.writeIdx = %d\n", loopBuff.writeIdx);
	pBuffer = &loopBuff.buffer[loopBuff.writeIdx][0];
	#else
	char buffer[256] = {'\0'};
	pBuffer = &buffer[0];
	#endif
	va_list  ArgPtr;
    va_start(ArgPtr, Format);
	vsnprintf(pBuffer, 255, Format, ArgPtr);
	va_end(ArgPtr);

	#if LOG_THREAD_ON
	// 用log线程输出 目前如果log太多会出现锁死的情况
	/* send signal to logout task */
	calcLoopBuffNextIdx(&loopBuff.writeIdx);
    pthread_cond_signal(&logTaskCond);
	#else
	//printf(buffer);
	network_send_log(buffer);
	#endif
	return;
}

