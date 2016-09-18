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

#include <stdio.h>
#include <stdint.h>
#include <string.h>

#ifdef _WIN32
#include <windows.h>
#else
#include <unistd.h>
#include <sys/time.h>
#endif

#include "pthread.h"
#include "base.h"
#include "event.h"

typedef struct
{
	uint32_t flag; 
	int32_t event; 
	pthread_cond_t cond;
	pthread_mutex_t mutex;
	int32_t pst_tm_max[3];   /* post time cost stats (total usr sys)*/
} EVENT_FD_S, *PEVENT_FD_S;
#define EVENT_FD_S_LEN  sizeof(EVENT_FD_S)

typedef struct
{
	uint8_t opened;
	uint16_t post_cost_threshold;
	uint8_t reserved;
} EVENT_PARAM_S, *PEVENT_PARAM_S;
#define EVENT_PARAM_S_LEN   sizeof(EVENT_PARAM_S)

static EVENT_PARAM_S event_param = { 0 };

int32_t event_open(void)
{
	PEVENT_PARAM_S pevent_param = &event_param;
	PEVENT_FD_S fd = NULL;

	if (NULL == (fd = malloc(EVENT_FD_S_LEN)))
	{
		return -1;
	}
	pthread_mutex_init(&fd->mutex, NULL);
	pthread_cond_init(&fd->cond, NULL);
	if (FALSE == pevent_param->opened)
	{
		pevent_param->opened = TRUE;
	}

	return (int32_t)fd;
}

int32_t event_recv(int32_t handle, int32_t want, int32_t *events)
{
	PEVENT_FD_S fd = (PEVENT_FD_S)handle;

	if (handle <= 0)
	{
		printf("handle is invalid\n");
		return -1;
	}

	if (NULL == events)
	{
		printf("unsupported events is NULL\n");
		return -1;
	}

	printf("enter handle(0x%x) want(0x%x)\n", handle, want);
	pthread_mutex_lock(&fd->mutex);
	*events = (fd->event & want), fd->event &= ~want;
	pthread_mutex_unlock(&fd->mutex);
	printf("leave handle(0x%x) events(0x%x) fd->event(0x%x)\n", handle, *events, fd->event);

	return 0;
}

int32_t event_wait(int32_t handle, int32_t want, int32_t *events)
{
	PEVENT_FD_S fd = (PEVENT_FD_S)handle;

	if (NULL == events)
	{
		printf("unsupported events is NULL\n");
		return -1;
	}
	printf("enter handle(0x%x) want(0x%x)\n", handle, want);
	pthread_mutex_lock(&fd->mutex);
	while (0 == (fd->event & want))
	{
		printf("cond wait\n");
		pthread_cond_wait(&fd->cond, &fd->mutex);
	}
	*events = (fd->event & want), fd->event &= ~want;
	pthread_mutex_unlock(&fd->mutex);

	printf("leave handle(0x%x) events(0x%x) fd->event(0x%x)\n", handle, *events, fd->event);

	return 0;
}

int32_t event_post(int32_t handle, int32_t events)
{
	PEVENT_FD_S fd = (PEVENT_FD_S)handle;
	int32_t pst_tm_cur[3] = { 0 };
	n_timeval_t  func_start, func_stop;
	int32_t usec_time = 0;

	if (event_param.post_cost_threshold)
	{
		//runtime_get_msec(&pst_tm_cur[0], &pst_tm_cur[1], &pst_tm_cur[2]);
	}
	printf("enter handle(0x%x) event(0x%x) events(0x%x)\n", handle, fd->event, events);

	get_current_time(&func_start);
	pthread_mutex_lock(&fd->mutex);
	get_current_time(&func_stop);

	usec_time = ((func_stop.tv_sec - func_start.tv_sec) * 1000 * 1000) + (func_stop.tv_usec - func_start.tv_usec);
	printf("lock usec_time(%dUS)\n", usec_time);
	fd->event |= events;
	pthread_cond_signal(&fd->cond);
	pthread_mutex_unlock(&fd->mutex);

	get_current_time(&func_start);
	usec_time = ((func_start.tv_sec - func_stop.tv_sec) * 1000 * 1000) + (func_start.tv_usec - func_stop.tv_usec);
	printf("unlock usec_time(%dUS)\n", usec_time);
	printf("leave handle(0x%x) event(0x%x) events(0x%x)\n", handle, fd->event, events);

	/*if (event_param.post_cost_threshold)
	{
		pst_tm_cur[0] = runtime_get_msec(&pst_tm_cur[0], &pst_tm_cur[1], &pst_tm_cur[2]);
		fd->pst_tm_max[0] = IMAX(fd->pst_tm_max[0], pst_tm_cur[0]);
		fd->pst_tm_max[1] = IMAX(fd->pst_tm_max[1], pst_tm_cur[1]);
		fd->pst_tm_max[2] = IMAX(fd->pst_tm_max[2], pst_tm_cur[2]);
		if (pst_tm_cur[0] > event_param.post_cost_threshold)
		{
			printf("post cost (total %d, usr %d, sys %d), MAX (total %d, usr %d, sys %d)\n",
				pst_tm_cur[0], pst_tm_cur[1], pst_tm_cur[2], fd->pst_tm_max[0], fd->pst_tm_max[1], fd->pst_tm_max[2]);
		}
	}*/

	return 0;
}

int32_t event_close(int32_t handle)
{
	PEVENT_FD_S fd = (PEVENT_FD_S)handle;

	fd->flag = 0;
	pthread_cond_destroy(&fd->cond);
	pthread_mutex_destroy(&fd->mutex);
	free(fd);
	fd = NULL;

	return 0;
}
