#ifndef _TIMER_H_
#define _TIMER_H_

#define MAX_ID_LEN  32


typedef void(*notifycallback)(void * data);

typedef enum
{
    TIMER_ERROR_BLOCK = 32, /* 定时器堵塞 */
    TIMER_ERROR_OPEN, 
    TIMER_ERROR_FOPEN,  /* */
    TIMER_ERROR_FWRITE, 
    TIMER_ERROR_FSEEK, 
    TIMER_ERROR_FTELL, 
} TIMER_ERROR_E;

/* 以下宏定义为timer_ioctrl()的cmd类型 */
typedef enum
{
    TIMER_CMD_GETTMCOUNT = 1,    /* 获取当前启动的计数器个数,param(int32_t *),channel(此处无效) */
    TIMER_CMD_GETTMPASST,   /* 获取当前定时器已经经过的时间(ms),param(int32_t *),channel(此处无效) */
    TIMER_CMD_GETTMREACH,   /* 获取当前定时器离到达的时间(ms),param(int32_t *),channel(此处无效) */
} TIMER_CMD_E;

int32_t timer_open();

int32_t timer_create();

int32_t timer_init(int32_t handle, int32_t  once, uint32_t interval, notifycallback func, void * data, char identify[MAX_ID_LEN]);

int32_t timer_start(int32_t handle);

int32_t timer_stop(int32_t handle);

int32_t timer_modify(int32_t handle, uint32_t interval);

int32_t timer_destroy(int32_t handle);

int32_t timer_close(int32_t handle);

#endif /* _TIMER_H_ */
