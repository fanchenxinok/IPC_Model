#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <pthread.h>
#include <sys/epoll.h>
#include "list.h"
#include "message.h"
#include "socket.h"
#include "heartbeat.h"
#include "common.h"

/* this network is only used to send log text to clients */
#define PORT_NUM  (3333)
#define MAX_USER_WAIT (6)
#define USER_CONNECT_MSG_TYPE   	(0X22446688)
#define USER_DISCONN_MSG_TYPE	    (0x88664422)

#define HB_PORT_NUM (8888)
#define HB_CHECK_PERIOD (10000)  // 10000 ms

 typedef enum
 {
	 NETWORK_INIT = 0,
	 NETWORK_READY,
	 NETWORK_RUN
 }enNetworkStatus;

 typedef struct
{
	int socket_fd; //server socket fd
	int epoll_fd;
	enNetworkStatus status;
	stList *pUser_list;
	pthread_mutex_t mutex_lock;
	pthread_cond_t run_cond;
	pthread_t thread_id;
}stNetwork;

typedef struct
{
	int user_id;
	int socket_fd;
}stUserInfo;

static stNetwork* s_network = NULL;
static stHbServer *pHbServer = NULL;

static void network_set_handle(stNetwork *pNetwork)
{
	s_network = pNetwork;
}

static stNetwork* network_get_handle()
{
	return s_network;
}

void network_show_ip_addrs()
{
    socket_show_ip_addrs();
}

static void network_accept_user(stNetwork *pNetwork)
{
	int new_fd = -1;
    if((new_fd = socket_inet_accept(pNetwork->socket_fd)) != -1) {
    	epoll_ctl_event(pNetwork->epoll_fd, new_fd, EPOLLIN | EPOLLRDHUP, EPOLL_EVENT_ADD);
    }
}

static void* network_find_user(int user_id)
{
	stNetwork *pNetwork = network_get_handle();
	if(!pNetwork) return NULL;
	stListNode *pCur = pNetwork->pUser_list->pHead->pNext;
	while(pCur) {
		stUserInfo *pUserInfo = (stUserInfo*)pCur->data;
		if(pUserInfo && pUserInfo->user_id == user_id){
			printf("[network] find user: %d\n", user_id);
			return (void*)pUserInfo;
		}
		pCur = pCur->pNext;
	}
	return NULL;
}

static void* network_find_user_by_socketfd(int socket_fd)
{
	stNetwork *pNetwork = network_get_handle();
	if(!pNetwork) return NULL;
	stListNode *pCur = pNetwork->pUser_list->pHead->pNext;
	while(pCur) {
		stUserInfo *pUserInfo = (stUserInfo*)pCur->data;
		if(pUserInfo && pUserInfo->socket_fd == socket_fd){
			printf("[network] find user socket fd: %d\n", socket_fd);
			return (void*)pUserInfo;
		}
		pCur = pCur->pNext;
	}
	return NULL;
}

static void network_add_user(int socket_fd, stMsg *pMsg)
{
	stNetwork *pNetwork = network_get_handle();
	if(!pNetwork) return;
	pthread_mutex_lock(&pNetwork->mutex_lock);
    stUserInfo *pUserInfo = (stUserInfo*)malloc(sizeof(stUserInfo));
    pUserInfo->user_id = pMsg->owner;
    pUserInfo->socket_fd = socket_fd;
    printf("===pUserInfo: %p, user id: %d\n", pUserInfo, pUserInfo->user_id);
    list_insert_last(pNetwork->pUser_list, (void**)&pUserInfo);
	pthread_mutex_unlock(&pNetwork->mutex_lock);
}

static void network_show_users()
{
	printf("**************************************** \n");
    stNetwork *pNetwork = network_get_handle();
	printf("Network have %d users:\n", pNetwork->pUser_list->cnt);
    if(pNetwork) {
        stListNode *pNode = pNetwork->pUser_list->pHead->pNext;
        while(pNode)
        {
            stUserInfo *pUserInfo = (stUserInfo*)pNode->data;
            if(pUserInfo) {
                printf("[user: %p] ID: %d, socket fd: %d\n", pUserInfo, pUserInfo->user_id, pUserInfo->socket_fd);
            }
            else {
                printf("pUserInfo is NULL\n");
            }
            pNode = pNode->pNext;
        }
    }
    else {
        fprintf(stderr, "network has not create!\n");
    }
	printf("**************************************** \n");
}

