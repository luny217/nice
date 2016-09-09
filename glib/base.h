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

typedef struct _TimeVal  n_timeval_t;
struct _TimeVal
{
	int32_t tv_sec;
	int32_t tv_usec;
};

void get_current_time(n_timeval_t * result);

void time_val_add(n_timeval_t  * _time, int32_t microseconds);


#endif