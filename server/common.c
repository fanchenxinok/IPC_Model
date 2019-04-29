#define _GNU_SOURCE

#include <stdlib.h> 
#include <stdio.h> 
#include <errno.h> 
#include <string.h>
#include <sys/wait.h>
#include <sys/epoll.h>
#include <time.h>

#include "common.h"

void epoll_ctl_event(int epollFd, int ctlFd, int state, enEpollEvent ctlType)
{
	struct epoll_event ev;
	ev.events = state;
	ev.data.fd = ctlFd;
	switch(ctlType) {
		case EPOLL_EVENT_ADD:
			epoll_ctl(epollFd, EPOLL_CTL_ADD, ctlFd, &ev);
			break;
		case EPOLL_EVENT_DEL:
			epoll_ctl(epollFd, EPOLL_CTL_DEL, ctlFd, &ev);
			break;
		default:
			break;
	}
	return;
}

void pthread_create_thread(pthread_t *thread_id, const char* thread_name,  void*(*worker)(void*), void* data)
{
	pthread_attr_t attr;
	pthread_attr_init (&attr);
	pthread_attr_setdetachstate (&attr, PTHREAD_CREATE_DETACHED);
	pthread_create(thread_id, &attr, worker, data);
	int size = strlen(thread_name) + 1;
	char name[16] = { '\0'};
	size = (size > 16) ? 16 : size;
	strncpy(name, thread_name, size);
	name[size - 1] = '\0';
	printf("Create Thread name : %s \n", name);
	pthread_setname_np(*thread_id, name); // thread name must less than 16 bytes
	pthread_attr_destroy (&attr);
}

void pthread_create_mutex(pthread_mutex_t *mutex)
{
	pthread_mutexattr_t attr;
	pthread_mutexattr_init(&attr);
	pthread_mutexattr_settype(&attr , PTHREAD_MUTEX_RECURSIVE);
	pthread_mutex_init(mutex, &attr);
	pthread_mutexattr_destroy(&attr);
}

void pthread_create_cond(pthread_cond_t *cond)
{
	pthread_condattr_t attrCond;
	pthread_condattr_init(&attrCond);
	pthread_condattr_setclock(&attrCond, CLOCK_MONOTONIC);
	pthread_cond_init(cond, &attrCond);	
}

int calc_process_time(int *pStartMs)
{
	struct timespec endTime;
	clock_gettime(CLOCK_REALTIME, &endTime);
	if(pStartMs){
		return endTime.tv_sec * 1000 + endTime.tv_nsec / 1000000 - *pStartMs;
	}
	return endTime.tv_sec * 1000 + endTime.tv_nsec / 1000000;
}


