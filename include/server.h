#ifndef __SERVER_H__
#define __SERVER_H__

#define MAX_NAME_LEN  	(16)

struct clientProxy;
typedef struct clientProxy stClientProxy;

typedef struct
{
	const char* service_name;
	void (*execute_command)(void* command);
	void (*async_execute_cb)(void* msg);
}stService;

int server_create();
void server_destory();
void server_start();
void server_stop();
/* if success return 0 else -1 */
int server_add_service(stService *pService);
void server_delete_service(const char* service_name);

/*
*  client_name: client name
*  service_name: specify which service the client want to bind 
*  execute_cb_async: if client callback function is too take time, it will occupy too much time of service to execute cb
	so it is need to create a thread to execute the client callback.
*/
stClientProxy* client_connect(const char* client_name, const char* service_name, int execute_cb_async);

/*
*	client rebind other service
*/
void client_rebind_service(stClientProxy *pClientProxy, const char* service_name);

void client_disconnect(stClientProxy *pClientProxy);
/* if service dispose the request then need execute callback async, 
	so use this interface to send async callback execute command to client,
	the client thread do callback.
*/
void server_service_transact_msg(void *msg);

#endif
