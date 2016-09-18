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

#include <signal.h>
#include <sys/types.h>
#include <time.h>
#include <stdint.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>

#ifdef _WIN32
#include <windows.h>
#else
#include <unistd.h>
#include <sys/time.h>
#endif

#include "base.h"

void get_current_time(n_timeval_t * result)
{
#ifndef _WIN32
	struct timeval r;

	g_return_if_fail(result != NULL);

	/*this is required on alpha, there the timeval structs are int's
	not longs and a cast only would fail horribly*/
	gettimeofday(&r, NULL);
	result->tv_sec = r.tv_sec;
	result->tv_usec = r.tv_usec;
#else
	FILETIME ft;
	uint64_t time64;

	GetSystemTimeAsFileTime(&ft);
	memmove(&time64, &ft, sizeof(FILETIME));

	/* Convert from 100s of nanoseconds since 1601-01-01
	* to Unix epoch. Yes, this is Y2038 unsafe.
	*/
	time64 -= 116444736000000000i64;
	time64 /= 10;

	result->tv_sec = (int32_t) (time64 / 1000000);
	result->tv_usec =(int32_t)(time64 % 1000000);
#endif
}

void time_val_add(n_timeval_t  * _time, int32_t microseconds)
{
	if (microseconds >= 0)
	{
		_time->tv_usec += microseconds % USEC_PER_SEC;
		_time->tv_sec += microseconds / USEC_PER_SEC;
		if (_time->tv_usec >= USEC_PER_SEC)
		{
			_time->tv_usec -= USEC_PER_SEC;
			_time->tv_sec++;
		}
	}
	else
	{
		microseconds *= -1;
		_time->tv_usec -= microseconds % USEC_PER_SEC;
		_time->tv_sec -= microseconds / USEC_PER_SEC;
		if (_time->tv_usec < 0)
		{
			_time->tv_usec += USEC_PER_SEC;
			_time->tv_sec--;
		}
	}
}

void sleep_us(uint32_t microseconds)
{
#ifdef _WIN32
	Sleep(microseconds / 1000);
#else
	struct timespec request, remaining;
	request.tv_sec = microseconds / USEC_PER_SEC;
	request.tv_nsec = 1000 * (microseconds % USEC_PER_SEC);
	while (nanosleep(&request, &remaining) == -1 && errno == EINTR)
		request = remaining;
#endif
}

void sleep_ms(uint32_t microseconds)
{
	//sleep_us
}