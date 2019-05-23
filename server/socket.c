#include <stdlib.h> 
#include <stdio.h> 
#include <errno.h> 
#include <sys/types.h> 
#include <sys/socket.h>
#include <sys/un.h>
#include <ifaddrs.h>
#include <netinet/in.h> 
#include <arpa/inet.h>
#include <netdb.h>
#include <fcntl.h>
#include <signal.h>
#include <stddef.h>

static void fcntl_set_cloexec(int fd)
{
	long flags;
	flags = fcntl(fd, F_GETFD);
	if (flags != -1) {
		if (fcntl(fd, F_SETFD, flags | FD_CLOEXEC) < 0) {
			fprintf(stderr, "fcntl_set_cloexec error: %s\n", strerror(errno));
		}
	}
}

void socket_show_ip_addrs()
{
 
    struct ifaddrs* ifAddrStruct = NULL, *tmpIfAddrStruct = NULL;
    void * tmpAddrPtr = NULL;
 
    getifaddrs(&ifAddrStruct);

	tmpIfAddrStruct = ifAddrStruct;

	printf("==========================\n");
    while (ifAddrStruct != NULL) 
	{
        if (ifAddrStruct->ifa_addr->sa_family == AF_INET)
		{   // check it is IP4
            // is a valid IP4 Address
            tmpAddrPtr = &((struct sockaddr_in *)ifAddrStruct->ifa_addr)->sin_addr;
            char addressBuffer[INET_ADDRSTRLEN];
            inet_ntop(AF_INET, tmpAddrPtr, addressBuffer, INET_ADDRSTRLEN);
            printf("%s IPV4 Address %s\n", ifAddrStruct->ifa_name, addressBuffer); 
        }
		else if (ifAddrStruct->ifa_addr->sa_family == AF_INET6)
		{   // check it is IP6
            // is a valid IP6 Address
            tmpAddrPtr=&((struct sockaddr_in *)ifAddrStruct->ifa_addr)->sin_addr;
            char addressBuffer[INET6_ADDRSTRLEN];
            inet_ntop(AF_INET6, tmpAddrPtr, addressBuffer, INET6_ADDRSTRLEN);
            printf("%s IPV6 Address %s\n", ifAddrStruct->ifa_name, addressBuffer); 
        } 
        ifAddrStruct = ifAddrStruct->ifa_next;
    }
	printf("==========================\n");
	freeifaddrs(tmpIfAddrStruct);
}

int socket_local_create(const char* server_name, int listen_num)
{
	int socket_fd = -1;
	struct sockaddr_un socket_addr;
	/* for forbiding server quit, when client terminate accidently */
    signal(SIGPIPE, SIG_IGN);
    /* forbiden this signal */
    sigset_t signal_mask;
    sigemptyset (&signal_mask);
    sigaddset (&signal_mask, SIGPIPE);
    int rc = pthread_sigmask (SIG_BLOCK, &signal_mask, NULL);
    if (rc != 0) {
        fprintf(stderr, "block sigpipe error\n");
    } 

    /* delete old socket file */
    //unlink(SERVER_NAME); 
    /* create PF_LOCAL socket used to communicate on the same machine efficiently */ 
    if((socket_fd = socket(PF_LOCAL, SOCK_STREAM, 0)) < 0) {
        fprintf(stderr,"Socket error:%s\n", strerror(errno)); 
        return -1; 
    } 

	fcntl_set_cloexec(socket_fd);
    printf("Create Server Socket success.....\n");

	memset(&socket_addr, 0, sizeof socket_addr);
	socket_addr.sun_family = AF_LOCAL;
	int name_size = 0;
	if(server_name) {
		name_size = snprintf(socket_addr.sun_path, sizeof socket_addr.sun_path, "%s", server_name);
	}
	else {
		name_size = snprintf(socket_addr.sun_path, sizeof socket_addr.sun_path, "%s", "#server_unix");
	}
	socket_addr.sun_path[0] = 0; /* the second name type:https://blog.csdn.net/xnwyd/article/details/7359506 */

	/* bind the socket */
	int n = 1;
    if(setsockopt(socket_fd, SOL_SOCKET, SO_REUSEADDR, &n, sizeof(int)) < 0){
        fprintf(stderr, "setsockopt fail: %s\n", strerror(errno));
    }
    
	socklen_t size;
	size = offsetof(struct sockaddr_un, sun_path) + name_size;
	if (bind(socket_fd, (struct sockaddr *) &socket_addr, size) < 0) {
		fprintf(stderr, "bind() failed with error: %s, size = %d\n", strerror(errno), size);
		close(socket_fd);
		return -1;
	}

    /* set the socket listen num */ 
    printf("start listening.....\n");
    printf("The max number of client to allow to wait to connect with server is:%d.....\n", listen_num);
	listen_num = (listen_num < 0) ? 1 : listen_num;
    if(listen(socket_fd, listen_num) < 0){ 
        fprintf(stderr,"Listen error: %s\n", strerror(errno));
		close(socket_fd);
        return -1; 
    }

	return socket_fd;
}

int socket_local_connect(const char* server_name)
{
	int socket_fd = 1;
	/* create PF_LOCAL socket used to communicate on the same machine efficiently */ 
    if((socket_fd = socket(PF_LOCAL, SOCK_STREAM, 0)) < 0) {
        fprintf(stderr,"connect Socket error:%s\n", strerror(errno)); 
        return -1; 
    } 

	struct sockaddr_un socket_addr;
	memset(&socket_addr, 0, sizeof socket_addr);
	socket_addr.sun_family = AF_LOCAL;
	int name_size = 0;
	if(server_name) {
		name_size = snprintf(socket_addr.sun_path, sizeof socket_addr.sun_path, "%s", server_name);
	}
	else {
		name_size = snprintf(socket_addr.sun_path, sizeof socket_addr.sun_path, "%s", "#server_unix");
	}
	socket_addr.sun_path[0] = 0;

	socklen_t size;
	size = offsetof(struct sockaddr_un, sun_path) + name_size;
	if (connect(socket_fd, (struct sockaddr *) &socket_addr, size) < 0) {
		close(socket_fd);
		fprintf(stderr,"connect error:%s\n", strerror(errno)); 
		return -1;
	}

	fcntl_set_cloexec(socket_fd);
	return socket_fd;
}

