
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>

#ifdef _WIN32
#include <windows.h>
#define TIMER_SUB_MSEC  0  /* ms */
#else
#include <unistd.h>
#include <sys/time.h>
#include <sys/poll.h>
#define TIMER_SUB_MSEC  10  /* ms */
#endif

#include "pthread.h"
#include "base.h"
#include "timer.h"
#include "event.h"
#include "nlist.h"

#define MAX_TIMER_NUM 64
#define TIMER_PTHREAD_NAME "timer"

#define TIMER_MIN_MSEC  10  /* ms */

typedef struct
{
	int32_t once, ticks, interval, enable;
	int64_t mono_ticks;
	notifycallback func;
	void * data;
	char identify[MAX_ID_LEN];
} n_timer_t;

#define TIMER_S_ELN sizeof(n_timer_t)

typedef struct
{
	int32_t flag;
	pthread_t tid, pid;
	int32_t healthy;
	int32_t max_num;
	int32_t running;
	int32_t count;
	n_slist_t * timer_list;
} n_timer_fd_t;

#define TIMER_FD_S_LEN  sizeof(n_timer_fd_t)

static n_timer_fd_t timer_fd = { 0 };

static void _timer_loop(void *arg)
{
	n_timer_fd_t * fd = &timer_fd;
	n_timer_t * n_timer = NULL;
	n_slist_t * i;
	n_timeval_t  tv_now, tv_last;
	int32_t step = TIMER_MIN_MSEC;
	int32_t interval = (TIMER_MIN_MSEC - TIMER_SUB_MSEC) * ONE_MSEC_PER_USEC;

	get_current_time(&tv_last);
	tv_last.tv_usec = ((tv_last.tv_usec + 5000) / 10000) * 10000; /* 时间精度为10毫秒 */

	fd->running = 1;
	while (fd->running)
	{
		sleep_us(interval);
		fd->healthy = 0;

		get_current_time(&tv_now);
		tv_now.tv_usec = ((tv_now.tv_usec + 5000) / 10000) * 10000; /* 时间精度为10毫秒 */
		step = ((tv_now.tv_sec - tv_last.tv_sec) * ONE_SEC_PER_MSEC) +
			((tv_now.tv_usec - tv_last.tv_usec) / ONE_MSEC_PER_USEC); /* 计算两次时间差,单位为毫秒 */

																	  /* 考虑系统时间修改等因素,步进单位<0或>1s则判断非法 */
		step = (((step > ONE_SEC_PER_MSEC) | (step < 0)) ? TIMER_MIN_MSEC : step);
		tv_last = tv_now;

		for (i = fd->timer_list; i; i = i->next)
		{
			n_timer = (n_timer_t *) i->data;

			if (n_timer->mono_ticks && n_timer->enable)
			{
				if ((get_monotonic_time() - n_timer->mono_ticks <= 0) && n_timer->enable)
				{
					n_timer->func(n_timer->data);					
					n_timer->mono_ticks = 0;
				}
			}
			
			if (n_timer->ticks && n_timer->enable)
			{
				n_timer->ticks -= step;
			}
			if (n_timer->ticks <= 0 && n_timer->enable)
			{
				n_timer->func(n_timer->data);

				if (n_timer->once)
				{
					timer_stop((int32_t)n_timer);
				}
				else
				{
					n_timer->ticks += n_timer->interval;
				}
			}
		}
	}
	printf("_timer_loop exit\n");
	pthread_exit(NULL);
}

int32_t timer_open()
{
	n_timer_fd_t * fd = &timer_fd;
	int32_t rtval;

	memset(fd, 0x00, TIMER_FD_S_LEN);
	fd->max_num = MAX_TIMER_NUM;

	if (fd->running)
	{
		printf("timer thread is started\n");
		return -1;
	}
	else
	{
		if ((rtval = pthread_create(&fd->tid, 0, (void *)_timer_loop, 0)) < 0)
		{
			fd->running = 0;
			return -1;
		}
		else
		{
			while ((0 == fd->running) && (0 == fd->healthy))
			{
				sleep_ms(10);
			}
		}
	}
	return (int32_t)fd;
}

int32_t timer_create()
{
	n_timer_fd_t * fd = &timer_fd;
	n_timer_t * n_timer = NULL;

	if (fd->count > MAX_TIMER_NUM)
	{
		return -1;
	}

	n_timer = (n_timer_t *)n_slice_alloc(TIMER_S_ELN);
	if (n_timer != NULL)
	{
		return (int32_t)n_timer;
	}
	return -1;
}

int32_t timer_init(int32_t handle, int32_t  once, uint32_t interval, notifycallback func, void * data, char identify[MAX_ID_LEN])
{
	n_timer_fd_t * fd = &timer_fd;
	n_timer_t * n_timer = (n_timer_t *)handle;

	if (n_timer != NULL && func != NULL)
	{
		n_timer->once = once;
		n_timer->interval = n_timer->ticks = interval;
		n_timer->mono_ticks = 0;
		n_timer->data = data;
		n_timer->func = func;
		n_timer->enable = FALSE;
		if (identify)
		{
			strcpy(n_timer->identify, identify);
		}
		else
		{
			memset(n_timer->identify, 0x00, MAX_ID_LEN);
		}
		fd->timer_list = n_slist_append(fd->timer_list, n_timer);
		fd->count++;
		printf("identify[%s] once(%d) interval(%d) func(0x%x)\n", identify, once, interval,  (int32_t)func);
		return 0;
	}
	return -1;
}

int32_t timer_start(int32_t handle)
{
	n_timer_fd_t * fd = &timer_fd;
	n_timer_t * n_timer = (n_timer_t *)handle;

	if (n_timer != NULL && n_timer->enable == FALSE)
	{
		n_timer->enable = TRUE;
		n_timer->ticks = n_timer->interval;
		printf("[%s] timer is start\n", n_timer->identify);
		return 0;
	}

	return -1;
}

int32_t timer_stop(int32_t handle)
{
	n_timer_fd_t * fd = &timer_fd;
	n_timer_t * n_timer = (n_timer_t *)handle;

	if (n_timer != NULL && n_timer->enable == TRUE)
	{
		n_timer->enable = FALSE;
		n_timer->ticks = n_timer->interval;
		n_timer->mono_ticks = 0;
		printf("[%s] timer is stop \n", n_timer->identify);
		return 0;
	}
	return -1;
}

int32_t timer_modify(int32_t handle, uint32_t interval)
{
	n_timer_fd_t * fd = &timer_fd;
	n_timer_t * n_timer = (n_timer_t *)handle;

	if (n_timer != NULL && n_timer->enable == TRUE)
	{
		printf("[%s] timer is modify from  %d to %lu\n", n_timer->identify, n_timer->interval, interval);
		n_timer->ticks = n_timer->interval = interval;		
		return 0;
	}
	return -1;
}

int32_t timer_set_mono(int32_t handle, int64_t ticks)
{
	n_timer_fd_t * fd = &timer_fd;
	n_timer_t * n_timer = (n_timer_t *)handle;

	if (n_timer != NULL && n_timer->enable == TRUE)
	{
		//printf("[%s] timer set monotonic_time %lld\n", n_timer->identify, ticks);
		n_timer->mono_ticks = ticks;
		return 0;
	}
	return -1;
}

int32_t timer_destroy(int32_t handle)
{
	return 0;
}

int32_t timer_close(int32_t handle)
{
	n_timer_fd_t * fd = &timer_fd;
	fd->flag = fd->running = 0;     /* 设置运行标识,使线程自然中止 */
	pthread_join(fd->tid, NULL);    /* 等待线程中止 */
	return 0;
}