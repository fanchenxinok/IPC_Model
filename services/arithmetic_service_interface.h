#ifndef __S_ARITHMETIC_SERVICE_INTERFACE_H__
#define __S_ARITHMETIC_SERVICE_INTERFACE_H__

#include "../server/message.h"

#define ARITHMETIC_SERVICE_NAME  "arithmetic"

typedef enum
{
	AS_ADD_REQ = 0,
	AS_MAX_REQ,
	AS_CTL_REQ,
	AS_SAY_REQ,
	AS_INF_REQ
}enArithmeticServiceReq;  //request command

typedef enum
{
	AS_ADD_RES = 0,
	AS_MAX_RES,
	AS_CTL_RES,
	AS_SAY_RES,
	AS_INF_RES
}enArithmeticServiceRes;  // command respond

/* 给client 端使用的API */
void as_add_req(void* pClientProxy, int a, int b, stCbInfo *pCbInfo);
void as_max_req(void* pClientProxy, int a, int b, stCbInfo *pCbInfo);
void as_ctrl_req(void* pClientProxy, stCbInfo *pCbInfo);
void as_say_req(void* pClientProxy, const char* words, stCbInfo *pCbInfo);
void as_inf_req(void* pClientProxy, stCbInfo *pCbInfo);
int as_get_discard_msg_cnt();


#endif
