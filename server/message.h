#ifndef __S_MESSAGE_H__
#define __S_MESSAGE_H__

#include "mailbox.h"

/* 
* SYNC_MSG 用于当client需要切换绑定的service时，
* 调用client_rebind_service会发送这个消息以保证
* 以前的所有消息都处理完毕。
* xxxx_service.c 的excute_command()函数必须添加：
	case SYNC_MSG:
		{
			if(pMsg->cb_async) {
				memcpy(&msg, pMsg, sizeof(stMsg));
				server_service_transact_msg(&msg);
			}
			else {
				message_sync_unwait(pMsg->sync_wait_id);
			}			
		}break;

* async_execute_callback()函数必须添加：
	case SYNC_MSG:
			message_sync_unwait(pMsg->sync_wait_id);
			break;
*/
#define SYNC_MSG (0x1122aabb)

typedef void (*call_back)(void* user, int errno);

/*
*  如果cb_func要执行比较耗时的操作将影响服务器处理其他client的请求
*  因此需要将cb_async置为1, 使cb_func在client_connect的时候
*  创建的线程中执行，这样可以避免service被一个client占用而影响其他client.
*
*  如果cb_async被置为0则可以分为两种情况：
	（1） 如果wait_time 大于0 则client 需要等待callback执行完才能继续，且超时时间为wait_time 毫秒。
	（2） 如果wait_time 等于0， 则client向服务器发送完命令后，不需要等待可直接执行
*/
typedef struct
{
	call_back cb_func;
	int cb_async;
	int wait_time;  // // -1: wait forever, 0: not wait, > 0: wait ms
}stCbInfo;


typedef struct
{
	int owner;  // this message is belong to who
	int service_handle; // this message is belong to which service to process
	int msg_type; // message type
	char msgText[256]; // message content
	void* cb;
	int cb_async;
	int sync_wait_id;
	int sync_wait_time;  // -1: wait forever, 0: not wait, > 0: wait ms
}stMsg;

/**
* @brief function: message packer
* @param[in] msgData:  message data
* @return void.
*/
void message_packer(char* msgData, ...);

/**
* @brief function: message depacker
* @param[out] msgData:  message data
* @return void.
*/
void message_depacker(char* msgData, ...);

/*example*/
/*
(1) pack message
char msgData[128];
int a = 10, b = 100;
message_packer(msgData,
		 	sizeof(int), &a,
		 	sizeof(int), &b, -1);
(2) unpack message
int a, b;
message_depacker(msgData,
		 	sizeof(int), &a,
		 	sizeof(int), &b, -1);
cout << a << b << endl;
*/

/* send message from client to server if success return msg size else return -1 */
int message_dispach(void* pClientProxy, stMsg *pMsg);

void message_sync_unwait(int sync_mailbox_id);

void message_log_filter(stMsg *pMsg, const char* flag);


#endif
