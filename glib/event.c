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

#include "nlist.h"
#include "pthread.h"
#include "base.h"
#include "event.h"

typedef struct
{
	uint32_t flag; 
	int32_t n_event; 
	pthread_cond_t cond;
	pthread_mutex_t mutex;	
	int32_t pst_tm_max[3];   /* post time cost stats (total usr sys)*/
	void * n_data[32];
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

	if (NULL == (fd = n_slice_new0(EVENT_FD_S)))
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

int32_t event_wait(int32_t handle, int32_t want, int32_t *events, void ** n_data)
{
	PEVENT_FD_S fd = (PEVENT_FD_S)handle;
	int32_t  idx = 0, i, tmp_events , ret;

	if (NULL == events)
	{
		printf("unsupported events is NULL\n");
		return -1;
	}
	//if (count == 1) sleep_ms(1000000);
	//printf("\n[event_wait]: lock want(0x%x) mutex(%p)\n", want, &fd->mutex);
	pthread_mutex_lock(&fd->mutex);
	while (0 == (fd->n_event & want) )
	{
		//printf("\n[cond wait]\n");
		ret = pthread_cond_wait(&fd->cond, &fd->mutex);
		//printf("[cond wait] ret = %d\n", ret);
	}	
	
	tmp_events = fd->n_event;
	for (i = 0; i < 32; i++)
	{
		if ((tmp_events >> i) & 1)
		{
			break;
		}
		idx++;
	}
	*n_data = fd->n_data[idx];
	*events = (fd->n_event & want), fd->n_event &= ~want;
	pthread_mutex_unlock(&fd->mutex);

	//printf("\n[event_wait]: unlock events(0x%x) idx(%d) data(%p)\n", *events, idx, *n_data);

	return 0;
}

int32_t event_post(int32_t handle, int32_t events, void * n_data)
{
	PEVENT_FD_S fd = (PEVENT_FD_S)handle;
	int32_t pst_tm_cur[3] = { 0 };
	n_timeval_t  func_start, func_stop;
	int32_t usec_time = 0, idx = 0, i, tmp_events ;

	
	if (event_param.post_cost_threshold)
	{
		//runtime_get_msec(&pst_tm_cur[0], &pst_tm_cur[1], &pst_tm_cur[2]);
	}
	//printf("\n[event_post]: prelock  fd->event(0x%x) events(0x%x) mutex(%p)\n", fd->n_event, events, &fd->mutex);

	get_current_time(&func_start);
	pthread_mutex_lock(&fd->mutex);
	get_current_time(&func_stop);

	usec_time = ((func_stop.tv_sec - func_start.tv_sec) * 1000 * 1000) + (func_stop.tv_usec - func_start.tv_usec);
	//printf("\n[event_post]: locked usec_time(%d us)\n", usec_time);
	fd->n_event |= events;
	tmp_events = events;
	for (i = 0; i < 32; i++)
	{
		if ((tmp_events >> i) & 1)
		{
			break;
		}
		idx++;
	}
	fd->n_data[idx] = n_data;
	pthread_cond_signal(&fd->cond);
	pthread_mutex_unlock(&fd->mutex);

	get_current_time(&func_start);
	usec_time = ((func_start.tv_sec - func_stop.tv_sec) * 1000 * 1000) + (func_start.tv_usec - func_stop.tv_usec);
	//printf("[event_post]: unlock usec_time(%d us)\n", usec_time);
	//printf("[event_post]: leave event(0x%x) events(0x%x) idx(%d) data(%p)\n", fd->n_event, events, idx, n_data);

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
