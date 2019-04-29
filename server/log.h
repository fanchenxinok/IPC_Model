#ifndef __LOG_WRAPPER_H__
#define __LOG_WRAPPER_H__
#include <stdio.h>
#include <stdarg.h>
#include <pthread.h>
#include <limits.h>
#include <linux/sched.h>

/* log color */
#define LOG_COL_NONE
#define LOG_COL_END         "\033[0m"
#define LOG_COL_BLK  	"\033[0;30m" /* black */
#define LOG_COL_RED  	"\033[0;31m" /* red */
#define LOG_COL_GRN  	"\033[0;32m" /* green */
#define LOG_COL_YLW  	"\033[0;33m" /* yellow */
#define LOG_COL_BLU  	"\033[0;34m" /* blue */
#define LOG_COL_PUR  	"\033[0;35m" /* purple */
#define LOG_COL_CYN  	"\033[0;36m" /* cyan */
#define LOG_COL_WHI  	"\033[0;37m"
#define LOG_COL_RED_BLK "\033[0;31;40m"
#define LOG_COL_RED_YLW "\033[0;31;43m"
#define LOG_COL_RED_WHI "\033[0;31;47m"
#define LOG_COL_GRN_BLK "\033[0;32;40m"
#define LOG_COL_YLW_BLK "\033[0;33;40m"
#define LOG_COL_YLW_GRN "\033[1;33;42m"
#define LOG_COL_YLW_PUR "\033[0;33;45m"
#define LOG_COL_WHI_GRN "\033[0;37;42m"

/* log level */
typedef enum{
	LOG_LEVEL_NONE,
	LOG_LEVEL_ERR,
	LOG_LEVEL_WARNING,
	LOG_LEVEL_NOTIFY,
	LOG_LEVEL_ALL
}enLogLevel;

extern void _LogOut_(int Level, const char Format[], ...) __attribute__((format(printf,2,3)));

#define LOG_OUTPUT(_level_, _fmt_, ...) _LogOut_(_level_, _fmt_, ##__VA_ARGS__)

#define LOG_ERR(_fmt_, ...)		LOG_OUTPUT(LOG_LEVEL_ERR, _fmt_, ##__VA_ARGS__)
#define LOG_WARNING(_fmt_, ...)	LOG_OUTPUT(LOG_LEVEL_WARNING, _fmt_, ##__VA_ARGS__)
#define LOG_NOTIFY(_fmt_, ...)	LOG_OUTPUT(LOG_LEVEL_NOTIFY, _fmt_, ##__VA_ARGS__)

#define LOG_THREAD_ON (0)

/*
    if LOG_THREAD_ON be set, output log by log thread,
    else output log through TCP to log receiver.
*/
void log_task_init();
int log_task_fin();

void log_set_level(int level);

#define DEBUG_TRACE() \
{ \
	LOG_NOTIFY("FUNC: %s(), at line = %d\n",  __FUNCTION__, __LINE__); \
}
#endif