#define _GNU_SOURCE

#include <stdlib.h> 
#include <stdio.h> 
#include <errno.h> 
#include <string.h> 
#include <sys/types.h> 
#include <pthread.h>
#include <unistd.h>
#include <sys/wait.h>
#include <sys/epoll.h>

#include "server.h"
#include "socket.h"
#include "message.h"
#include "list.h"
#include "mailbox.h"
#include "log.h"
#include "arithmetic_service.h"
#include "common.h"

/*************************************************

功能：Linux上实现基于TCP和Unix domain Socket的本地IPC通信模型。

结构：采用CSS(client-server-service)模式，一个server负责管理client
	  和service服务。可以在server上注册多个service，service是可以自
	  定义及添加的，一个Client在connet到server时可以指定绑定到某个service，
	  这个service可以为client提供服务。一个service可以绑定多个client。
	  Client和server建立的是Unix domain socket链接，log接受器和server
	  建立的是TCP链接。

**************************************************/

#define SERVER_LOG_ERR(msg, ...)	    LOG_ERR(LOG_COL_RED_YLW "[SERVER_ERR]"msg LOG_COL_END"\n", ##__VA_ARGS__)
#define SERVER_LOG_WARNING(msg, ...)	LOG_WARNING(LOG_COL_RED_WHI "[SERVER_WARNING]"msg LOG_COL_END"\n", ##__VA_ARGS__)
#define SERVER_LOG_NOTIFY(msg, ...)		LOG_NOTIFY(LOG_COL_YLW_GRN "[SERVER_NOTIFY]"msg LOG_COL_END"\n", ##__VA_ARGS__)

#define SERVER_NAME "#server_local"
#define MAX_WAIT_CLIENT  	(6)  // max number of client of wait connect to server
#define CLIENT_CONNECT_MSG_TYPE   	(0XAABBCCDD)
#define CLIENT_DISCONN_MSG_TYPE	    (0xDDCCBBAA)

#define MAX_SERVICE_NUM (MAX_MAILBOX_NUM)

typedef enum
{
	SERVER_INIT = 0,
	SERVER_READY,
	SERVER_RUN
}enServerStatus;

typedef enum
{
	SERVICE_FREE = 0,
	SERVICE_BUSY
}stServiceStatus;

typedef enum
{
	CLIENT_CONNECT = 0,
	CLIENT_DISCONN,
	CLIENT_REBIND_SERVICE
}enClientStatus;

typedef struct
{
	int socket_fd; //server socket fd
	int epoll_fd;
	int client_cnt;
	int client_index;
	
	int service_cnt;
	enServerStatus status;
	stService* service_list[MAX_SERVICE_NUM];
	stServiceStatus service_status[MAX_SERVICE_NUM];
	int service_mailbox_ids[MAX_SERVICE_NUM];
	pthread_t service_thread_ids[MAX_SERVICE_NUM];
	
	stList *pClient_list;
	pthread_mutex_t mutex_lock;
	pthread_cond_t run_cond;
	pthread_cond_t connect_cond;
	pthread_t thread_id;
}stServer;

struct clientProxy
{
	int client_id;
	int service_handle;
	int socket_fd; //the client socket fd of client side
	enClientStatus status;
	int sync_mailbox_id;  // for wait cb return
	pthread_t thread_id;
};

typedef struct
{
    int client_id;
    int socket_fd; // the client socket fd of server side
    int cb_async;
    char name[MAX_NAME_LEN];
}stClientInfo;

static stServer *s_server = NULL;

static void epoll_close_callback(void * arg)
{
	close(*(int*)arg);
	printf("epoll_close_fd() was called, ID = %d!\n", *(int*)arg);
	return;
}

static void server_set_handle(stServer *pServer)
{
	s_server = pServer;
}

static stServer* server_get_handle()
{
	return s_server;
}

static void server_set_status(enServerStatus status)
{
	stServer *pServer = server_get_handle();
	if(pServer){
		pServer->status = status;
	}
}

