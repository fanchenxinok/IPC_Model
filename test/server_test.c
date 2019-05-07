#define _GNU_SOURCE

#include <stdio.h>
#include <time.h>
#include <pthread.h>
#include <signal.h>
#include <time.h>
#include <unistd.h>
#include "server.h"
#include "log.h"
#include "common.h"

#include "../services/arithmetic_service_interface.h"
#include "../services/arithmetic_service.h"

#define TEST_LOG(msg, ...)		LOG_NOTIFY(LOG_COL_YLW_BLK "[CLIENT]"msg LOG_COL_END"\n", ##__VA_ARGS__)

static int send_msg_cnt = 0;
static int recv_msg_cnt = 0;

void client_add_callback(void *user, int error_no)
{
	recv_msg_cnt++;
	int result = *(int*)user;
	TEST_LOG("The result of add is : %d", result);
}

void client_max_callback(void *user, int error_no)
{
	recv_msg_cnt++;
	int result = *(int*)user;
	TEST_LOG("The result of max is : %d", result);
}

void client_ctl_callback(void *user, int error_no)
{
	recv_msg_cnt++;
	int result = *(int*)user;
	TEST_LOG("The result of ctrl is : %d", result);
}

void client_say_callback(void *user, int error_no)
{
	recv_msg_cnt++;
	char* result = (char*)user;
	TEST_LOG("%s", result);
	usleep(100000);
}

void client_info_callback(void *user, int error_no)
{
	recv_msg_cnt++;
	char* result = (char*)user;
	TEST_LOG("Service info: %s", result);
}

void signal_handle(int signo) 
{
    printf("oops! stop!!!\n");
	printf("Total send msg: %d, total receive msg: %d, discard msg: %d, service cnt: %d\n", 
		send_msg_cnt, recv_msg_cnt, as_get_discard_msg_cnt(), get_arithmetic_command_cnt());
	server_destory();
    _exit(0);
}

void* client_test(void* data)
{
	stClientProxy *pClient = (stClientProxy*)data;
	while(1)
	{
		stCbInfo cb_info;
		cb_info.cb_func = client_add_callback;
		cb_info.cb_async = 0;
		cb_info.wait_time = 0;
		as_add_req(pClient, 1000, 5000, &cb_info);
		send_msg_cnt++;

		cb_info.cb_func = client_max_callback;
		cb_info.cb_async = 1;
		as_max_req(pClient, 100, 400, &cb_info);
		send_msg_cnt++;

		cb_info.cb_func = client_ctl_callback;
		cb_info.cb_async = 0;
		cb_info.wait_time = 1000;
		as_ctrl_req(pClient, &cb_info);
		send_msg_cnt++;

		cb_info.cb_func = client_say_callback;
		cb_info.cb_async = 1;
		as_say_req(pClient, "Hello server!", &cb_info);
		send_msg_cnt++;

		cb_info.cb_func = client_info_callback;
		cb_info.cb_async = 0;
		cb_info.wait_time = 500;
		as_inf_req(pClient, &cb_info);
		send_msg_cnt++;
		usleep(100000);
	}
	return NULL;
}

void client_cancel_thread(pthread_t thread_id, const char* thread_name)
{
	if (0 == pthread_cancel(thread_id)){
		pthread_join(thread_id, NULL);
        printf("@@@@Thread %s finish success@@@@\n", thread_name);
    }
	return;
}

int main(int argc, char *argv[]) 
{ 
	signal(SIGINT, signal_handle); // ctr+c
	
    server_create();
	server_start();
	
	run_arithmetic_service(); // run the arithmetic service in server

	#if 1
	stClientProxy *client0 = client_connect("client0", ARITHMETIC_SERVICE_NAME, 0);
	stClientProxy *client1 = client_connect("client1", ARITHMETIC_SERVICE_NAME, 1);
	stClientProxy *client2 = client_connect("client2", ARITHMETIC_SERVICE_NAME, 0);

	pthread_t thread_id[10];
	pthread_create_thread(&thread_id[1], "client1_cmd", &client_test, (void*)client1);
	pthread_create_thread(&thread_id[2], "client2_cmd", &client_test, (void*)client2);

	usleep(2000000);
	server_stop();
	usleep(2000000);
	server_start();

	stClientProxy *client3 = client_connect("client3", ARITHMETIC_SERVICE_NAME, 1);
	pthread_create_thread(&thread_id[3], "client3_cmd", &client_test, (void*)client3);
	stClientProxy *client4 = client_connect("client4", ARITHMETIC_SERVICE_NAME, 1);
	pthread_create_thread(&thread_id[4], "client4_cmd", &client_test, (void*)client4);
	stClientProxy *client5 = client_connect("client5", ARITHMETIC_SERVICE_NAME, 0);
	pthread_create_thread(&thread_id[5], "client5_cmd", &client_test, (void*)client5);
	stClientProxy *client6 = client_connect("client6", ARITHMETIC_SERVICE_NAME, 1);
	pthread_create_thread(&thread_id[6], "client6_cmd", &client_test, (void*)client6);
	stClientProxy *client7 = client_connect("client7", ARITHMETIC_SERVICE_NAME, 0);
	pthread_create_thread(&thread_id[7], "client7_cmd", &client_test, (void*)client7);
	usleep(10000000);

	client_cancel_thread(thread_id[2], "client2_cmd");
	client_disconnect(client2);
	client_cancel_thread(thread_id[4], "client4_cmd");
	client_disconnect(client4);
	client_cancel_thread(thread_id[6], "client6_cmd");
	client_disconnect(client6);

	int cnt = 0;
	while(1)
	{
		usleep(100000);
		/*
		if(cnt++ > 5) {
			//server_stop();
			client_disconnect(client1);
			client_disconnect(client3);
			usleep(100000);
			server_destory();
			break;
		}
		*/
		//TEST_LOG("sleep 1 second");

		client4 = client_connect("client4", ARITHMETIC_SERVICE_NAME, 1);
		pthread_create_thread(&thread_id[4], "client4_cmd", &client_test, (void*)client4);

		usleep(1000000);

		client_cancel_thread(thread_id[4], "client4_cmd");
		client_disconnect(client4);

		if(cnt++ > 100) break;
	}

	printf("Total send msg: %d, total receive msg: %d, discard msg: %d, service cnt: %d\n", 
		send_msg_cnt, recv_msg_cnt, as_get_discard_msg_cnt(), get_arithmetic_command_cnt());

	#else
	stClientProxy *client1 = client_connect("client1", ARITHMETIC_SERVICE_NAME, client_async_dispose_msg);

	pthread_t thread_id;
	pthread_create(&thread_id, NULL, &client_test, (void*)client1);
	while(1) {
		usleep(1000000);
	}
	#endif
	server_destory();
	return 0;
} 
