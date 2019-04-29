#include <stdio.h>  
#include <stdlib.h>  
#include <string.h>  
#include <sys/types.h>  
#include <sys/socket.h>  
#include <arpa/inet.h>  
#include <unistd.h>  
#include <signal.h>
#include <time.h>
#include <sys/timerfd.h>
#include <pthread.h>
#include <sys/epoll.h>

#include "list.h"
#include "message.h"
#include "heartbeat.h"
#include "socket.h"
#include "common.h"

#define UDP_PORT_NUM   (8888)
#define MSG_HEART_BEAT (0x10101010)

struct hbServer
{
	int socket_fd;
	int epoll_fd;
	int timer_fd;
	int hb_timeout;  // ms
	pthread_t monitor_thread_id;
	pthread_t hbrecv_thread_id;
	stList *pHbList;
	pfOffLineCb offline_cb;
	pthread_mutex_t mutex_lock;
};

typedef struct
{
	int hb_id;
	int socket_fd;
	int epoll_fd;
	int hb_period;
	struct sockaddr_in socket_addr;
}stHbClient;

typedef enum
{
	HB_OFF_LINE = 0,
	HB_ON_LINE
}enHbStatus;

typedef struct
{
	int hb_id;
	enHbStatus hb_status;
	struct timespec hb_time;
}stHbInfo;

static void* heartbeat_find_client(stList *pList, int hb_id)
{
	if(!pList) return NULL;
	stListNode *pCur = pList->pHead->pNext;
	while(pCur) {
		stHbInfo *pHbInfo = (stHbInfo*)pCur->data;
		if(pHbInfo && pHbInfo->hb_id == hb_id){
			//printf("[hb] find client: %d\n", hb_id);
			return (void*)pHbInfo;
		}
		pCur = pCur->pNext;
	}
	return NULL;
}

static void heartbeat_add_client(stList *pList, int hb_id)
{
	if(!pList) return;
	stHbInfo *pHbInfo = (stHbInfo*)malloc(sizeof(stHbInfo));
    pHbInfo->hb_id = hb_id;
	pHbInfo->hb_status = HB_ON_LINE;
	clock_gettime(CLOCK_REALTIME, &pHbInfo->hb_time);
    printf("===pHbInfo: %p, hb id: %d\n", pHbInfo, pHbInfo->hb_id);
    list_insert_last(pList, (void**)&pHbInfo);
}

static void heartbeat_delete_client(stList *pList, int hb_id)
{
	if(!pList) return;
	stHbInfo *pHbInfo = heartbeat_find_client(pList, hb_id);
	if(pHbInfo) {
		list_delete_node(pList, (void*)&pHbInfo);
	}
	else {
		printf("[hb]heartbeat_delete_client, can not find hb_id: %d\n", hb_id);
	}
}

static void heartbeat_release_list(stList *pList)
{
	if(!pList) return;
	stListNode *pCur = pList->pHead->pNext;
	while(pCur) {
		stHbInfo *pHbInfo = (stHbInfo*)pCur->data;
		//release something
		pCur = pCur->pNext;
	}
}

static void heartbeat_show_clients(stList *pList)
{
	printf("###################################\n");
	printf("HB Server have %d clients:\n", pList->cnt);
    stListNode *pNode = pList->pHead->pNext;
    while(pNode)
    {
        stHbInfo *pHbInfo = (stHbInfo*)pNode->data;
        if(pHbInfo) {
            printf("[hb client: %p] ID: %d, time: [%lu:%lu]\n",
				pHbInfo, pHbInfo->hb_id, pHbInfo->hb_time.tv_sec, pHbInfo->hb_time.tv_nsec);
        }
        else {
            printf("pHbInfo is NULL\n");
        }
        pNode = pNode->pNext;
    }

	printf("###################################\n");
}

void* heartbeat_monitor(void* user)
{
	stHbServer *pHbServer = (stHbServer*)user;
	struct epoll_event events[1];
	int cnt = 0;
	while(1)
	{
		pthread_testcancel();
		int ms = 0;
		//ms = calc_process_time(NULL);
		int fireEvents = epoll_wait(pHbServer->epoll_fd, events, 1, -1);
		//ms = calc_process_time(&ms);
		if(fireEvents > 0){
			//printf("monitor time out: %d ms\n", ms);
			uint64_t exp;
			ssize_t size = read(events[0].data.fd, &exp, sizeof(uint64_t));
			if(size != sizeof(uint64_t)) {
				printf("read error!\n");
			}

			struct timespec nowTime;
			clock_gettime(CLOCK_REALTIME, &nowTime);
			
			pthread_mutex_lock(&pHbServer->mutex_lock);
			stListNode *pCur = pHbServer->pHbList->pHead->pNext;
			while(pCur) {
				stHbInfo *pHbInfo = (stHbInfo*)pCur->data;
				if(pHbInfo->hb_status == HB_ON_LINE){
					int duration = (nowTime.tv_sec * 1000 + nowTime.tv_nsec / 1000000) - (pHbInfo->hb_time.tv_sec * 1000 + pHbInfo->hb_time.tv_nsec / 1000000);
					if (duration > pHbServer->hb_timeout) {
						pHbInfo->hb_status = HB_OFF_LINE;
						printf("[monitor]client: %d had off line\n", pHbInfo->hb_id);
						if(pHbServer->offline_cb) {
							pHbServer->offline_cb(pHbInfo->hb_id, NULL);
						}

						list_delete_node(pHbServer->pHbList, (void*)&pHbInfo);
						heartbeat_show_clients(pHbServer->pHbList);
					}
				}
				pCur = pCur->pNext;
			}
			pthread_mutex_unlock(&pHbServer->mutex_lock);

			//printf("All client check finished\n");
			
		}
		else{
			printf("fireEvents = %d\n", fireEvents);
		}
	}
	return NULL;
}


