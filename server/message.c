#include <string.h>
#include <stdarg.h>
#include "message.h"

typedef enum
{
	CLIENT_CONNECT = 0,
	CLIENT_DISCONN,
	CLIENT_REBIND_SERVICE
}enClientStatus;
	
typedef struct
{
	int client_id;
	int service_handle;
	int socket_fd; //the client socket fd of client side
	enClientStatus status;
	int sync_mailbox_id;  // for wait cb return
}stCommand;

/**
* @brief function: message packer
* @param[in] msgData:  message data
* @return void.
*/
void message_packer(char* msgData, ...)
{
	if(msgData == NULL) return;
	va_list vaList;
	void* para;
	char* funParam = msgData;
	va_start(vaList, msgData);
	int len = va_arg(vaList, int);
	while((len != 0) && (len != -1)){
		para = va_arg(vaList, void*);
		memcpy(funParam, para, len);
		funParam += len;
		len = va_arg(vaList, int);
	}
	va_end(vaList);
	return;
}

/**
* @brief function: message depacker
* @param[out] msgData: message
* @return void.
*/
void message_depacker(char* msgData, ...)
{
	if(msgData == NULL) return;
	va_list vaList;
	void* para;
	char* funParam = msgData;
	va_start(vaList, msgData);
	int len = va_arg(vaList, int);
	while((len != 0) && (len != -1)){
		para = va_arg(vaList, void*);
		memcpy(para, funParam, len);
		funParam += len;
		len = va_arg(vaList, int);
	}
	va_end(vaList);
	return;
}

void message_sync_wait(int sync_mailbox_id, int wait_time)
{
	stMsg *pMsg = NULL;
	if(mailbox_recv_msg(sync_mailbox_id, (void**)&pMsg, wait_time) != -1){
		//printf("++++++++++++message_sync_wait()  wait cb return: %d!!\n", sync_mailbox_id);
		mailbox_free_msgbuff((void*)pMsg);
	}
	else {
		printf("message_sync_wait()  wait timeout.......\n");
	}
}

void message_sync_unwait(int sync_mailbox_id)
{
	//printf("##############message_sync_unwait()  wait cb return: %d!!\n", sync_mailbox_id);
	stMsg *pMsg = NULL;
	if(mailbox_get_msgbuff((void**)&pMsg) != -1) {
		mailbox_send_msg(sync_mailbox_id, (void*)pMsg, 1000);
	}
	else {
		printf("message_sync_unwait: mailbox_get_msgbuff Fail, so this message be discarded\n");
	}
}

int message_dispatch(void* pClientProxy, stMsg *pMsg, stCbInfo *pCbInfo)
{
	if(!pClientProxy || !pMsg) return -1;
	stCommand *pCommand = (stCommand*)pClientProxy; 
	if((pCommand->status == CLIENT_DISCONN) && (pCommand->status == CLIENT_REBIND_SERVICE)) {
		printf("MMMMMMMMMMMMMMMMM: client status: %d\n", pCommand->status);
		return -1;
	}
	
	if(pCbInfo) {
		pMsg->cb = pCbInfo->cb_func;
		pMsg->cb_async = pCbInfo->cb_async;
		pMsg->sync_wait_time = pCbInfo->wait_time;
	}
	else {
		pMsg->cb = NULL;
		pMsg->cb_async = 0;
		pMsg->sync_wait_time = 0;
	}
	
    pMsg->owner = pCommand->client_id;
	pMsg->service_handle = pCommand->service_handle;
	if(pMsg->cb_async) {
    	return write(pCommand->socket_fd, pMsg, sizeof(stMsg));
	}
	else {
		pMsg->sync_wait_id = pCommand->sync_mailbox_id;
		write(pCommand->socket_fd, pMsg, sizeof(stMsg));
		if(pMsg->sync_wait_time != 0) {
			// wait until callback function return
			message_sync_wait(pCommand->sync_mailbox_id, pMsg->sync_wait_time);
		}
		return 0;
	}
}

void message_log_filter(stMsg *pMsg, const char* flag)
{
	#if 0
	if(pMsg->msg_type == SYNC_MSG) {
		printf("[%s] SSSSSSSSSSSSSS Type: 0x%x, owner = %d, service_handle = %d, cb_async = %d, sync_wait_id = %d\n",
			flag, pMsg->msg_type, pMsg->owner, pMsg->service_handle, pMsg->cb_async, pMsg->sync_wait_id);
	}
	#endif
}