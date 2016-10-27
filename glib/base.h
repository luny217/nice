/*
 * FileName:       
 * Author:         luny  Version: 1.0  Date: 2016-9-6
 * Description:    
 * Version:        
 * Function List:  
 *                 1.
 * History:        
 *     <author>   <time>    <version >   <desc>
 */
#ifndef __BASE_H__
#define __BASE_H__

#include <stdint.h>

#define USEC_PER_SEC 1000000

#define ONE_MSEC_PER_USEC   1000
#define ONE_SEC_PER_MSEC    1000
#define ONE_SEC_PER_USEC    (ONE_MSEC_PER_USEC * ONE_SEC_PER_MSEC)

typedef struct _TimeVal  n_timeval_t;
struct _TimeVal
{
	int32_t tv_sec;
	int32_t tv_usec;
};

int32_t atomic_int_get(const volatile int32_t *atomic);

void atomic_int_set(volatile int32_t *atomic, int32_t newval);

void atomic_int_inc(volatile int32_t *atomic);

void get_current_time(n_timeval_t * result);

void time_val_add(n_timeval_t  * _time, int32_t microseconds);

void sleep_us(uint32_t microseconds);

void sleep_ms(uint32_t milliseconds);

void clock_win32_init(void);

int64_t get_monotonic_time(void);

char * n_strdup(const char *str);

char ** n_strsplit(const char * string, const char * delimiter, uint32_t  max_tokens);

char ** n_strsplit_set(const char *string, const char *delimiters, int32_t max_tokens);

void n_strfreev(char **str_array);

void * n_memdup(const void * mem, uint32_t  byte_size);

#ifdef _WIN32
typedef unsigned long nfds_t;

struct pollfd 
{
    int fd;
    short events;  /* events to look for */
    short revents; /* events that occurred */
};

/* events & revents */
#define POLLIN     0x0001  /* any readable data available */
#define POLLOUT    0x0002  /* file descriptor is writeable */
#define POLLRDNORM POLLIN
#define POLLWRNORM POLLOUT
#define POLLRDBAND 0x0008  /* priority readable data */
#define POLLWRBAND 0x0010  /* priority data can be written */
#define POLLPRI    0x0020  /* high priority readable data */

/* revents only */
#define POLLERR    0x0004  /* errors pending */
#define POLLHUP    0x0080  /* disconnected */
#define POLLNVAL   0x1000  /* invalid file descriptor */
int poll(struct pollfd *fds, nfds_t numfds, int timeout);

#if 0
int net_errno(void)
{
    int err = WSAGetLastError();
    switch (err) {
    case WSAEWOULDBLOCK:
        return AVERROR(EAGAIN);
    case WSAEINTR:
        return AVERROR(EINTR);
    case WSAEPROTONOSUPPORT:
        return AVERROR(EPROTONOSUPPORT);
    case WSAETIMEDOUT:
        return AVERROR(ETIMEDOUT);
    case WSAECONNREFUSED:
        return AVERROR(ECONNREFUSED);
    case WSAEINPROGRESS:
        return AVERROR(EINPROGRESS);
    }
    return -err;
}
#endif

#endif
#endif