static void server_show_clients()
{
	printf("\n----------------------------------------------------- \n");
    stServer *pServer = server_get_handle();
	printf("Server have %d clients:\n", pServer->pClient_list->cnt);
    if(pServer) {
        stListNode *pNode = pServer->pClient_list->pHead->pNext;
        while(pNode)
        {
            stClientInfo *pClientInfo = (stClientInfo*)pNode->data;
            if(pClientInfo) {
                printf("[client ID: %d], Name: %s, socket_fd = %d, cb_async = %d\n", 
					pClientInfo->client_id, pClientInfo->name, pClientInfo->socket_fd, pClientInfo->cb_async);
            }
            else {
                printf("pClientInfo is NULL\n");
            }
            pNode = pNode->pNext;
        }
    }
    else {
        fprintf(stderr, "server has not create!\n");
    }
	printf("----------------------------------------------------- \n");
}

static enServerStatus server_get_status()
{
	stServer *pServer = server_get_handle();
	if(pServer){
		return pServer->status;
	}
}

static void server_accept_client(stServer *pServer)
{
	int new_fd = -1;
    if((new_fd = socket_local_accept(pServer->socket_fd)) != -1) {
    	epoll_ctl_event(pServer->epoll_fd, new_fd, EPOLLIN | EPOLLRDHUP, EPOLL_EVENT_ADD);
    }
}

static void server_add_client(int socket_fd, stMsg *pMsg)
{
	stServer *pServer = server_get_handle();
	if(!pServer) return;
	pthread_mutex_lock(&pServer->mutex_lock);
    stClientInfo *pClientInfo = (stClientInfo*)malloc(sizeof(stClientInfo));
    pClientInfo->client_id = pMsg->owner;
    pClientInfo->socket_fd = socket_fd;
	pClientInfo->cb_async = pMsg->cb_async;
    snprintf(pClientInfo->name, MAX_NAME_LEN, "%s", pMsg->msgText);
    printf("===pClientInfo: %p, client id: %d, name: %s\n", pClientInfo, pClientInfo->client_id, pClientInfo->name);
    list_insert_last(pServer->pClient_list, (void**)&pClientInfo);
	pthread_mutex_unlock(&pServer->mutex_lock);
	server_show_clients();
}

static void* server_find_client(int client_id)
{
	stServer *pServer = server_get_handle();
	if(!pServer) return NULL;
	stListNode *pCur = pServer->pClient_list->pHead->pNext;
	while(pCur) {
		stClientInfo *pClientInfo = (stClientInfo*)pCur->data;
		if(pClientInfo && pClientInfo->client_id == client_id){
			SERVER_LOG_NOTIFY("[server] find client: %d", client_id);
			return (void*)pClientInfo;
		}
		pCur = pCur->pNext;
	}
	return NULL;
}

static void* server_find_client_by_socketfd(int socket_fd)
{
	stServer *pServer = server_get_handle();
	if(!pServer) return NULL;
	stListNode *pCur = pServer->pClient_list->pHead->pNext;
	while(pCur) {
		stClientInfo *pClientInfo = (stClientInfo*)pCur->data;
		if(pClientInfo && pClientInfo->socket_fd == socket_fd){
			printf("[server] find client socket fd: %d\n", socket_fd);
			return (void*)pClientInfo;
		}
		pCur = pCur->pNext;
	}
	return NULL;
}


static void server_delete_client(int client_id)
{
	stServer *pServer = server_get_handle();
	if(!pServer) return;
	pthread_mutex_lock(&pServer->mutex_lock);
	stClientInfo*pClientInfo = (stClientInfo*)server_find_client(client_id);
	if(pClientInfo) {
		epoll_ctl_event(pServer->epoll_fd, pClientInfo->socket_fd, EPOLLIN | EPOLLRDHUP, EPOLL_EVENT_DEL);
		close(pClientInfo->socket_fd);
		list_delete_node(pServer->pClient_list, (void*)&pClientInfo);
	}
	pthread_mutex_unlock(&pServer->mutex_lock);
}

static void server_show_info()
{
	printf("----------------------------------------------------- \n");
    stServer *pServer = server_get_handle();
    if(pServer) {
		printf("server socket fd: %d\n", pServer->socket_fd);
		printf("server epoll fd: %d\n", pServer->epoll_fd);
		printf("client total cnt: %d\n", pServer->client_cnt);
		printf("client next id: %d\n", pServer->client_index);
		printf("service cnt: %d\n", pServer->service_cnt);
		printf("server status: %d\n", pServer->status);
		printf("server run thread id: 0x%x\n", pServer->thread_id);
    }
    else {
        fprintf(stderr, "server has not create!\n");
    }
	printf("----------------------------------------------------- \n");
}

