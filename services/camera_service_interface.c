#include <stdio.h>
#include <pthread.h>
#include <unistd.h>
#include "camera_service_interface.h"

static int discard_msg_cnt = 0;

void cs_ctrl_req(void* pClientProxy, int on, stCbInfo *pCbInfo)
{
    stMsg msg = {0};
    msg.msg_type = (on == 0) ? CS_OFF_REQ : CS_ON_REQ;
    if(message_dispatch(pClientProxy, &msg, pCbInfo) < 0) {
		//printf("====================\n");
		discard_msg_cnt++;
		return;
    }
}

int cs_get_discard_msg_cnt()
{
	return discard_msg_cnt;
}
