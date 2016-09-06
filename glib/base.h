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

typedef struct _TimeVal  g_time_val;
struct _TimeVal
{
	int32_t tv_sec;
	int32_t tv_usec;
};

void get_current_time(g_time_val * result);

void time_val_add(g_time_val  * _time, int32_t microseconds);


#endif