/* */

#ifdef HAVE_CONFIG_H
# include <config.h>
#endif

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <unistd.h>
#endif

#include "stun_timer.h"

#include <stdlib.h> /* div() */

/*
 * Clock used throughout the STUN code.
 * STUN requires a monotonic 1kHz clock to operate properly.
 */
static void stun_gettime(struct timeval * now)
{
#ifdef _WIN32
    FILETIME ft;
    unsigned long long * time64 = (unsigned long long *) &ft;

    GetSystemTimeAsFileTime(&ft);

    /* Convert from 100s of nanoseconds since 1601-01-01
     * to Unix epoch. Yes, this is Y2038 unsafe.
     */
    *time64 -= (unsigned long long) 116444736000000000;
    *time64 /= 10;

    now->tv_sec = (long)(*time64 / 1000000);
    now->tv_usec = *time64 % 1000000;
#else
#if defined (_POSIX_MONOTONIC_CLOCK) && (_POSIX_MONOTONIC_CLOCK >= 0)
    struct timespec spec;
    if (!clock_gettime(CLOCK_MONOTONIC, &spec))
    {
        now->tv_sec = spec.tv_sec;
        now->tv_usec = spec.tv_nsec / 1000;
    }
    else
#endif
    {
        // fallback to wall clock
        gettimeofday(now, NULL);
    }
#endif
}


static void add_delay(struct timeval * ts, unsigned delay)
{
    /* Delay is in ms. */
    ts->tv_sec += delay / 1000;
    ts->tv_usec += (delay % 1000) * 1000;

    while (ts->tv_usec > 1000000)
    {
        ts->tv_usec -= 1000000;
        ts->tv_sec++;
    }
}

void stun_timer_start(StunTimer * timer, uint32_t initial_timeout, uint32_t max_retransmissions)
{
    stun_gettime(&timer->deadline);
    timer->retransmissions = 0;
    timer->delay = initial_timeout;
    timer->max_retransmissions = max_retransmissions;
    add_delay(&timer->deadline, timer->delay);
}

void stun_timer_start_reliable(StunTimer * timer, uint32_t initial_timeout)
{
    stun_timer_start(timer, initial_timeout, 0);
}

unsigned stun_timer_remainder(const StunTimer * timer)
{
    unsigned delay;
    struct timeval now;

    stun_gettime(&now);
    if (now.tv_sec > timer->deadline.tv_sec)
        return 0;

    delay = timer->deadline.tv_sec - now.tv_sec;
    if ((delay == 0) && (now.tv_usec >= timer->deadline.tv_usec))
        return 0;

    delay *= 1000;
    delay += ((signed)(timer->deadline.tv_usec - now.tv_usec)) / 1000;
    return delay;
}

StunTimerReturn stun_timer_refresh(StunTimer * timer)
{
    unsigned delay = stun_timer_remainder(timer);
    if (delay == 0)
    {
        if (timer->retransmissions >= timer->max_retransmissions)
            return STUN_TIMER_RET_TIMEOUT;

        add_delay(&timer->deadline, timer->delay *= 2);
        timer->retransmissions++;
        return STUN_TIMER_RET_RETRANSMIT;
    }

    return STUN_TIMER_RET_SUCCESS;
}
