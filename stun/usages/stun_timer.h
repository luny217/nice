/* This file is part of the Nice GLib ICE library */

#ifndef STUN_TIMER_RET_H
#define STUN_TIMER_RET_H

/**
 * SECTION:timer
 * @short_description: STUN timer Usage
 * @include: stun/usages/timer.h
 * @stability: Stable
 *
 * The STUN timer usage is a set of helpful utility functions that allows you
 * to easily track when a STUN message should be retransmitted or considered
 * as timed out.
 *
 *
 <example>
   <title>Simple example on how to use the timer usage</title>
   <programlisting>
   StunTimer timer;
   unsigned remainder;
   StunTimerReturn ret;

   // Build the message, etc..
   ...

   // Send the message and start the timer
   send(socket, request, sizeof(request));
   stun_timer_start(&timer, STUN_TIMER_TIMEOUT, STUN_TIMER_MAX_RETRANS);

   // Loop until we get the response
   for (;;) {
     remainder = stun_timer_remainder(&timer);

     // Poll the socket until data is received or the timer expires
     if (poll (&pollfd, 1, delay) <= 0) {
       // Time out and no response was received
       ret = stun_timer_refresh (&timer);
       if (ret == STUN_TIMER_RET_TIMEOUT) {
         // Transaction timed out
         break;
       } else if (ret == STUN_TIMER_RET_RETRANSMIT) {
         // A retransmission is necessary
         send(socket, request, sizeof(request));
         continue;
       } else if (ret == STUN_TIMER_RET_SUCCESS) {
         // The refresh succeeded and nothing has to be done, continue polling
         continue;
       }
     } else {
       // We received a response, read it
       recv(socket, response, sizeof(response));
       break;
     }
   }

   // Check if the transaction timed out or not
   if (ret == STUN_TIMER_RET_TIMEOUT) {
     // do whatever needs to be done in that case
   } else {
     // Parse the response
   }

   </programlisting>
 </example>
 */

#include <stdint.h>
#ifdef _WIN32
#include <winsock2.h>
#else
# include <sys/types.h>
# include <sys/time.h>
# include <time.h>
#endif


/**
 * StunTimer:
 *
 * An opaque structure representing a STUN transaction retransmission timer
 */
typedef struct stun_timer_s StunTimer;

struct stun_timer_s
{
    struct timeval deadline;
    unsigned delay;
    unsigned retransmissions;
    unsigned max_retransmissions;
};

/**
 * STUN_TIMER_TIMEOUT:
 *
 * The default intial timeout to use for the timer
 */
#define STUN_TIMER_TIMEOUT 600

/**
 * STUN_TIMER_MAX_RETRANS:
 *
 * The default maximum retransmissions allowed before a timer decides to timeout
 */
#define STUN_TIMER_MAX_RETRANS 3

/**
 * STUN_TIMER_RET_RELIABLE_TIMEOUT:
 *
 * The default intial timeout to use for a reliable timer
 */
#define STUN_TIMER_RELIABLE_TIMEOUT 7900

/**
 * StunTimerReturn:
 * @STUN_TIMER_RET_SUCCESS: The timer was refreshed successfully
 * and there is nothing to be done
 * @STUN_TIMER_RET_RETRANSMIT: The timer expired and the message
 * should be retransmitted now.
 * @STUN_TIMER_RET_TIMEOUT: The timer expired as well as all the
 * retransmissions, the transaction timed out
 *
 * Return value of stun_usage_timer_refresh() which provides you with status
 * information on the timer.
 */
typedef enum
{
    STUN_TIMER_RET_SUCCESS,
    STUN_TIMER_RET_RETRANSMIT,
    STUN_TIMER_RET_TIMEOUT
} StunTimerReturn;

/**
    * stun_timer_start:
    * @timer: The #StunTimer to start
    * @initial_timeout: The initial timeout to use before the first retransmission
    * @max_retransmissions: The maximum number of transmissions before the
    * #StunTimer times out
    *
    * Starts a STUN transaction retransmission timer.
    * This should be called as soon as you send the message for the first time on
    * a UDP socket.
    * The timeout before the next retransmission is set to @initial_timeout, then
    * each time a packet is retransmited, that timeout will be doubled, until the
    * @max_retransmissions retransmissions limit is reached.
    * <para>
    * To determine the total timeout value, one can use the following equation :
    <programlisting>
    total_timeout =  initial_timeout * (2^(max_retransmissions + 1) - 1);
    </programlisting>
    * </para>
    *
    * See also: #STUN_TIMER_TIMEOUT
    *
    * See also: #STUN_TIMER_MAX_RETRANS
    */
void stun_timer_start(StunTimer * timer, uint32_t initial_timeout, uint32_t max_retransmissions);

/**
    * stun_timer_start_reliable:
    * @timer: The #StunTimer to start
    * @initial_timeout: The initial timeout to use before the first retransmission
    *
    * Starts a STUN transaction retransmission timer for a reliable transport.
    * This should be called as soon as you send the message for the first time on
    * a TCP socket
    */
void stun_timer_start_reliable(StunTimer * timer, uint32_t initial_timeout);

/**
    * stun_timer_refresh:
    * @timer: The #StunTimer to refresh
    *
    * Updates a STUN transaction retransmission timer.
    * Returns: A #StunTimerReturn telling you what to do next
    */
StunTimerReturn stun_timer_refresh(StunTimer * timer);

/**
    * stun_timer_remainder:
    * @timer: The #StunTimer to query
    *
    * Query the timer on the time left before the next refresh should be done
    * Returns: The time remaining for the timer to expire in milliseconds
    */
unsigned stun_timer_remainder(const StunTimer * timer);

#endif /* !STUN_TIMER_RET_H */
