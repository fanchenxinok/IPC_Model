#ifndef __S_CAMERA_SERVICE_INTERFACE_H__
#define __S_CAMERA_SERVICE_INTERFACE_H__

#include "../server/message.h"

#define CAMERA_SERVICE_NAME  "camera"

typedef enum
{
	CS_ON_REQ = 0,
	CS_OFF_REQ
}enCameraServiceReq;  //request command

typedef enum
{
	CS_ON_RES = 0,
	CS_OFF_RES
}enCameraServiceRes;  // command respond

/* 给client 端使用的API */
void cs_ctrl_req(void* pClientProxy, int on, stCbInfo *pCbInfo);
int cs_get_discard_msg_cnt();


#endif