int socket_local_accept(int socket_fd)
{
    socklen_t len = sizeof(struct sockaddr_un); 
    struct sockaddr_un client_addr;
    int new_fd = -1;
    if((new_fd = accept(socket_fd, (struct sockaddr *)(&client_addr), &len)) == -1){ 
        fprintf(stderr,"Accept error:%s\n", strerror(errno)); 
        return -1;
    } 
    printf("@@@ local server get connection from socketFd = %d\n", new_fd);

    fcntl_set_cloexec(new_fd);
	return new_fd;
}

int socket_inet_create(const int port_num, int listen_num)
{
	int socket_fd = -1;
	struct sockaddr_in socket_addr;
	/* for forbiding server quit, when client terminate accidently */
    signal(SIGPIPE, SIG_IGN);
    /* forbiden this signal */
    sigset_t signal_mask;
    sigemptyset (&signal_mask);
    sigaddset (&signal_mask, SIGPIPE);
    int rc = pthread_sigmask (SIG_BLOCK, &signal_mask, NULL);
    if (rc != 0) {
        fprintf(stderr, "block sigpipe error\n");
    } 

    /* create AF_INET socket used to communicate on the same machine efficiently */ 
    if((socket_fd = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
        fprintf(stderr,"Socket error:%s\n", strerror(errno));
		return -1;
    } 

    printf("Create network Socket success.....\n");

	bzero(&socket_addr, sizeof(struct sockaddr_in));
	socket_addr.sin_family = AF_INET;
	socket_addr.sin_addr.s_addr = htonl(INADDR_ANY);
	//socket_addr.sin_addr.s_addr = inet_addr("192.168.1.1");
	socket_addr.sin_port = htons(port_num);
	
	/* bind the socket */
	int n = 1;
    if(setsockopt(socket_fd, SOL_SOCKET, SO_REUSEADDR, &n, sizeof(int)) < 0){
        fprintf(stderr, "setsockopt fail: %s\n", strerror(errno));
    }
    
	if (bind(socket_fd, (struct sockaddr *) &socket_addr, sizeof(struct sockaddr)) < 0) {
		fprintf(stderr, "bind() failed with error: %s\n", strerror(errno));
		close(socket_fd);
		return -1;
	}
	printf("network bind socket finished.\n");

    /* set the socket listen num */
    if(listen(socket_fd, listen_num) < 0){ 
        fprintf(stderr,"Listen error: %s\n", strerror(errno));
		close(socket_fd);
        return -1; 
    } 
	return socket_fd;
}

int socket_inet_connect(const char* ip_address, int port_num)
{
	int socket_fd = -1;
	if((socket_fd = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
        fprintf(stderr,"Socket error:%s\n", strerror(errno)); 
        return -1; 
    } 
	
	struct in_addr ip_addr;
	struct sockaddr_in socket_addr;
	bzero(&socket_addr, sizeof(struct sockaddr_in));
	socket_addr.sin_family = AF_INET;
	socket_addr.sin_port = htons(port_num);
	inet_aton(ip_address, &ip_addr);
	socket_addr.sin_addr = ip_addr;

	if(connect(socket_fd, (struct sockaddr*)(&socket_addr), sizeof(struct sockaddr)) < 0) {
		printf("connect error\n"); 
		close(socket_fd);
		return -1;
	}

	return socket_fd;
}

int socket_inet_accept(int socket_fd)
{
    socklen_t len = sizeof(struct sockaddr_in); 
    struct sockaddr_in user_addr;
    int new_fd = -1;
    if((new_fd = accept(socket_fd, (struct sockaddr *)(&user_addr), &len)) == -1){ 
        fprintf(stderr,"Accept error:%s\n", strerror(errno)); 
        return - 1;
    } 
    printf("@@@ inet server get connection from socketFd = %d, ip address = %s\n", new_fd, inet_ntoa(user_addr.sin_addr));
	return new_fd;
}

int socket_udp_server(int port_num)
{
	int socket_fd = -1;
	struct sockaddr_in server_addr;
	
  	/* create UDP socket */
    if ((socket_fd = socket(AF_INET, SOCK_DGRAM, 0)) < 0){  
        fprintf(stderr, "udp: failed to create socket\n");  
        return -1;  
    }  
  
    server_addr.sin_family = AF_INET;  
    server_addr.sin_port = htons(port_num);  
    server_addr.sin_addr.s_addr = htonl(INADDR_ANY);  
  
    if (bind(socket_fd, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0){  
        fprintf(stderr, "udp: failed to bind\n"); 
		close(socket_fd);
        return -1;  
    }
	return socket_fd;
}

int socket_udp_client(const char* ip_addr, int port_num, struct sockaddr_in *pAddr)
{
	int socket_fd = -1;
    struct sockaddr_in server_addr;  
	
    if ((socket_fd = socket(AF_INET, SOCK_DGRAM, 0)) < 0) {  
        fprintf(stderr, "heart beat create client udp socket fail\n");  
        return -1;  
    }
  
    pAddr->sin_family = AF_INET;  
    pAddr->sin_port = htons(port_num);  
    pAddr->sin_addr.s_addr = inet_addr(ip_addr);
	return socket_fd;
}