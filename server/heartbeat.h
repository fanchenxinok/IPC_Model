#ifndef __S_HEART_BEAT_H__
#define __S_HEART_BEAT_H__

/* this cb is used for remove client node from client list when this client is time out */
typedef void (*pfOffLineCb)(int id, void *user);

struct hbServer;
typedef struct hbServer stHbServer;

// hb_timeout (ms)
stHbServer* heartbeat_server_create(
	int port_num,
	int hb_timeout,  //ms
	pfOffLineCb offline_cb);

void heartbeat_server_destory(stHbServer *pHbServer);

int heartbeat_client_create(
	const char* ip_addr, 
	int port_num,
	int hb_id,  // this is the same to user id
	int hb_period);

#endif