static void server_send_callback(int socket_fd, stMsg *pMsg)
{
	write(socket_fd, pMsg, sizeof(stMsg));
}

static void server_recv_msg_test(int socket_fd, stMsg *pMsg)
{
    int a, b, c;
	stMsg msg = {0};
    switch(pMsg->msg_type)
    {
        case 0:
	        message_depacker(pMsg->msgText, sizeof(int), &a, sizeof(int), &b, -1);
	        printf("Owner: %d, msg: a = %d, b = %d\n", pMsg->owner, a, b);
			c = a + b;
			msg.msg_type = 0;
			msg.owner = pMsg->owner;
			message_packer(msg.msgText, sizeof(int), &c, -1);
			
	        break;
        case 1:
            message_depacker(pMsg->msgText, sizeof(int), &a, sizeof(int), &b, -1);
            printf("Owner: %d, msg: a = %d, b = %d\n", pMsg->owner, a, b);
			c = a + b;
			msg.msg_type = 1;
			msg.owner = pMsg->owner;
			message_packer(msg.msgText, sizeof(int), &c, -1);
            break;
        default:
            printf("Owner: %d, msg: %s\n", pMsg->owner, pMsg->msgText);
			msg.msg_type = 2;
			msg.owner = pMsg->owner;
			sprintf(msg.msgText, "%s %d\n", "server say: hello client", pMsg->owner);
            break;
    }

	if(pMsg->cb) {
		server_send_callback(socket_fd, &msg);
	}
}

int server_get_service(const char* service_name)
{
	stServer *pServer = server_get_handle();
	if(pServer && service_name) {
		pthread_mutex_lock(&pServer->mutex_lock);
		// iterate all list
		int handle = 0; 
		for(; handle < MAX_SERVICE_NUM; handle++) {
			if(pServer->service_list[handle] && strcmp(pServer->service_list[handle]->service_name, service_name) == 0) {
				pthread_mutex_unlock(&pServer->mutex_lock);
				SERVER_LOG_NOTIFY("Service: %s get success, handle = %d", service_name, handle);
				return handle;
			}
		}
		pthread_mutex_unlock(&pServer->mutex_lock);
	}
	return -1;
}

static void* server_service_dispose_msg(void* data)
{
	stServer *pServer = server_get_handle();
	stService *pService = (stService*)data;
	int service_handle = server_get_service(pService->service_name);
	if(service_handle < 0) {
		fprintf(stderr, "%s service handle is invalid\n", pService->service_name);
		return NULL;
	}
	while(pServer->service_status[service_handle] != SERVICE_FREE) {
		pthread_testcancel(); // this is a cancle point for pthread_cancle()
		stMsg *pMsg = NULL;
		if(mailbox_recv_msg(pServer->service_mailbox_ids[service_handle], (void**)&pMsg, -1) != -1){
			if(pMsg) {
				message_log_filter(pMsg, "server_service_dispose_msg");
				if(pMsg->service_handle != service_handle) {
					printf("NNNNNNNNNNNNNNNN service: %d, msg service: %d\n",
						service_handle, pMsg->service_handle);
				}
				else {
					pService->execute_command((void*)pMsg);
				}
				mailbox_free_msgbuff((void*)pMsg);
			}
			else {
				fprintf(stderr, "Server server_service_dispose_msg(): pMsg is NULL\n");
			}
		}
	}
	return NULL;
}

static void server_dispatch_msg(stServer *pServer, stMsg* msg)
{
	message_log_filter(msg, "server_dispatch_msg");
	stMsg *pMsg = NULL;
	if((msg->service_handle < 0) || (msg->service_handle >= MAX_SERVICE_NUM)) {
		SERVER_LOG_WARNING("The message from client: %d, but the service handle is invalid: %d", msg->owner, msg->service_handle);
		return;
	}
	if(mailbox_get_msgbuff((void**)&pMsg) != -1) {
		memcpy(pMsg, msg, sizeof(stMsg));
		mailbox_send_msg(pServer->service_mailbox_ids[pMsg->service_handle], (void*)pMsg, 1000);
	}
	else {
		SERVER_LOG_WARNING("mailbox_get_msgbuff Fail, so this message be discarded");
	}
}

