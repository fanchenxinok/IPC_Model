#ifndef __S_COMMON_H__
#define __S_COMMON_H__
#include <pthread.h>

typedef enum
{
	EPOLL_EVENT_ADD = 0,
	EPOLL_EVENT_DEL
}enEpollEvent;

void epoll_ctl_event(int epollFd, int ctlFd, int state, enEpollEvent ctlType);
void pthread_create_thread(pthread_t *thread_id, const char* thread_name,  void*(*worker)(void*), void* data);
void pthread_create_mutex(pthread_mutex_t *mutex);
void pthread_create_cond(pthread_cond_t *cond);
int calc_process_time(int *pStartMs);
#endif