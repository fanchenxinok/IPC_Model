#ifndef __S_NETWORK_H__
#define __S_NETWORK_H__

void network_show_ip_addrs();
int network_create();
void network_destory();
void network_start();
void network_stop();
void network_send_log(const char* log);

#endif