static void* server_run(void *data)
{
	stServer* pServer = (stServer*)data;
	printf("[server] server wait running.....\n");
	pServer->epoll_fd = epoll_create(MAX_WAIT_CLIENT);
	epoll_ctl_event(pServer->epoll_fd, pServer->socket_fd, EPOLLIN, EPOLL_EVENT_ADD);
	struct epoll_event events[MAX_WAIT_CLIENT];
    while(1)
    {
		pthread_testcancel(); // this is a cancle point for pthread_cancle()
		pthread_mutex_lock(&pServer->mutex_lock);
		while(pServer->status != SERVER_RUN) {
			printf("[server] server stop.....\n");
			pthread_cond_wait(&pServer->run_cond, &pServer->mutex_lock);
			printf("[server] server start running.....\n");
		}
		pthread_mutex_unlock(&pServer->mutex_lock);

		memset(events, 0, sizeof(struct epoll_event) * MAX_WAIT_CLIENT);
		int eventNum = epoll_wait(pServer->epoll_fd, events, MAX_WAIT_CLIENT, -1);
		//printf("[server] epoll wake up, event num = %d!\n", eventNum);

		pthread_mutex_lock(&pServer->mutex_lock);
		int i = 0;
		for(; i < eventNum; i++){
			if(events[i].events & EPOLLIN) {
				if(events[i].data.fd == pServer->socket_fd){
					server_accept_client(pServer);
					pthread_cond_signal(&pServer->connect_cond);
				}
				else{	
					if(events[i].events & EPOLLRDHUP) {
						fprintf(stderr, "[server] server side socket id: %d hand up.\n", events[i].data.fd);
						stClientInfo *pClientInfo = (stClientInfo*)server_find_client_by_socketfd(events[i].data.fd);
						if(pClientInfo) {
							server_delete_client(pClientInfo->client_id);
							server_show_clients();
						}
						else {
							fprintf(stderr, "[server] can not find client by socketfd: %d\n", events[i].data.fd);
						}
						continue;
					}
					
					stMsg msg= {0};
					if(read(events[i].data.fd, &msg, sizeof(stMsg)) > 0){
						SERVER_LOG_WARNING("[server]rec: client socket id: %d, msgType: 0x%x", events[i].data.fd, msg.msg_type);

						message_log_filter(&msg, "server_run");
						/* get client information */
						if(msg.msg_type == CLIENT_CONNECT_MSG_TYPE) {
							server_add_client(events[i].data.fd, &msg);
							continue;
						}
						else if(msg.msg_type == CLIENT_DISCONN_MSG_TYPE) {
							server_delete_client(msg.owner);
							pthread_cond_signal(&pServer->connect_cond);
						}
						else {
							server_dispatch_msg(pServer, &msg);
						}
					}
					else {
						fprintf(stderr, "[server run]No data to read!\n");
					}
				}
			}
		}
		pthread_mutex_unlock(&pServer->mutex_lock);
    }
	
	printf("[server] server terminate.....\n");
	return NULL;
}


int server_create()
{
	stServer *pServer = server_get_handle();
	if(pServer) return 0; //only create once
	pServer = (stServer*)malloc(sizeof(stServer));
	if(!pServer) return -1;
	#if LOG_THREAD_ON
	log_task_init();
	#else
	log_network_init();
	#endif
	log_set_level(LOG_LEVEL_ALL);

	pthread_create_mutex(&pServer->mutex_lock);
	pthread_create_cond(&pServer->connect_cond);
	pthread_create_cond(&pServer->run_cond);

	pthread_mutex_lock(&pServer->mutex_lock);
	pServer->status = SERVER_INIT;

	if((pServer->socket_fd = socket_local_create(SERVER_NAME, MAX_WAIT_CLIENT)) < 0) {
		fprintf(stderr, "[server] create local socket fail!\n");
		goto FAIL;
	}
	
	pServer->epoll_fd = -1;
	pServer->client_cnt = 0;
	pServer->client_index = 0;
	
	/* creat clients list */
	pServer->pClient_list = list_create();
	/* create server list */
	memset(pServer->service_list, NULL, sizeof(pServer->service_list));
	memset(pServer->service_status, SERVICE_FREE, sizeof(pServer->service_status));
	int i = 0;
	for(; i < MAX_SERVICE_NUM; i++) {
		pServer->service_thread_ids[i] = -1;
	}
	pServer->service_cnt = 0;
	
	pServer->status = SERVER_READY;

	/* create mailbox */
	if(mailbox_all_init() < 0) {
		fprintf(stderr,"mailbox_all_init() fail\n");
		close(pServer->socket_fd);
        goto FAIL; 
	}

	pthread_create_thread(&pServer->thread_id, "server_run", &server_run, (void*)pServer);
	printf("thread id: 0x%x\n", pServer->thread_id);

	server_set_handle(pServer);
    printf("Server create success.....\n");
    pthread_mutex_unlock(&pServer->mutex_lock);
	return 0;

FAIL:
	pthread_mutex_unlock(&pServer->mutex_lock);
	free(pServer);
	return -1;
}

