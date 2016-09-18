#ifndef _TIMER_H_
#define _TIMER_H_

#define MAX_TIMER_IDENTIFY_LEN  32


typedef void(*notifycallback)(int32_t wparam, int32_t lparam);

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

/******************************************************************************
 * ��������: ����һ����ʱ��
 * �������: once: 0-ѭ����ʱ,1-һ��������,��onceΪ1ʱ,���н�����ʱ�����Զ��ر�
 *           interval: ��ʱ��ʱ����,��λ: ����
 *           func: ��ʱ���ص�����
 *           wparam, lparam: �ص���������
 *           identify: ��ʶ��
 * �������: ��
 * ����ֵ  : >0-�ɹ�,��ʾ��ʱ�����;<0-�������
 *****************************************************************************/
int32_t timer_startext(int32_t once, int32_t interval, notifycallback func, int32_t wparam, int32_t lparam, char identify[]);

/******************************************************************************
 * ��������: ֹͣһ����ʱ��
 * �������: handle: ��ʱ�����
 * �������: ��
 * ����ֵ  : >0-�ɹ�,<0-�������
 *****************************************************************************/
int32_t timer_stop(int32_t handle);

/******************************************************************************
 * ��������: TIMER����
 * �������: handle: TIMER���
 *           cmd: ����
 *           channel: ͨ����,�˴���Ч
 *           param: �������
 *           param_len: param����,�ر����GET����ʱ,�������Ӧ���жϻ������Ƿ��㹻
 * �������: param: �������
 * ����ֵ  : >=0-�ɹ�,<0-�������
 *****************************************************************************/
int32_t timer_ioctrl(int32_t handle, int32_t cmd, int32_t channel, int32_t *param, int32_t param_len);


#endif /* _TIMER_H_ */
