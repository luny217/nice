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

#define N_ELEMENTS(arr)		(sizeof (arr) / sizeof ((arr)[0]))
#define MAX(a, b)  (((a) > (b)) ? (a) : (b))
#define MIN(a, b)  (((a) < (b)) ? (a) : (b))

#define G_MININT8	((int8_t) -0x80)
#define G_MAXINT8	((int8_t)  0x7f)
#define G_MAXUINT8	((uint8_t) 0xff)

#define G_MININT16	((int16_t) -0x8000)
#define G_MAXINT16	((int16_t)  0x7fff)
#define G_MAXUINT16	((uint16_t) 0xffff)

#define G_MININT32	((int32_t) -0x80000000)
#define G_MAXINT32	((int32_t)  0x7fffffff)
#define G_MAXUINT32	((uint32_t) 0xffffffff)

#define G_MININT64	((int64_t) G_GINT64_CONSTANT(-0x8000000000000000))
#define G_MAXINT64	G_GINT64_CONSTANT(0x7fffffffffffffff)
#define G_MAXUINT64	G_GUINT64_CONSTANT(0xffffffffffffffff)

#if defined (__GNUC__) && defined (__cplusplus)
#define G_STRFUNC     ((const char*) (__PRETTY_FUNCTION__))
#elif defined (__STDC_VERSION__) && __STDC_VERSION__ >= 199901L
#define G_STRFUNC     ((const char*) (__func__))
#elif defined (__GNUC__) || (defined(_MSC_VER) && (_MSC_VER > 1300))
#define G_STRFUNC     ((const char*) (__FUNCTION__))
#else
#define G_STRFUNC     ((const char*) ("???"))
#endif

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

typedef unsigned long nfds_t;

#if 0 //def _WIN32
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
#endif

#if 1
int net_errno(void);
#endif


#endif