int server_add_service(stService *pService)
{
	stServer *pServer = server_get_handle();
	if(pServer) {
		pthread_mutex_lock(&pServer->mutex_lock);
		// iterate all list
		int i = 0; 
		for(; i < MAX_SERVICE_NUM; i++) {
			if(pServer->service_status[i] == SERVICE_FREE) {
				if(mailbox_create(&pServer->service_mailbox_ids[i], MAX_MAILBOX_SIZE) < 0) {
					fprintf(stderr,"service %s mailbox_create() fail\n", pService->service_name);
			        break; 
				}
				pServer->service_status[i] = SERVICE_BUSY;
				pthread_create_thread(&pServer->service_thread_ids[i], pService->service_name, &server_service_dispose_msg, (void*)pService);
				pServer->service_list[i] = pService;
				pServer->service_cnt++;
				pthread_mutex_unlock(&pServer->mutex_lock);
				printf("Service: %s add success\n", pService->service_name);
				return 0;
			}
		}
		pthread_mutex_unlock(&pServer->mutex_lock);
	}
	SERVER_LOG_ERR("Try add service: %s to server fail", pService->service_name);
	return -1;
}

void server_delete_service(const char* service_name)
{
	stServer *pServer = server_get_handle();
	if(pServer) {
		pthread_mutex_lock(&pServer->mutex_lock);
		// iterate all list
		int i = 0; 
		for(; i < MAX_SERVICE_NUM; i++) {
			if(strcmp(pServer->service_list[i]->service_name, service_name) == 0) {
				pServer->service_status[i] = SERVICE_FREE;
				if(pServer->service_thread_ids[i] != -1)
					pthread_join(pServer->service_thread_ids[i], NULL);
				
				pServer->service_status[i] = SERVICE_FREE;
				pServer->service_list[i] = NULL;
				pServer->service_cnt--;
				mailbox_destory(pServer->service_mailbox_ids[i]);
				pthread_mutex_unlock(&pServer->mutex_lock);
				SERVER_LOG_NOTIFY("Service: %s delete success", service_name);
				return;
			}
		}
		pthread_mutex_unlock(&pServer->mutex_lock);
	}
}

void server_destory()
{
    stServer *pServer = server_get_handle();
	if(pServer){
		/* (1) terminate the server_run thread */
		server_stop();
	    pthread_mutex_lock(&pServer->mutex_lock);
		while(1) {
			printf("thread id: 0x%x\n", pServer->thread_id);
			if (0 == pthread_cancel(pServer->thread_id)){
				pthread_join(pServer->thread_id, NULL);
		        printf("server_run thread finish success\n");
				break;
		    } else {
		        printf("server_run thread finish fail\n");
				usleep(100000); // retry
		    }
		}

		/* (2) terminate the all service threads */
		pServer->status = SERVER_INIT;
		/* wait all service thread terminate */
		int i = 0;
		for(; i < MAX_SERVICE_NUM; i++) {
			if(pServer->service_status[i] == SERVICE_FREE) continue;
			server_delete_service(pServer->service_list[i]->service_name);
		}
		printf("all service thread finish success\n");
		
		/* (3) destory client info */
		stListNode *pCur = pServer->pClient_list->pHead->pNext;
		while(pCur) {
			stClientInfo *pClientInfo = (stClientInfo*)pCur->data;
			if(pClientInfo){
				epoll_ctl_event(pServer->epoll_fd, pClientInfo->socket_fd, EPOLLIN, EPOLL_EVENT_DEL);
				close(pClientInfo->socket_fd);
			}
			pCur = pCur->pNext;
		}
		
		list_destory(pServer->pClient_list);
		printf("client info destory success\n");

		/* (4) release server resource */
		if(pServer->epoll_fd != -1) close(pServer->epoll_fd);
		close(pServer->socket_fd);
		pthread_cond_destroy(&pServer->connect_cond);
		pthread_cond_destroy(&pServer->run_cond);
		pthread_mutex_unlock(&pServer->mutex_lock);
		free(pServer);
		server_set_handle(NULL);
		mailbox_all_destory();
		printf("mailbox destory success\n");
		#if LOG_THREAD_ON
		log_task_fin();
		#else
		log_network_fin();
		printf("log destory success\n");
		#endif
	}

	return;
}

