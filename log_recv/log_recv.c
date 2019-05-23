#define _GNU_SOURCE

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <pthread.h>
#include <sys/epoll.h>
#include <signal.h>
#include <string.h>
#include <time.h>

#include "socket.h"
#include "message.h"
#include "heartbeat.h"

#define PORT_NUM (3333)
#define USER_CONNECT_MSG_TYPE   	(0X22446688)
#define USER_DISCONN_MSG_TYPE	    (0x88664422)

#define HB_PORT_NUM (8888)
#define HB_PERIOD (2000)  // 2000 ms


const char* filters[] = {"SERVER", "CLIENT", "MAILBOX"};

/* sunday search algorithm */
int sunday_search(char *pMainStr, int mainLen, char *pSubStr, int subLen)
{
	int pos[256] = {0};
	int i = 0;
	for(; i < subLen; i++){
		pos[pSubStr[i] & 0xff] = subLen - i;
	}

	int mainStart = 0, matchLen = 0;
	while((mainStart + subLen) <= mainLen){
		if(pMainStr[mainStart+matchLen] == pSubStr[matchLen]){
			++matchLen;
			if(matchLen == subLen){
				return mainStart;
			}
		}
		else{
			char c = pMainStr[mainStart+subLen] & 0xff;
			if(pos[c] != 0){
				matchLen = 0;
				mainStart += pos[c];
			}
			else{
				mainStart += (subLen + 1);
				matchLen = 0;
			}
		}
	}
	return -1;
}

int log_filter(const char* log)
{
	int i = 0;
	for(; i < sizeof(filters) / sizeof(filters[0]); i++) {
		if(filters[i]) {
			if(sunday_search(log, strlen(log), filters[i], strlen(filters[i])) != -1) {
				//printf("main: %s, sub: %s\n", log, filters[i]);
				return 0;
			}
		}
	}
	return -1;
}

typedef struct
{
	int socket_fd;
	int epoll_fd;
	int user_id;
}stUser;

static stUser s_user = {-1};

void signal_handle(int signo) 
{
    printf("oops! stop!!!\n");
	if(s_user.socket_fd != -1) {
		stMsg msg;
		msg.owner = s_user.user_id;
		msg.msg_type = USER_DISCONN_MSG_TYPE;
		write(s_user.socket_fd, &msg, sizeof(stMsg));
		close(s_user.socket_fd);
		if(s_user.epoll_fd != -1)
			close(s_user.epoll_fd);
		printf("[log recv] signal handle finished, socket fd = %d, epoll fd = %d\n",
			s_user.socket_fd, s_user.epoll_fd);
	}
    _exit(0);
}

int main(int argc, char *argv[])
{
	int socket_fd;
	s_user.epoll_fd = -1;
	s_user.socket_fd = -1;
	s_user.user_id = -1;
	
	//system("clear");
	if(argc != 2 && argc != 3){
        printf("Useage: ./log_recv [ip_address] [filepath]\n");
        printf("example:\n");
        printf("    <1> ./log_recv 192.168.1.1\n");
		printf("    <2> ./log_recv 192.168.1.1 /tmp/log.txt\n");
        exit(-1);
    }

	printf("ip_addr = %s\n", argv[1]);

	signal(SIGINT, signal_handle); // ctr+c

	int retry = 0;
	while((socket_fd = socket_inet_connect(argv[1], PORT_NUM)) < 0) {
		fprintf(stderr, "connect inet socket fail, retry %d!\n", ++retry);
		if(retry > 10) exit(-1);
		usleep(1000000);
	}

	int epoll_fd = epoll_create(1);
	struct epoll_event ev;
	ev.events = EPOLLIN | EPOLLRDHUP;
	ev.data.fd = socket_fd;
	epoll_ctl(epoll_fd, EPOLL_CTL_ADD, socket_fd, &ev);
	struct epoll_event events[1];

	printf("connect success.\n");

	FILE *log_fd = NULL;
	if(argc == 3) {
		log_fd = fopen(argv[2], "wb");
	}

	struct timespec time;
	clock_gettime(CLOCK_REALTIME, &time);
	int user_id = time.tv_sec;
	stMsg msg;
	msg.owner = user_id;
	msg.msg_type = USER_CONNECT_MSG_TYPE;
	write(socket_fd, &msg, sizeof(stMsg));
	printf("owner = %d, socket fd = %d, epoll fd = %d\n", msg.owner, socket_fd, epoll_fd);

	heartbeat_client_create(argv[1], HB_PORT_NUM, user_id, HB_PERIOD);

	s_user.socket_fd = socket_fd;
	s_user.epoll_fd = epoll_fd;
	s_user.user_id = user_id;
	while(1)
	{
		memset(events, 0, sizeof(struct epoll_event) * 1);
		int eventNum = epoll_wait(epoll_fd, events, 1, -1);
		char log[256] = {'\0'};
		//printf("event: %x\n", events[0].events);
		if((events[0].events & EPOLLIN) && (events[0].events & EPOLLRDHUP)) {
			fprintf(stderr, "[log recv] network server has terminate.\n");
			close(socket_fd);
			close(epoll_fd);
			exit(0);
		}
		
		if((eventNum > 0) && (events[0].events & EPOLLIN)) {
			int cnt = 0;
			if((cnt = read(events[0].data.fd, &log[0], 256)) > 0){
				if(!log_fd) {
					if(log_filter(log) != -1)
						printf(log);
				}
				else {
					fwrite(log, cnt, 1, log_fd);
				}
			}
			else {
				fprintf(stderr, "[log recv]No data to read, cnt = %d!\n", cnt);
			}
		}
		
	}

	msg.owner = user_id;
	msg.msg_type = USER_DISCONN_MSG_TYPE;
	write(socket_fd, &msg, sizeof(stMsg));
	close(socket_fd);
	close(epoll_fd);
	return 0;
}
