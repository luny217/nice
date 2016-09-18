
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

#define MAX_TIMER_NUM 64
#define TIMER_PTHREAD_NAME "timer"

#define TIMER_MIN_MSEC  10  /* ms */

typedef struct
{
    int32_t once, ticks, interval;
    notifycallback func;
    int32_t wparam, lparam;
    char identify[MAX_TIMER_IDENTIFY_LEN];
} TIMER_S, *PTIMER_S;
#define TIMER_S_ELN sizeof(TIMER_S)

typedef struct _NODE_S
{
    struct _NODE_S *next;
    TIMER_S timer;
} NODE_S, *PNODE_S;
#define NODE_S_LEN  sizeof(NODE_S)

typedef struct _LINK_S
{
    PNODE_S head, tail;
} LINK_S, *PLINK_S;
#define LINK_S_LEN  sizeof(LINK_S)

typedef struct
{
    int32_t flag;
	pthread_t tid, pid;
    int32_t healthy;
    int32_t maxch;
    int32_t running;
    int32_t count;
    NODE_S node[MAX_TIMER_NUM + 2];
    LINK_S link_used, link_free;
} TIMER_FD_S, *PTIMER_FD_S;
#define TIMER_FD_S_LEN  sizeof(TIMER_FD_S)

static TIMER_FD_S timer_fd = {0};

static PNODE_S _link_get(PLINK_S plink, PNODE_S pnode)
{
    PNODE_S ptmp = pnode->next;

    if (ptmp)
    {
        pnode->next = ptmp->next;

        if ((int32_t)ptmp == (int32_t)plink->tail)
        {
            plink->tail = pnode;
        }
    }

    return ptmp;
}

static PNODE_S _link_put(PLINK_S plink, PNODE_S pnode)
{
    pnode->next = NULL;
    plink->tail->next = pnode;
    plink->tail = pnode;

    return pnode;
}

static PLINK_S _link_open(PLINK_S plink, PNODE_S pnode, int32_t count)
{
    if (NULL == pnode)
    {
        return NULL;
    }

    plink->head = plink->tail = pnode;
    while (--count)
    {
        _link_put(plink, ++pnode);
    }
    plink->tail->next = NULL;

    return plink;
}

/******************************************************************************
 * ��������: ģ���߳�ִ����
 * �������: fd: ģ����
 * �������: ��
 * ����ֵ  : ��
 *****************************************************************************/

static void _timer_body(void *arg)
{
    PTIMER_FD_S fd = &timer_fd;
    PTIMER_S ptimer = NULL;
    PNODE_S pnode = NULL;
	n_timeval_t  tv_now, tv_last;
	int32_t step = TIMER_MIN_MSEC;
	int32_t interval = (TIMER_MIN_MSEC - TIMER_SUB_MSEC) * ONE_MSEC_PER_USEC;


	get_current_time(&tv_last);
    tv_last.tv_usec = ((tv_last.tv_usec + 5000) / 10000) * 10000; /* ʱ�侫��Ϊ10���� */

    fd->running = 1;
    while (fd->running)
    {
		sleep_us(interval);
        fd->healthy = 0;
        
		get_current_time(&tv_now);
        tv_now.tv_usec = ((tv_now.tv_usec + 5000) / 10000) * 10000; /* ʱ�侫��Ϊ10���� */
        step = ((tv_now.tv_sec - tv_last.tv_sec) * ONE_SEC_PER_MSEC) +
            ((tv_now.tv_usec - tv_last.tv_usec) / ONE_MSEC_PER_USEC); /* ��������ʱ���,��λΪ���� */

        /* ����ϵͳʱ���޸ĵ�����,������λ<0��>1s���жϷǷ� */
        step = (((step > ONE_SEC_PER_MSEC) | (step < 0)) ? TIMER_MIN_MSEC : step);
        tv_last = tv_now;

        pnode = fd->link_used.head;
        while (pnode->next)
        {
            ptimer = &pnode->next->timer;
            if (ptimer->ticks)
            {
                ptimer->ticks -= step;
            }
            if (ptimer->ticks <= 0)
            {
            
                ptimer->func(ptimer->wparam, ptimer->lparam);
                printf("identify[%s]\n"
                    "\tinterval(%d) func(0x%x) wparam(0x%x) lparam(0x%x)\n", 
                    ptimer->identify,  ptimer->interval, (int32_t)ptimer->func, ptimer->wparam, ptimer->lparam);

                if (ptimer->once)
                {
                    _link_put(&fd->link_free, _link_get(&fd->link_used, pnode));
                    if (NULL == fd->link_used.head->next)
                    {
                        fd->flag = fd->running = 0;
                        break;
                    }
                }
                else
                {
                    ptimer->ticks += ptimer->interval;
                }
            }
            if (pnode->next) /* ���������������ڵ� */
            {
                pnode = pnode->next;
            }
        }
    }
    printf("OK\n");
    pthread_exit(NULL);
}

/******************************************************************************
 * ��������: ��ģ��
 * �������: ��
 * �������: ��
 * ����ֵ  : >=0-�ɹ�,<0-�������
 *****************************************************************************/
static int32_t _timer_open(PTIMER_FD_S fd)
{

    memset(fd, 0x00, TIMER_FD_S_LEN);
    _link_open(&fd->link_used, fd->node, 1);
    _link_open(&fd->link_free, fd->node + 1, MAX_TIMER_NUM + 1);
    fd->maxch = MAX_TIMER_NUM;

    return (int32_t)fd;
}