void server_start()
{
	stServer *pServer = server_get_handle();
	if(!pServer || pServer->status != SERVER_READY) {
		return;
	}
	pthread_mutex_lock(&pServer->mutex_lock);
	pServer->status = SERVER_RUN;
	pthread_mutex_unlock(&pServer->mutex_lock);
	pthread_cond_signal(&pServer->run_cond);
	server_show_info();
}

void server_stop()
{
	stServer *pServer = server_get_handle();
	if(!pServer) return;
	pthread_mutex_lock(&pServer->mutex_lock);
	pServer->status = SERVER_READY;
	pthread_mutex_unlock(&pServer->mutex_lock);
	server_show_info();
}

void server_service_transact_msg(void *msg)
{
	stMsg *pMsg = (stMsg*)msg;
	message_log_filter(pMsg, "server_service_transact_msg");
	stClientInfo *pClientInfo = (stClientInfo*)server_find_client(pMsg->owner);
	//printf("=== client: %d, cb_async = %d\n", pClientInfo->client_id, pClientInfo->cb_async);
	/* only when client connect specify it need async call back then do the following  */
	if(pClientInfo && pClientInfo->cb_async) {
		server_send_callback(pClientInfo->socket_fd, pMsg);
	}
}

static void client_recv_msg_test(stClientProxy* pClientProxy, stMsg *pMsg)
{
	if(pClientProxy->status == CLIENT_DISCONN) return;
    int c;
    switch(pMsg->msg_type)
    {
        case 0:
            message_depacker(pMsg->msgText, sizeof(int), &c, -1);
			printf("[client %d] result : %d\n", pClientProxy->client_id, c);
            break;
        case 1:
            message_depacker(pMsg->msgText, sizeof(int), &c, -1);
			printf("[client %d] result : %d\n", pClientProxy->client_id, c);
            break;
        default:
            printf("[client %d] recv: %s\n", pClientProxy->client_id , pMsg->msgText);
            break;
    }
}

static void* execute_client_cb(void *data)
{
	stClientProxy *pClientProxy = (stClientProxy*)data;
	int epoll_fd = epoll_create(1);
	epoll_ctl_event(epoll_fd, pClientProxy->socket_fd, EPOLLIN | EPOLLRDHUP, EPOLL_EVENT_ADD);

	stServer *pServer = server_get_handle();
	stService *pService = pServer->service_list[pClientProxy->service_handle];
	if(!pService) {
		printf("execute_client_cb: can not find service!!!!\n");
		return NULL;
	}

	/* if thread was terminated abnormal, should close epoll fd */
	pthread_cleanup_push(epoll_close_callback, &epoll_fd);
	
	struct epoll_event events[1];
    while(pClientProxy->status != CLIENT_DISCONN)
    {
		memset(events, 0, sizeof(struct epoll_event) * 1);
		int eventNum = epoll_wait(epoll_fd, events, 1, -1);
		int i = 0;
		for(; i < eventNum; i++){
			if(events[i].events & EPOLLIN) {
				if(events[i].events & EPOLLRDHUP) {
					fprintf(stderr, "[client run] client side socket id: %d hand up.\n", events[i].data.fd);
					epoll_ctl_event(epoll_fd, events[i].data.fd, EPOLLIN | EPOLLRDHUP, EPOLL_EVENT_DEL);
					close(events[i].data.fd);
					continue;
				}
				
				stMsg msg= {0};
				if(read(events[i].data.fd, &msg, sizeof(stMsg)) > 0){
					if(msg.service_handle == pClientProxy->service_handle) {
						pService = pServer->service_list[pClientProxy->service_handle];
						if(pService) {
							pService->async_execute_cb(&msg);
						}
					}
				}
				else {
					fprintf(stderr, "[client run]No data to read!\n");
				}
			}
		}
    }
	
	printf("[client %d] client terminate.....\n", pClientProxy->client_id);
	close(epoll_fd);
	printf("Epoll was closed!\n");
	pthread_cleanup_pop(0);
	return NULL;
}