static void network_delete_user(int user_id)
{
	stNetwork *pNetwork = network_get_handle();
	if(!pNetwork) return;
	pthread_mutex_lock(&pNetwork->mutex_lock);
	stUserInfo *pUserInfo = (stUserInfo*)network_find_user(user_id);
	if(pUserInfo) {
		printf("[network] network delete user socket fd: %d\n", pUserInfo->socket_fd);
		epoll_ctl_event(pNetwork->epoll_fd, pUserInfo->socket_fd, EPOLLIN | EPOLLRDHUP, EPOLL_EVENT_DEL);
		close(pUserInfo->socket_fd);
		list_delete_node(pNetwork->pUser_list, (void*)&pUserInfo);
	}
	pthread_mutex_unlock(&pNetwork->mutex_lock);
}

static void network_hb_callback(int user_id, void *user)
{
	network_delete_user(user_id);
}

static void* network_run(void *data)
{
	stNetwork* pNetwork = (stNetwork*)data;
	printf("[network] server wait running.....\n");
	pNetwork->epoll_fd = epoll_create(MAX_USER_WAIT);
	epoll_ctl_event(pNetwork->epoll_fd, pNetwork->socket_fd, EPOLLIN, EPOLL_EVENT_ADD);
	struct epoll_event events[MAX_USER_WAIT];
    while(1)
    {
		pthread_testcancel();
		pthread_mutex_lock(&pNetwork->mutex_lock);
		while(pNetwork->status != NETWORK_RUN) {
			printf("[network] server stop.....\n");
			pthread_cond_wait(&pNetwork->run_cond, &pNetwork->mutex_lock);
			printf("[network] server start running.....\n");
		}
		pthread_mutex_unlock(&pNetwork->mutex_lock);

		memset(events, 0, sizeof(struct epoll_event) * MAX_USER_WAIT);
		int eventNum = epoll_wait(pNetwork->epoll_fd, events, MAX_USER_WAIT, -1);

		pthread_mutex_lock(&pNetwork->mutex_lock);
		int i = 0;
		for(; i < eventNum; i++){
			if(events[i].events & EPOLLIN) {
				if(events[i].data.fd == pNetwork->socket_fd){
					network_accept_user(pNetwork);
				}
				else{	
					if(events[i].events & EPOLLRDHUP) {
						fprintf(stderr, "[network] user socket id: %d hand up.\n", events[i].data.fd);
						stUserInfo *pUserInfo = (stUserInfo*)network_find_user_by_socketfd(events[i].data.fd);
						if(pUserInfo) {
							network_delete_user(pUserInfo->user_id);
							network_show_users();
						}
						else {
							fprintf(stderr, "[network] can not find user by socketfd: %d\n", events[i].data.fd);
						}
						continue;
					}
					stMsg msg= {0};
					if(read(events[i].data.fd, &msg, sizeof(stMsg)) > 0){
						printf("[network]rec: client socket id: %d, msgType: %x\n", events[i].data.fd, msg.msg_type);
						/* get client information */
						switch(msg.msg_type)
						{
							case USER_CONNECT_MSG_TYPE:
								network_add_user(events[i].data.fd, &msg);
								network_show_users();
								break;
							case USER_DISCONN_MSG_TYPE:
								network_delete_user(msg.owner);
								network_show_users();
								break;
							default:
								break;
						}
					}
					else {
						fprintf(stderr, "[network]No data to read!\n");
					}
				}
			}
		}
		pthread_mutex_unlock(&pNetwork->mutex_lock);
    }
	
	printf("[network] server terminate.....\n");
	return NULL;
}