static void* heartbeat_recv(void* user)
{
	stHbServer *pHbServer = (stHbServer*)user;
	struct sockaddr_in client_addr;  
    int client_addrlen = sizeof(struct sockaddr);
	while(1) {
		pthread_testcancel();
		stMsg msg = {0};
		if (recvfrom(pHbServer->socket_fd, &msg, sizeof(msg), 0, (struct sockaddr *)&client_addr, &client_addrlen) < 0){  
	        perror("hb recv: failed to recvform client\n");  
	        return NULL;  
	    }
		else {
			if(msg.msg_type == MSG_HEART_BEAT) {
				stHbInfo *pHbInfo = heartbeat_find_client(pHbServer->pHbList, msg.owner);
				if(pHbInfo) {
					pHbInfo->hb_status = HB_ON_LINE;
					clock_gettime(CLOCK_REALTIME, &pHbInfo->hb_time);
				}
				else {
					pthread_mutex_lock(&pHbServer->mutex_lock);
					heartbeat_add_client(pHbServer->pHbList, msg.owner);
					pthread_mutex_unlock(&pHbServer->mutex_lock);
				}
			}
		}
	}
	return NULL;
}

static void* heartbeat_send(void* user)
{
	stHbClient hb_client = {0};
	stHbClient *pHbClient = &hb_client;
	memcpy(pHbClient, (stHbClient*)user, sizeof(stHbClient));
	free(user);
	printf("[pHbClient: %d] epoll fd = %d, socket fd = %d, hb period = %d\n", 
		pHbClient->hb_id, pHbClient->epoll_fd, pHbClient->socket_fd, pHbClient->hb_period);
	while(1)
	{
		struct epoll_event events[1];
		int ms = 0;
		ms = calc_process_time(NULL);
		int fireEvents = epoll_wait(pHbClient->epoll_fd, events, 1, -1);
		ms = calc_process_time(&ms);
		if(fireEvents > 0){
			printf("hb id: %d, heart beat period = %d ms\n", 
				pHbClient->hb_id, pHbClient->hb_period);
			uint64_t exp;
			ssize_t size = read(events[0].data.fd, &exp, sizeof(uint64_t));
			if(size != sizeof(uint64_t)) {
				printf("read error!\n");
			}

			stMsg msg = {0};
			msg.msg_type = MSG_HEART_BEAT;
			msg.owner = pHbClient->hb_id;
		  
		    if (sendto(pHbClient->socket_fd, &msg, sizeof(stMsg), 0,   
		                (struct sockaddr *)&pHbClient->socket_addr, sizeof(struct sockaddr)) < 0){  
		        perror("failed to send online message");  
		        break;  
		    }  
		}
		else{
			printf("[hb] heart_beat_send: fireEvents = %d\n", fireEvents);
		}
	}
	return NULL;
}

