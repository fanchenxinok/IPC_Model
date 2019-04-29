#include <stdio.h>
#include <pthread.h>
#include <unistd.h>
#include "arithmetic_service_interface.h"

static int discard_msg_cnt = 0;

void as_add_req(void* pClientProxy, int a, int b, stCbInfo *pCbInfo)
{
    stMsg msg = {0};
    msg.msg_type = AS_ADD_REQ;
	msg.cb = pCbInfo->cb_func;
	msg.cb_async = pCbInfo->cb_async;
	msg.sync_wait_time = pCbInfo->wait_time;
    message_packer(msg.msgText, sizeof(int), &a, sizeof(int), &b, -1);
    if(message_dispatch(pClientProxy, &msg) < 0) {
		//printf("====================\n");
		discard_msg_cnt++;
		return;
    }
}

void as_max_req(void* pClientProxy, int a, int b, stCbInfo *pCbInfo)
{
    stMsg msg = {0};
    msg.msg_type = AS_MAX_REQ;
	msg.cb = pCbInfo->cb_func;
	msg.cb_async = pCbInfo->cb_async;
	msg.sync_wait_time = pCbInfo->wait_time;
    message_packer(msg.msgText, sizeof(int), &a, sizeof(int), &b, -1);
    if(message_dispatch(pClientProxy, &msg) < 0) {
		//printf("====================\n");
		discard_msg_cnt++;
		return;
    }
}

void as_ctrl_req(void* pClientProxy, stCbInfo *pCbInfo)
{
	stMsg msg = {0};
	msg.msg_type = AS_CTL_REQ;
	msg.cb = pCbInfo->cb_func;
	msg.cb_async = pCbInfo->cb_async;
	msg.sync_wait_time = pCbInfo->wait_time;
	if(message_dispatch(pClientProxy, &msg) < 0) {
		//printf("====================\n");
		discard_msg_cnt++;
		return;
    }
}

void as_say_req(void* pClientProxy, const char* words, stCbInfo *pCbInfo)
{
	stMsg msg = {0};
	msg.msg_type = AS_SAY_REQ;
	msg.cb = pCbInfo->cb_func;
	msg.cb_async = pCbInfo->cb_async;
	msg.sync_wait_time = pCbInfo->wait_time;
	sprintf(msg.msgText, "%s", words);
	if(message_dispatch(pClientProxy, &msg) < 0) {
		//printf("====================\n");
		discard_msg_cnt++;
		return;
    }
}

void as_inf_req(void* pClientProxy, stCbInfo *pCbInfo)
{
	stMsg msg = {0};
	msg.msg_type = AS_INF_REQ;
	msg.cb = pCbInfo->cb_func;
	msg.cb_async = pCbInfo->cb_async;
	msg.sync_wait_time = pCbInfo->wait_time;
	if(message_dispatch(pClientProxy, &msg) < 0) {
		//printf("====================\n");
		discard_msg_cnt++;
		return;
    }
}

int as_get_discard_msg_cnt()
{
	return discard_msg_cnt;
}
