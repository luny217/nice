#ifndef _TIMER_H_
#define _TIMER_H_

#define MAX_TIMER_IDENTIFY_LEN  32


typedef void(*notifycallback)(int32_t wparam, int32_t lparam);

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

/******************************************************************************
 * 函数介绍: 启动一个定时器
 * 输入参数: once: 0-循环定时,1-一次性运行,当once为1时,运行结束后定时器将自动关闭
 *           interval: 定时器时间间隔,单位: 毫秒
 *           func: 定时器回调函数
 *           wparam, lparam: 回调函数参数
 *           identify: 标识码
 * 输出参数: 无
 * 返回值  : >0-成功,表示定时器句柄;<0-错误代码
 *****************************************************************************/
int32_t timer_startext(int32_t once, int32_t interval, notifycallback func, int32_t wparam, int32_t lparam, char identify[]);

/******************************************************************************
 * 函数介绍: 停止一个定时器
 * 输入参数: handle: 定时器句柄
 * 输出参数: 无
 * 返回值  : >0-成功,<0-错误代码
 *****************************************************************************/
int32_t timer_stop(int32_t handle);

/******************************************************************************
 * 函数介绍: TIMER配置
 * 输入参数: handle: TIMER句柄
 *           cmd: 命令
 *           channel: 通道号,此处无效
 *           param: 输入参数
 *           param_len: param长度,特别对于GET命令时,输出参数应先判断缓冲区是否足够
 * 输出参数: param: 输出参数
 * 返回值  : >=0-成功,<0-错误代码
 *****************************************************************************/
int32_t timer_ioctrl(int32_t handle, int32_t cmd, int32_t channel, int32_t *param, int32_t param_len);


#endif /* _TIMER_H_ */