/******************************************************************************
 * ��������: ��ģ��
 * �������: ��
 * �������: ��
 * ����ֵ  : >=0-�ɹ�,<0-�������
 *****************************************************************************/
static int32_t _timer_start(PTIMER_FD_S fd)
{
    int32_t rtval = 0;

    
    if (fd->running)
    {
        printf("thread is started\n");
        return -1;
    }
    else
    {
        if ((rtval = pthread_create(&fd->tid, 0, (void *)_timer_body, 0)) < 0)
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

    return 0;
}

/******************************************************************************
 * ��������: �ر�ģ��,�ͷ�������Դ
 * �������: handle: ģ����,�˴���Ч
 * �������: ��
 * ����ֵ  : 0-�ɹ�,<0-�������
 *****************************************************************************/
static int32_t _timer_close(int32_t handle)
{
    PTIMER_FD_S fd = &timer_fd;

    fd->flag = fd->running = 0;     /* �������б�ʶ,ʹ�߳���Ȼ��ֹ */
    pthread_join(fd->tid, NULL);    /* �ȴ��߳���ֹ */

    return 0;
}

/******************************************************************************
 * ��������: ����һ����ʱ��
 * �������: once: 0-ѭ����ʱ,1-һ��������,��onceΪ1ʱ,���н�����ʱ�����Զ��ر�
 *           interval: ��ʱ��ʱ����,��λ: ����
 *           func: ��ʱ���ص�����
 *           wparam, lparam: �ص���������
 *           compid: ģ��ID
 *           identify: ��ʶ��
 * �������: ��
 * ����ֵ  : >0-�ɹ�,��ʾ��ʱ�����;<0-�������
 *****************************************************************************/
int32_t timer_startext(int32_t once, int32_t interval, notifycallback func, 
    int32_t wparam, int32_t lparam, char identify[MAX_TIMER_IDENTIFY_LEN])
{
    PTIMER_FD_S fd = &timer_fd;
    PNODE_S pnode = NULL;

    if (NULL == func)
    {
        return -1;
    }
	
	_timer_open(fd);
	_timer_start(fd);

    if ((pnode = _link_get(&fd->link_free, fd->link_free.head)))
    {
        pnode->timer.once = once;
        pnode->timer.interval = pnode->timer.ticks = interval;
        pnode->timer.wparam = wparam;
        pnode->timer.lparam = lparam;
        pnode->timer.func = func;
        if (identify)
        {
            strcpy(pnode->timer.identify, identify);
        }
        else
        {
            memset(pnode->timer.identify, 0x00, MAX_TIMER_IDENTIFY_LEN);
        }
        _link_put(&fd->link_used, pnode);
        fd->count++;
        printf("once(%d) interval(%d) wparam(0x%x) lparam(0x%x)\n"
            "\tfunc(0x%x) identify[%s]\n", 
            once, interval, wparam, lparam, (int32_t)func, identify);
        
        return (int32_t)pnode;
    }

    return -1;
}

/******************************************************************************
 * ��������: ֹͣһ����ʱ��
 * �������: handle: ��ʱ�����
 * �������: ��
 * ����ֵ  : >0-�ɹ�,<0-�������
 *****************************************************************************/
int32_t timer_stop(int32_t handle)
{
    PTIMER_FD_S fd = &timer_fd;
    PNODE_S pnode = NULL;

    if ((handle < (int32_t)(fd->node + 2)) | (handle > (int32_t)(fd->node + MAX_TIMER_NUM + 1)))
    {
        return -1;
    }
    pnode = fd->link_used.head;
    while (pnode->next)
    {
        if ((int32_t)pnode->next == handle)
        {
            _link_put(&fd->link_free, _link_get(&fd->link_used, pnode));
            fd->count--;
            if (NULL == fd->link_used.head->next)
            {
                _timer_close((int32_t)fd);
            }
            return handle;
        }
        pnode = pnode->next;
    }

    return handle;
}

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
int32_t timer_ioctrl(int32_t handle, int32_t cmd, int32_t channel, int32_t *param, int32_t param_len)
{
    PTIMER_FD_S fd = &timer_fd;

    switch (cmd)
    {
    case TIMER_CMD_GETTMCOUNT:
        if ((NULL == param) || (param_len < (int32_t)sizeof(int32_t)))
        {
            return -1;
        }
        *param = fd->count;
        return fd->count;
    case TIMER_CMD_GETTMPASST:
        if ((handle < (int32_t)fd->node) | (handle > (int32_t)(fd->node + MAX_TIMER_NUM)))
        {
            return -1;
        }
        else
        {
            PNODE_S pnode = (PNODE_S)handle;

            *param = (pnode->timer.interval - pnode->timer.ticks) * TIMER_MIN_MSEC;
            return *param;
        }
        break;
    case TIMER_CMD_GETTMREACH:
        if ((handle < (int32_t)fd->node) | (handle > (int32_t)(fd->node + MAX_TIMER_NUM)))
        {
            return -1;
        }
        else
        {
            PNODE_S pnode = (PNODE_S)handle;

            *param = pnode->timer.ticks * TIMER_MIN_MSEC;
            return *param;
        }
        break;
    default:
        printf("unsupported cmd, 0x%x\n", cmd);
        return -1;
    }

    return 0;
}
