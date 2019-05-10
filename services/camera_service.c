#include <stdio.h>
#include "camera_service.h"
#include "camera_service_interface.h"
#include "server.h"
#include "../server/message.h"

/* 测试的service: 提供摄像服务 */
static int cmd_cnt = 0;
static int open_status = 0;
static void execute_command(void *command);
static void async_execute_callback(void *msg);

static stService camera_service = {
	CAMERA_SERVICE_NAME,
	execute_command,
	async_execute_callback
};

static void execute_command(void *command)
{
	int a, b, c;
	stMsg msg = {0};
	stMsg *pMsg = (stMsg*)command;
	memcpy(&msg, pMsg, sizeof(stMsg));
	message_log_filter(pMsg, "camera, execute_command");
	//printf("dispose msgtype: %d", pMsg->msg_type);
    switch(pMsg->msg_type)
    {
		case CS_ON_REQ:
		{
			//printf("[camera_service]Owner: %d, request open camera\n", pMsg->owner);
			msg.msg_type = CS_ON_RES;
			msg.owner = pMsg->owner;
			if(open_status) {
				sprintf(msg.msgText, "[%s]: camera already opened!!!\n", CAMERA_SERVICE_NAME);
			}
			else {
				sprintf(msg.msgText, "[%s]: ready to open camera!!!\n", CAMERA_SERVICE_NAME);
			}

			if(pMsg->cb_async) {
				msg.cb = pMsg->cb;
				server_service_transact_msg(&msg);
			}
			else {
				if(pMsg->cb) ((call_back)(pMsg->cb))(msg.msgText, 0);
				if(pMsg->sync_wait_time != 0) {
					message_sync_unwait(pMsg->sync_wait_id);
				}
			}

			if(open_status) break;
			open_status = 1;

			sprintf(msg.msgText, "[%s]: camera opened success!!!\n", CAMERA_SERVICE_NAME);

			if(pMsg->cb_async) {
				msg.cb = pMsg->cb;
				server_service_transact_msg(&msg);
			}
			else {
				if(pMsg->cb) ((call_back)(pMsg->cb))(msg.msgText, 0);
				if(pMsg->sync_wait_time != 0) {
					message_sync_unwait(pMsg->sync_wait_id);
				}
			}
			
        }break;
        case CS_OFF_REQ:
		{
			//printf("[camera_service]Owner: %d, request close camera\n", pMsg->owner);
			msg.msg_type = CS_OFF_RES;
			msg.owner = pMsg->owner;

			open_status = 0;

			sprintf(msg.msgText, "[%s]: camera already closed!!!\n", CAMERA_SERVICE_NAME);

			if(pMsg->cb_async) {
				msg.cb = pMsg->cb;
				server_service_transact_msg(&msg);
			}
			else {
				if(pMsg->cb) ((call_back)(pMsg->cb))(msg.msgText, 0);
				if(pMsg->sync_wait_time != 0) {
					message_sync_unwait(pMsg->sync_wait_id);
				}
				
			}			
        }break;
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
            break;
    }

	cmd_cnt++;
}

static void async_execute_callback(void *msg)
{
	cmd_cnt++;
	stMsg *pMsg = (stMsg*)msg;
	int c = 0;
	message_log_filter(pMsg, "camera, async_execute_callback");
	switch(pMsg->msg_type)
	{
		case CS_ON_RES:
		case CS_OFF_RES:
			if(pMsg->cb) ((call_back)(pMsg->cb))(pMsg->msgText, 0);
			break;
		case SYNC_MSG:
			message_sync_unwait(pMsg->sync_wait_id);
			break;
		default:
			break;
	}

	//printf("async execute cb type: %d\n", pMsg->msg_type);
}


void run_camera_service()
{
	server_add_service(&camera_service);
}

void stop_camera_service()
{
	server_delete_service(CAMERA_SERVICE_NAME);
}

int get_camera_command_cnt()
{
	return cmd_cnt;
}

