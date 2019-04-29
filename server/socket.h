#ifndef __S_SOCKET_H__
#define __S_SOCKET_H__

void socket_show_ip_addrs();
int socket_local_create(const char* server_name, int listen_num);
int socket_local_connect(const char* server_name);
int socket_local_accept(int socket_fd);

int socket_inet_create(const int port_num, int listen_num);
int socket_inet_connect(const char* ip_address, int port_num);
int socket_inet_accept(int socket_fd);

int socket_udp_server(int port_num);
int socket_udp_client(const char* ip_addr, int port_num, struct sockaddr_in *pAddr);
#endif