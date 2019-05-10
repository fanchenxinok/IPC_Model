#include <stdio.h>
#include "arithmetic_service.h"
#include "arithmetic_service_interface.h"
#include "server.h"
#include "../server/message.h"

/* 测试的service: 提供算术运算的服务 */
static int control = 0;
static int cmd_cnt = 0;
static void execute_command(void *command);
static void async_execute_callback(void *msg);

static stService arithmetic_service = {
	ARITHMETIC_SERVICE_NAME,
	execute_command,
	async_execute_callback
};

static int fun_add(int a, int b)
{
	return a + b;
}

static int fun_max(int a, int b)
{
	return (a > b) ? a : b;
}

static int fun_control()
{
	control = (control == 0) ? 1 : 0;
	return control;
}

static void execute_command(void *command)
{
	int a, b, c;
	stMsg msg = {0};
	stMsg *pMsg = (stMsg*)command;
	memcpy(&msg, pMsg, sizeof(stMsg));
	//printf("dispose msgtype: %d", pMsg->msg_type);
	message_log_filter(pMsg, "arithmetic, execute_command");
    switch(pMsg->msg_type)
    {
        case AS_ADD_REQ:
	        message_depacker(pMsg->msgText, sizeof(int), &a, sizeof(int), &b, -1);
	        //printf("[arithmetic_service]Owner: %d, request ADD msg: a = %d, b = %d\n", pMsg->owner, a, b);
			c = fun_add(a, b);
			msg.msg_type = AS_ADD_RES;
			msg.owner = pMsg->owner;
			message_packer(msg.msgText, sizeof(int), &c, -1);

			if(pMsg->cb_async) {
				msg.cb = pMsg->cb;
				server_service_transact_msg(&msg);
			}
			else {
				if(pMsg->cb) ((call_back)(pMsg->cb))(&c, 0);
				if(pMsg->sync_wait_time != 0) {
					message_sync_unwait(pMsg->sync_wait_id);
				}
				cmd_cnt++;
			}
	        break;
        case AS_MAX_REQ:
            message_depacker(pMsg->msgText, sizeof(int), &a, sizeof(int), &b, -1);
            //printf("[arithmetic_service]Owner: %d, request MAX msg: a = %d, b = %d\n", pMsg->owner, a, b);
			c = fun_max(a, b);
			msg.msg_type = AS_MAX_RES;
			msg.owner = pMsg->owner;
			message_packer(msg.msgText, sizeof(int), &c, -1);

			if(pMsg->cb_async) {
				msg.cb = pMsg->cb;
				server_service_transact_msg(&msg);
			}
			else {
				if(pMsg->cb) ((call_back)(pMsg->cb))(&c, 0);
				if(pMsg->sync_wait_time != 0) {
					message_sync_unwait(pMsg->sync_wait_id);
				}
				cmd_cnt++;
			}
            break;
		case AS_CTL_REQ:
            //printf("[arithmetic_service]Owner: %d, request CTRL\n", pMsg->owner);
			c = fun_control();
			msg.msg_type = AS_CTL_RES;
			msg.owner = pMsg->owner;
			message_packer(msg.msgText, sizeof(int), &c, -1);

			if(pMsg->cb_async) {
				msg.cb = pMsg->cb;
				server_service_transact_msg(&msg);
			}
			else {
				if(pMsg->cb) ((call_back)(pMsg->cb))(&c, 0);
				if(pMsg->sync_wait_time != 0) {
					message_sync_unwait(pMsg->sync_wait_id);
				}
				cmd_cnt++;
			}
            break;
		case AS_INF_REQ:
			//printf("[arithmetic_service]Owner: %d, request INFO\n", pMsg->owner);
			msg.msg_type = AS_INF_RES;
			msg.owner = pMsg->owner;
			sprintf(msg.msgText, "AS Service name: %s", ARITHMETIC_SERVICE_NAME);

			if(pMsg->cb_async) {
				msg.cb = pMsg->cb;
				server_service_transact_msg(&msg);
			}
			else {
				if(pMsg->cb) ((call_back)(pMsg->cb))(msg.msgText, 0);
				if(pMsg->sync_wait_time != 0) {
					message_sync_unwait(pMsg->sync_wait_id);
				}
				cmd_cnt++;
			}
            break;
		case SYNC_MSG:
		{
			if(pMsg->cb_async) {
				server_service_transact_msg(&msg);
			}
			else {
				message_sync_unwait(pMsg->sync_wait_id);
			}			
		}break;
        default:
            //printf("[arithmetic_service]Owner: %d, msg: %s\n", pMsg->owner, pMsg->msgText);
			msg.msg_type = AS_SAY_RES;
			msg.owner = pMsg->owner;
			sprintf(msg.msgText, "%s %d!", "server say: I had read your message, dear client:", pMsg->owner);

			if(pMsg->cb_async) {
				msg.cb = pMsg->cb;
				server_service_transact_msg(&msg);
			}
			else {
				if(pMsg->cb) ((call_back)(pMsg->cb))(msg.msgText, 0);
				if(pMsg->sync_wait_time != 0) {
					message_sync_unwait(pMsg->sync_wait_id);
				}
				cmd_cnt++;
			}
            break;
    }

}

static void async_execute_callback(void *msg)
{
	cmd_cnt++;
	stMsg *pMsg = (stMsg*)msg;
	int c = 0;
	message_log_filter(pMsg, "arithmetic, async_execute_callback");
	switch(pMsg->msg_type)
	{
		case AS_ADD_RES:
			message_depacker(pMsg->msgText, sizeof(int), &c, -1);
			if(pMsg->cb) ((call_back)(pMsg->cb))(&c, 0);
			break;
		case AS_MAX_RES:
			message_depacker(pMsg->msgText, sizeof(int), &c, -1);
			if(pMsg->cb) ((call_back)(pMsg->cb))(&c, 0);
			break;
		case AS_CTL_RES:
			message_depacker(pMsg->msgText, sizeof(int), &c, -1);
			if(pMsg->cb) ((call_back)(pMsg->cb))(&c, 0);
			break;
		case AS_INF_RES:
			message_depacker(pMsg->msgText, sizeof(int), &c, -1);
			if(pMsg->cb) ((call_back)(pMsg->cb))(pMsg->msgText, 0);
			break;
		case SYNC_MSG:
			message_sync_unwait(pMsg->sync_wait_id);
			break;
		default:
			message_depacker(pMsg->msgText, sizeof(int), &c, -1);
			if(pMsg->cb) ((call_back)(pMsg->cb))(pMsg->msgText, 0);
			break;
	}

	//printf("async execute cb type: %d\n", pMsg->msg_type);
}


void run_arithmetic_service()
{
	server_add_service(&arithmetic_service);
}

void stop_arithmetic_service()
{
	server_delete_service(ARITHMETIC_SERVICE_NAME);
}

int get_arithmetic_command_cnt()
{
	return cmd_cnt;
}

