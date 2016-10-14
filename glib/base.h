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
#endif