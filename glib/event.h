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
#ifndef __EVENT_H__
#define __EVENT_H__

#include <stdint.h>

#define USEC_PER_SEC 1000000

int32_t event_open(void);

int32_t event_wait(int32_t handle, int32_t want, int32_t *events, void ** n_data);

int32_t event_post(int32_t handle, int32_t events, void * n_data);

int32_t event_close(int32_t handle);

#endif /* _EVENT_H_ */