stClientProxy* client_connect(const char* client_name, const char* service_name, int execute_cb_async)
{
	stServer *pServer = server_get_handle();
	if(!pServer || pServer->status != SERVER_RUN) {
		fprintf(stderr, "connect to server fail! line: %d\n", __LINE__);
		return NULL;
	}

	if(pServer->client_cnt >= MAX_CONNECT_CLIENT) {
		SERVER_LOG_ERR("Sorry server has over load!, client num = %d", pServer->client_cnt);
		return NULL;
	}
       
	stClientProxy *pClientProxy = (stClientProxy*)malloc(sizeof(stClientProxy));
	if(!pClientProxy) return NULL;
	pClientProxy->status = CLIENT_DISCONN;
	
	pthread_mutex_lock(&pServer->mutex_lock);

	if(mailbox_create(&pClientProxy->sync_mailbox_id, 16) < 0) {
		fprintf(stderr,"[client] mailbox_create() fail\n");
        goto FAIL; 
	}

	if((pClientProxy->socket_fd = socket_local_connect(SERVER_NAME)) < 0) {
		fprintf(stderr, "[client] connect to local socket fail\n");
		goto FAIL;
	}

	pClientProxy->client_id = pServer->client_index++;
	pClientProxy->service_handle = server_get_service(service_name);
	if(pClientProxy->service_handle < 0) {
		fprintf(stderr, "[client] connect to service: %s fail, service is not exist\n", service_name);
		goto FAIL;
	}
	pServer->client_cnt++;

	/* wait server accept finish */
	struct timespec tv;
	int timeOutMs = 1000;
	clock_gettime(CLOCK_MONOTONIC, &tv);
	tv.tv_nsec += (timeOutMs%1000)*1000*1000;
	tv.tv_sec += tv.tv_nsec/(1000*1000*1000) + timeOutMs/1000;
	tv.tv_nsec = tv.tv_nsec%(1000*1000*1000);
	pthread_cond_timedwait(&pServer->connect_cond, &pServer->mutex_lock, &tv);

	pClientProxy->status = CLIENT_CONNECT;
	/* create client thread */
	pClientProxy->thread_id = -1;
	if(execute_cb_async) {
		pthread_create_thread(&pClientProxy->thread_id, client_name, &execute_client_cb, (void*)pClientProxy);
	}
	
	/* tell server the info about client */
	stMsg msg;
	msg.owner = pClientProxy->client_id;
	msg.cb_async = execute_cb_async;
	msg.msg_type = CLIENT_CONNECT_MSG_TYPE;
	if(client_name) {
		snprintf(msg.msgText, MAX_NAME_LEN, "%s", client_name);
	}
	else {
		snprintf(msg.msgText, MAX_NAME_LEN, "client%d", pClientProxy->client_id);
	}
	write(pClientProxy->socket_fd, &msg, sizeof(stMsg));
       
	pthread_mutex_unlock(&pServer->mutex_lock);
	printf("++++ Client: %s connect to server socket fd = %d, client side socket fd = %d\n", client_name, pServer->socket_fd, pClientProxy->socket_fd);
	
	return pClientProxy;

FAIL:
	free(pClientProxy);
	pthread_mutex_unlock(&pServer->mutex_lock);
	return NULL;
}


