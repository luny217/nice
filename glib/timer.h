#ifndef _TIMER_H_
#define _TIMER_H_

#define MAX_ID_LEN  32


typedef void(*notifycallback)(void * data);

typedef enum
{
    TIMER_ERROR_BLOCK = 32, /* ��ʱ������ */
    TIMER_ERROR_OPEN, 
    TIMER_ERROR_FOPEN,  /* */
    TIMER_ERROR_FWRITE, 
    TIMER_ERROR_FSEEK, 
    TIMER_ERROR_FTELL, 
} TIMER_ERROR_E;

/* ���º궨��Ϊtimer_ioctrl()��cmd���� */
typedef enum
{
    TIMER_CMD_GETTMCOUNT = 1,    /* ��ȡ��ǰ�����ļ���������,param(int32_t *),channel(�˴���Ч) */
    TIMER_CMD_GETTMPASST,   /* ��ȡ��ǰ��ʱ���Ѿ�������ʱ��(ms),param(int32_t *),channel(�˴���Ч) */
    TIMER_CMD_GETTMREACH,   /* ��ȡ��ǰ��ʱ���뵽���ʱ��(ms),param(int32_t *),channel(�˴���Ч) */
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