// hb_timeout (ms)
stHbServer* heartbeat_server_create(
	int port_num,
	int hb_timeout,  //ms
	pfOffLineCb offline_cb)
{
	int socket_fd = -1;  
  	if((socket_fd = socket_udp_server(port_num)) < 0) {
		fprintf(stderr, "[hb] create udp socket fail!\n");
		return NULL;
  	}

	/* create epoll */
	int epoll_fd = -1;
	epoll_fd = epoll_create(1);
	/* create timer */
	int timer_fd = -1;
	timer_fd = timerfd_create(CLOCK_MONOTONIC, TFD_CLOEXEC | TFD_NONBLOCK);
	
	/* add timer fd to epoll monitor event */
	epoll_ctl_event(epoll_fd, timer_fd, EPOLLIN, EPOLL_EVENT_ADD);

	/* create timer */
	struct itimerspec its;
	its.it_interval.tv_sec = hb_timeout / 1000;
	its.it_interval.tv_nsec = hb_timeout % 1000 * 1000 * 1000;
	its.it_value.tv_sec = hb_timeout / 1000;
	its.it_value.tv_nsec = hb_timeout % 1000 * 1000 * 1000;
	if (timerfd_settime(timer_fd, 0, &its, NULL) < 0) {
		fprintf(stderr, "timerfd_settime error\n");
		goto FAIL;
	}

	/* create thread to monitor */
	stHbServer *pHbServer = (stHbServer*)malloc(sizeof(stHbServer));
	pthread_create_mutex(&pHbServer->mutex_lock);
	
	pHbServer->socket_fd = socket_fd;
	pHbServer->epoll_fd = epoll_fd;
	
	pHbServer->timer_fd = timer_fd;
	pHbServer->offline_cb = offline_cb;
	pHbServer->hb_timeout = hb_timeout;
	pHbServer->pHbList = list_create();
	pthread_t thread_id = -1;
	pthread_create_thread(&thread_id, "hb_monitor", &heartbeat_monitor, (void*)pHbServer);
	pHbServer->monitor_thread_id = thread_id;
	pthread_create_thread(&thread_id, "hb_recv", &heartbeat_recv, (void*)pHbServer);
	pHbServer->hbrecv_thread_id = thread_id;
	printf("[hb] heart beat server create success\n");
	return pHbServer;

FAIL:
	close(socket_fd);
	close(epoll_fd);
	close(timer_fd);
	free(pHbServer);
	return NULL;
}

void heartbeat_server_destory(stHbServer *pHbServer)
{
	if(!pHbServer) return;
	while(1) {
		printf("[hb]monitor thread id: 0x%x\n", pHbServer->monitor_thread_id);
		if (0 == pthread_cancel(pHbServer->monitor_thread_id)){
			pthread_join(pHbServer->monitor_thread_id, NULL);
	        printf("[hb]monitor thread finish success\n");
			break;
	    } else {
	        printf("[hb]monitor thread finish fail\n");
			usleep(100000); // retry
	    }
	}

	while(1) {
		printf("[hb]heart beat recv thread id: 0x%x\n", pHbServer->hbrecv_thread_id);
		if (0 == pthread_cancel(pHbServer->hbrecv_thread_id)){
			pthread_join(pHbServer->hbrecv_thread_id, NULL);
	        printf("[hb]hbrecv thread finish success\n");
			break;
	    } else {
	        printf("[hb]hbrecv thread finish fail\n");
			usleep(100000); // retry
	    }
	}

	close(pHbServer->socket_fd);
	close(pHbServer->epoll_fd);
	close(pHbServer->timer_fd);

	list_destory(pHbServer->pHbList);

	free(pHbServer);
	pHbServer = NULL;
}

// hb_period (ms)
int heartbeat_client_create(
	const char* ip_addr, 
	int port_num,
	int hb_id,  // this is the same to user id
	int hb_period)
{
    int socket_fd = -1;
	struct sockaddr_in server_addr;
    if((socket_fd = socket_udp_client(ip_addr, port_num, &server_addr)) < 0) {
		fprintf(stderr, "[hb] connet to udp socket fail!\n");
		return -1;
    }

	/* create epoll */
	int epoll_fd = -1;
	epoll_fd = epoll_create(1);
	/* create timer */
	int timer_fd = -1;
	timer_fd = timerfd_create(CLOCK_MONOTONIC, TFD_CLOEXEC | TFD_NONBLOCK);
	
	/* add timer fd to epoll monitor event */
	epoll_ctl_event(epoll_fd, timer_fd, EPOLLIN, EPOLL_EVENT_ADD);

	/* create heart beat thread */
	stHbClient *pHbClient = (stHbClient*)malloc(sizeof(stHbClient));
	pthread_t thread_id = -1;
	pHbClient->epoll_fd = epoll_fd;
	pHbClient->socket_fd = socket_fd;
	pHbClient->socket_addr = server_addr;
	pHbClient->hb_id = hb_id;
	pHbClient->hb_period = hb_period;
	char name[256] = {'\0'};
	sprintf(name, "hb_send%03d", hb_id);
	pthread_create_thread(&thread_id, name, &heartbeat_send, (void*)pHbClient);

	/* create timer */
	struct itimerspec its;
	its.it_interval.tv_sec = hb_period / 1000;
	its.it_interval.tv_nsec = hb_period % 1000 * 1000 * 1000;
	its.it_value.tv_sec = hb_period / 1000;
	its.it_value.tv_nsec = hb_period % 1000 * 1000 * 1000;
	if (timerfd_settime(timer_fd, 0, &its, NULL) < 0) {
		fprintf(stderr, "timerfd_settime error\n");
		close(epoll_fd);
		close(socket_fd);
		close(timer_fd);
		return -1;
	}
	
	printf("[hb client: %d] epoll fd = %d, socket fd = %d, hb period = %d\n", 
		pHbClient->hb_id, pHbClient->epoll_fd, pHbClient->socket_fd, pHbClient->hb_period);
	return 0;
}