int network_create()
{
	stNetwork *pNetwork = network_get_handle();
	if(pNetwork) return 0; //only create once
	pNetwork = (stNetwork*)malloc(sizeof(stNetwork));
	if(!pNetwork) return -1;
	
	pthread_create_mutex(&pNetwork->mutex_lock);
	pthread_create_cond(&pNetwork->run_cond);

	pthread_mutex_lock(&pNetwork->mutex_lock);
	pNetwork->status = NETWORK_INIT;

	if((pNetwork->socket_fd = socket_inet_create(PORT_NUM, MAX_USER_WAIT)) < 0) {
		fprintf(stderr, "[network] create inet socket fail!\n");
		goto FAIL;
	}

	pNetwork->epoll_fd = -1;

	/* creat users list */
	pNetwork->pUser_list = list_create();
	pNetwork->status = NETWORK_READY;

	pthread_create_thread(&pNetwork->thread_id, "network_run", &network_run, (void*)pNetwork);
	printf("thread id: 0x%x\n", pNetwork->thread_id);

	network_set_handle(pNetwork);
    printf("Network create success.....\n");
    pthread_mutex_unlock(&pNetwork->mutex_lock);

	/* create heart beat monitor */
	pHbServer = heartbeat_server_create(HB_PORT_NUM, HB_CHECK_PERIOD, network_hb_callback);
	if(!pHbServer) {
		fprintf(stderr, "[network] create heart beat server fail!\n");
	}
	return 0;

FAIL:
	pthread_mutex_unlock(&pNetwork->mutex_lock);
	free(pNetwork);
	return -1;
}

void network_destory()
{
    stNetwork *pNetwork = network_get_handle();
	if(pNetwork){
	    pthread_mutex_lock(&pNetwork->mutex_lock);
		pNetwork->status = NETWORK_INIT;
		while(1) {
			printf("thread id: 0x%x\n", pNetwork->thread_id);
			if (0 == pthread_cancel(pNetwork->thread_id)){
				pthread_join(pNetwork->thread_id, NULL);
		        printf("network_run thread finish success\n");
				break;
		    } else {
		        printf("network_run thread finish fail\n");
				usleep(100000); // retry
		    }
		}

		/* destory client info */
		stListNode *pCur = pNetwork->pUser_list->pHead->pNext;
		while(pCur) {
			stUserInfo *pUserInfo = (stUserInfo*)pCur->data;
			if(pUserInfo){
				epoll_ctl_event(pNetwork->epoll_fd, pUserInfo->socket_fd, EPOLLIN, EPOLL_EVENT_DEL);
				close(pUserInfo->socket_fd);
			}
			pCur = pCur->pNext;
		}
		
		if(pNetwork->epoll_fd != -1) close(pNetwork->epoll_fd);
		close(pNetwork->socket_fd);
		list_destory(pNetwork->pUser_list);
		pthread_cond_destroy(&pNetwork->run_cond);
		pthread_mutex_unlock(&pNetwork->mutex_lock);
		free(pNetwork);
		network_set_handle(NULL);
	}

	heartbeat_server_destory(pHbServer);
	pHbServer = NULL;
	return;
}

void network_start()
{
	network_show_ip_addrs();
	stNetwork *pNetwork = network_get_handle();
	if(!pNetwork) return;
	pthread_mutex_lock(&pNetwork->mutex_lock);
	pNetwork->status = NETWORK_RUN;
	pthread_mutex_unlock(&pNetwork->mutex_lock);
	pthread_cond_signal(&pNetwork->run_cond);
}

void network_stop()
{
	stNetwork *pNetwork = network_get_handle();
	if(!pNetwork) return;
	pthread_mutex_lock(&pNetwork->mutex_lock);
	pNetwork->status = NETWORK_READY;
	pthread_mutex_unlock(&pNetwork->mutex_lock);
}

void network_send_log(const char* log)
{
	stNetwork *pNetwork = network_get_handle();
	if(!pNetwork) return;
	stListNode *pCur = pNetwork->pUser_list->pHead->pNext;
	while(pCur) {
		stUserInfo *pUserInfo = (stUserInfo*)pCur->data;
		if(pUserInfo){
			write(pUserInfo->socket_fd, log, 256);
		}
		pCur = pCur->pNext;
	}
}