void client_rebind_service(stClientProxy *pClientProxy, const char* service_name)
{
	if(!pClientProxy || !service_name) return;
	stServer *pServer = server_get_handle();
	if(!pServer || pServer->status != SERVER_RUN || pClientProxy->status == CLIENT_DISCONN) {
		fprintf(stderr, "[client] rebind to service fail! line: %d\n", __LINE__);
		return;
	}

	// get rebind service handle
	int service_handle = server_get_service(service_name);
	if(service_handle < 0) {
		printf("EEEEEEEEEEEE service: %s can not find!\n", service_name);
		return;
	}

	// if current service handle is not the same to current service handle
	if((pClientProxy->service_handle != -1) && (pClientProxy->service_handle != service_handle)){
		pthread_mutex_lock(&pServer->mutex_lock);
		pClientProxy->status = CLIENT_REBIND_SERVICE;
		stMsg msg;
		msg.msg_type = SYNC_MSG;
		if(pClientProxy->thread_id == -1) {
			msg.cb_async = 0;
		}
		else {
			msg.cb_async = 1;
		}
		msg.sync_wait_id = pClientProxy->sync_mailbox_id;
		msg.service_handle = pClientProxy->service_handle;
		msg.owner = pClientProxy->client_id;
		write(pClientProxy->socket_fd, &msg, sizeof(stMsg));
		//wait until service all task completed
		printf("WWWWWWWWWWWWWWWWWW current service: %d, rebind service: %d, cb_async = %d, mailbox_id = %d\n", 
			pClientProxy->service_handle, service_handle, msg.cb_async, msg.sync_wait_id);
		pthread_mutex_unlock(&pServer->mutex_lock);

		message_log_filter(&msg, "client_rebind_service");
		message_sync_wait(pClientProxy->sync_mailbox_id, 1000); // timeout is 1000 ms

		pthread_mutex_lock(&pServer->mutex_lock);
		pClientProxy->service_handle = service_handle;
		pClientProxy->status = CLIENT_CONNECT;
		pthread_mutex_unlock(&pServer->mutex_lock);

		printf("RRRRRRRRRRRRRRRRRR client: %d, rebind service: %s\n", pClientProxy->client_id, service_name);
	}
	else {
		printf("EEEEEEEEEEEEEEEEEE current service: %d, rebind service: %d\n", pClientProxy->service_handle, service_handle);
	}
	
	return;
}

void client_disconnect(stClientProxy *pClientProxy)
{
	stServer *pServer = server_get_handle();
	if(!pServer || !pClientProxy) return;
	pthread_mutex_lock(&pServer->mutex_lock);
	pClientProxy->status = CLIENT_DISCONN;
	printf("[client %d]thread id: 0x%x wait disconnect........\n", pClientProxy->client_id, pClientProxy->thread_id);
	if(pClientProxy->thread_id != -1)
		pthread_join(pClientProxy->thread_id, NULL);
    printf("client %d thread finish success\n", pClientProxy->client_id);
	/* tell server the client will disconnect */
	stMsg msg;
	msg.owner = pClientProxy->client_id;
	msg.msg_type = CLIENT_DISCONN_MSG_TYPE;
	write(pClientProxy->socket_fd, &msg, sizeof(stMsg));

	printf("[client %d] wait server disconnect finished \n", pClientProxy->client_id);
	/* wait server disconnect finish */
	struct timespec tv;
	int timeOutMs = 1000;
	clock_gettime(CLOCK_MONOTONIC, &tv);
	tv.tv_nsec += (timeOutMs%1000)*1000*1000;
	tv.tv_sec += tv.tv_nsec/(1000*1000*1000) + timeOutMs/1000;
	tv.tv_nsec = tv.tv_nsec%(1000*1000*1000);
	pthread_cond_timedwait(&pServer->connect_cond, &pServer->mutex_lock, &tv);
	close(pClientProxy->socket_fd);
	mailbox_destory(pClientProxy->sync_mailbox_id);
	printf("[client %d] disconnect finished\n", pClientProxy->client_id);
	pServer->client_cnt--;
	free(pClientProxy);
	pClientProxy = NULL;
	pthread_mutex_unlock(&pServer->mutex_lock);
	
	server_show_clients();
}

void client_send_msg_test(stClientProxy* pClientProxy, int msg_type, int need_callback)
{
	if(pClientProxy->status == CLIENT_DISCONN) return;
    stMsg msg = {0};
    msg.msg_type = msg_type;
    msg.owner = pClientProxy->client_id;
    int a, b;
    switch(msg_type)
    {
        case 0:
            a = 10; b = 100;
            message_packer(msg.msgText, sizeof(int), &a, sizeof(int), &b, -1);
            break;
        case 1:
            a = 100; b = 100;
            message_packer(msg.msgText, sizeof(int), &a, sizeof(int), &b, -1);
            break;
        default:
            sprintf(msg.msgText, "%s", "Hello world!");
            break;
    }
    write(pClientProxy->socket_fd, &msg, sizeof(stMsg));
}
