/************************************************
*   Author:  Fanchenxin
*   Date:  2018/05/17
*************************************************/
#ifndef _TEST_COMMON_H_
#define _TEST_COMMON_H_
#include <unistd.h>
#include <pthread.h>

typedef unsigned char  uInt8;
typedef unsigned short uInt16;
typedef unsigned int uInt32;
typedef unsigned long long uInt64;
typedef char Int8;
typedef short Int16;
typedef int Int32;
typedef signed long long Int64;

typedef enum
{
	FALSE = 0,
	TRUE = 1
}enBOOL;

typedef struct
{
	uInt32 x;
	uInt32 y;
	uInt32 width;
	uInt32 height;
}stRect;

#define MAX_CONNECT_CLIENT  (20)  // max number of client connect to server

/* Alignment with a power of two value. */
#define COMM_ALIGN(n, align) (((n) + (align) - 1L) & ~((align) - 1L))
#define ARRAY_SIZE(array) (sizeof((array)) / sizeof((array)[0]))
/* get the lower n bits of v */
#define GET_BITS(v, n) ((unsigned)v & ((1U << (n)) - 1))
/* get the bits from f to t */
#define GET_BITS_RANG(v, f, t) (((v) >> (f)) & (~0U >> (31 - t + f)))

#define MAX(a, b) (((a) > (b)) ? (a) : (b))
#define MIN(a, b) (((a) < (b)) ? (a) : (b))

#define COLOR_ARGB8(A, R, G, B) \
    ((((A)&0xFF) << 24) | (((R)&0xFF) << 16) | (((G)&0xFF) << 8) | ((B)&0xFF))

#define CHECK_POINTER_VALID(P) \
	do{ \
		if(NULL == P){ \
			printf("Pointer: %s is NULL!", #P); \
			return; \
		} \
	}while(0)

#define CHECK_POINTER_VALID_RET(P, V) \
	do{ \
		if(NULL == P){ \
			printf("Pointer: %s is NULL!", #P); \
			return V; \
		} \
	}while(0)

#define SAFETY_MUTEX_LOCK(mutex) \
do{ \
	pthread_cleanup_push(pthread_mutex_unlock, &(mutex)); \
	pthread_mutex_lock(&(mutex))
						
#define SAFETY_MUTEX_UNLOCK(mutex) \
	pthread_mutex_unlock(&mutex); \
	pthread_cleanup_pop(0); \
}while(0)

#endif
