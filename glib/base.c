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

#include <signal.h>
#include <sys/types.h>
#include <time.h>
#include <stdint.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>

#ifdef _WIN32
#include <windows.h>
#else
#include <unistd.h>
#include <sys/time.h>
#endif

#include "base.h"
#include "nlist.h"

 /*
 * http://msdn.microsoft.com/en-us/library/ms684122(v=vs.85).aspx
 */
int32_t atomic_int_get(const volatile int32_t *atomic)
{
	MemoryBarrier();
	return *atomic;
}

void atomic_int_set(volatile int32_t *atomic, int32_t newval)
{
	*atomic = newval;
	MemoryBarrier();
}

void atomic_int_inc(volatile int32_t *atomic)
{
	InterlockedIncrement(atomic);
}

void get_current_time(n_timeval_t * result)
{
#ifndef _WIN32
	struct timeval r;

	g_return_if_fail(result != NULL);

	/*this is required on alpha, there the timeval structs are int's
	not longs and a cast only would fail horribly*/
	gettimeofday(&r, NULL);
	result->tv_sec = r.tv_sec;
	result->tv_usec = r.tv_usec;
#else
	FILETIME ft;
	uint64_t time64;

	GetSystemTimeAsFileTime(&ft);
	memmove(&time64, &ft, sizeof(FILETIME));

	/* Convert from 100s of nanoseconds since 1601-01-01
	* to Unix epoch. Yes, this is Y2038 unsafe.
	*/
	time64 -= 116444736000000000i64;
	time64 /= 10;

	result->tv_sec = (int32_t) (time64 / 1000000);
	result->tv_usec =(int32_t)(time64 % 1000000);
#endif
}

void time_val_add(n_timeval_t  * _time, int32_t microseconds)
{
	if (microseconds >= 0)
	{
		_time->tv_usec += microseconds % USEC_PER_SEC;
		_time->tv_sec += microseconds / USEC_PER_SEC;
		if (_time->tv_usec >= USEC_PER_SEC)
		{
			_time->tv_usec -= USEC_PER_SEC;
			_time->tv_sec++;
		}
	}
	else
	{
		microseconds *= -1;
		_time->tv_usec -= microseconds % USEC_PER_SEC;
		_time->tv_sec -= microseconds / USEC_PER_SEC;
		if (_time->tv_usec < 0)
		{
			_time->tv_usec += USEC_PER_SEC;
			_time->tv_sec--;
		}
	}
}

void sleep_us(uint32_t microseconds)
{
#ifdef _WIN32
	Sleep(microseconds / 1000);
#else
	struct timespec request, remaining;
	request.tv_sec = microseconds / USEC_PER_SEC;
	request.tv_nsec = 1000 * (microseconds % USEC_PER_SEC);
	while (nanosleep(&request, &remaining) == -1 && errno == EINTR)
		request = remaining;
#endif
}

void sleep_ms(uint32_t milliseconds)
{
	sleep_us(milliseconds * 1000);
}

#if defined _WIN32
static ULONGLONG(*_GetTickCount64) (void) = NULL;
static uint32_t _win32_tick_epoch = 0;

void clock_win32_init(void)
{
	HMODULE kernel32;

	_GetTickCount64 = NULL;
	kernel32 = GetModuleHandle(L"kernel32.dll");
	if (kernel32 != NULL)
		_GetTickCount64 = (void *)GetProcAddress(kernel32, "GetTickCount64");
	_win32_tick_epoch = ((uint32_t)GetTickCount()) >> 31;
}

int64_t get_monotonic_time(void)
{
	uint64_t ticks;
	uint32_t ticks32;

	/* There are four sources for the monotonic time on Windows:
	*
	* Three are based on a (1 msec accuracy, but only read periodically) clock chip:
	* - GetTickCount (GTC)
	*    32bit msec counter, updated each ~15msec, wraps in ~50 days
	* - GetTickCount64 (GTC64)
	*    Same as GetTickCount, but extended to 64bit, so no wrap
	*    Only available in Vista or later
	* - timeGetTime (TGT)
	*    similar to GetTickCount by default: 15msec, 50 day wrap.
	*    available in winmm.dll (thus known as the multimedia timers)
	*    However apps can raise the system timer clock frequency using timeBeginPeriod()
	*    increasing the accuracy up to 1 msec, at a cost in general system performance
	*    and battery use.
	*
	* One is based on high precision clocks:
	* - QueryPrecisionCounter (QPC)
	*    This has much higher accuracy, but is not guaranteed monotonic, and
	*    has lots of complications like clock jumps and different times on different
	*    CPUs. It also has lower long term accuracy (i.e. it will drift compared to
	*    the low precision clocks.
	*
	* Additionally, the precision available in the timer-based wakeup such as
	* MsgWaitForMultipleObjectsEx (which is what the mainloop is based on) is based
	* on the TGT resolution, so by default it is ~15msec, but can be increased by apps.
	*
	* The QPC timer has too many issues to be used as is. The only way it could be used
	* is to use it to interpolate the lower precision clocks. Firefox does something like
	* this:
	*   https://bugzilla.mozilla.org/show_bug.cgi?id=363258
	*
	* However this seems quite complicated, so we're not doing this right now.
	*
	* The approach we take instead is to use the TGT timer, extending it to 64bit
	* either by using the GTC64 value, or if that is not available, a process local
	* time epoch that we increment when we detect a timer wrap (assumes that we read
	* the time at least once every 50 days).
	*
	* This means that:
	*  - We have a globally consistent monotonic clock on Vista and later
	*  - We have a locally monotonic clock on XP
	*  - Apps that need higher precision in timeouts and clock reads can call
	*    timeBeginPeriod() to increase it as much as they want
	*/

	if (_GetTickCount64 != NULL)
	{
		uint32_t ticks_as_32bit;

		ticks = _GetTickCount64();
		ticks32 = timeGetTime();

		/* GTC64 and TGT are sampled at different times, however they
		* have the same base and source (msecs since system boot).
		* They can differ by as much as -16 to +16 msecs.
		* We can't just inject the low bits into the 64bit counter
		* as one of the counters can have wrapped in 32bit space and
		* the other not. Instead we calculate the signed difference
		* in 32bit space and apply that difference to the 64bit counter.
		*/
		ticks_as_32bit = (uint32_t)ticks;

		/* We could do some 2's complement hack, but we play it safe */
		if (ticks32 - ticks_as_32bit <= INT_MAX)
			ticks += ticks32 - ticks_as_32bit;
		else
			ticks -= ticks_as_32bit - ticks32;
	}
	else
	{
		uint32_t epoch;

		epoch = atomic_int_get(&_win32_tick_epoch);

		/* Must read ticks after the epoch. Then we're guaranteed
		* that the ticks value we read is higher or equal to any
		* previous ones that lead to the writing of the epoch.
		*/
		ticks32 = timeGetTime();

		/* We store the MSB of the current time as the LSB
		* of the epoch. Comparing these bits lets us detect when
		* the 32bit counter has wrapped so we can increase the
		* epoch.
		*
		* This will work as long as this function is called at
		* least once every ~24 days, which is half the wrap time
		* of a 32bit msec counter. I think this is pretty likely.
		*
		* Note that g_win32_tick_epoch is a process local state,
		* so the monotonic clock will not be the same between
		* processes.
		*/
		if ((ticks32 >> 31) != (epoch & 1))
		{
			epoch++;
			atomic_int_set(&_win32_tick_epoch, epoch);
		}

		ticks = (uint64_t)ticks32 | ((uint64_t)epoch) << 31;
	}

	return ticks * 1000;
}
#elif defined(HAVE_MACH_MACH_TIME_H) /* Mac OS */
int64_t get_monotonic_time(void)
{
	static mach_timebase_info_data_t timebase_info;

	if (timebase_info.denom == 0)
	{
		/* This is a fraction that we must use to scale
		* mach_absolute_time() by in order to reach nanoseconds.
		*
		* We've only ever observed this to be 1/1, but maybe it could be
		* 1000/1 if mach time is microseconds already, or 1/1000 if
		* picoseconds.  Try to deal nicely with that.
		*/
		mach_timebase_info(&timebase_info);

		/* We actually want microseconds... */
		if (timebase_info.numer % 1000 == 0)
			timebase_info.numer /= 1000;
		else
			timebase_info.denom *= 1000;

		/* We want to make the numer 1 to avoid having to multiply... */
		if (timebase_info.denom % timebase_info.numer == 0)
		{
			timebase_info.denom /= timebase_info.numer;
			timebase_info.numer = 1;
		}
		else
		{
			/* We could just multiply by timebase_info.numer below, but why
			* bother for a case that may never actually exist...
			*
			* Plus -- performing the multiplication would risk integer
			* overflow.  If we ever actually end up in this situation, we
			* should more carefully evaluate the correct course of action.
			*/
			mach_timebase_info(&timebase_info); /* Get a fresh copy for a better message */
			g_error("Got weird mach timebase info of %d/%d.  Please file a bug against GLib.",
				timebase_info.numer, timebase_info.denom);
		}
	}

	return mach_absolute_time() / timebase_info.denom;
}
#else
int64_t  get_monotonic_time(void)
{
	struct timespec ts;
	int32_t result;

	result = clock_gettime(CLOCK_MONOTONIC, &ts);

	return (((int64_t)ts.tv_sec) * 1000000) + (ts.tv_nsec / 1000);
}
#endif

/**
* g_strdup:
* @str: (nullable): the string to duplicate
*
* Duplicates a string. If @str is %NULL it returns %NULL.
* The returned string should be freed with g_free()
* when no longer needed.
*
* Returns: a newly-allocated copy of @str
*/
char * n_strdup(const char *str)
{
	char *new_str;
	int32_t length;

	if (str)
	{
		length = strlen(str) + 1;
		//new_str = g_new(char, length);
		new_str = malloc(length * sizeof(char));
		memcpy(new_str, str, length);
	}
	else
		new_str = NULL;

	return new_str;
}

/**
* g_strndup:
* @str: the string to duplicate
* @n: the maximum number of bytes to copy from @str
*
* Duplicates the first @n bytes of a string, returning a newly-allocated
* buffer @n + 1 bytes long which will always be nul-terminated. If @str
* is less than @n bytes long the buffer is padded with nuls. If @str is
* %NULL it returns %NULL. The returned value should be freed when no longer
* needed.
*
* To copy a number of characters from a UTF-8 encoded string,
* use g_utf8_strncpy() instead.
*
* Returns: a newly-allocated buffer containing the first @n bytes
*     of @str, nul-terminated
*/
char * n_strndup(const char *str, int32_t n)
{
	char *new_str;

	if (str)
	{
		//new_str = g_new(char, n + 1);
		new_str = malloc((n + 1) * sizeof(char));
		strncpy(new_str, str, n);
		new_str[n] = '\0';
	}
	else
		new_str = NULL;

	return new_str;
}

/**
* n_strsplit:
* @string: a string to split
* @delimiter: a string which specifies the places at which to split
*     the string. The delimiter is not included in any of the resulting
*     strings, unless @max_tokens is reached.
* @max_tokens: the maximum number of pieces to split @string into.
*     If this is less than 1, the string is split completely.
*
* Splits a string into a maximum of @max_tokens pieces, using the given
* @delimiter. If @max_tokens is reached, the remainder of @string is
* appended to the last token.
*
* As an example, the result of g_strsplit (":a:bc::d:", ":", -1) is a
* %NULL-terminated vector containing the six strings "", "a", "bc", "", "d"
* and "".
*
* As a special case, the result of splitting the empty string "" is an empty
* vector, not a vector containing a single string. The reason for this
* special case is that being able to represent a empty vector is typically
* more useful than consistent handling of empty elements. If you do need
* to represent empty elements, you'll need to check for the empty string
* before calling g_strsplit().
*
* Returns: a newly-allocated %NULL-terminated array of strings. Use
*    g_strfreev() to free it.
*/
char ** n_strsplit(const char * string, const char * delimiter, uint32_t  max_tokens)
{
	n_slist_t * string_list = NULL, *slist;
	char ** str_array, * s;
	uint32_t n = 0;
	const char *remainder;	

	if (max_tokens < 1)
		max_tokens = INT_MAX;

	remainder = string;
	s = strstr(remainder, delimiter);
	if (s)
	{
		uint32_t delimiter_len = strlen(delimiter);

		while (--max_tokens && s)
		{
			uint32_t len;

			len = s - remainder;
			string_list = n_slist_prepend(string_list, n_strndup(remainder, len));
			n++;
			remainder = s + delimiter_len;
			s = strstr(remainder, delimiter);
		}
	}
	if (*string)
	{
		n++;
		string_list = n_slist_prepend(string_list, n_strdup(remainder));
	}

	//str_array = g_new(char*, n + 1); 
	str_array = malloc((n + 1) * sizeof(char*));

	str_array[n--] = NULL;
	for (slist = string_list; slist; slist = slist->next)
		str_array[n--] = slist->data;

	n_slist_free(string_list);

	return str_array;
}

/**
* g_strsplit_set:
* @string: The string to be tokenized
* @delimiters: A nul-terminated string containing bytes that are used
*     to split the string.
* @max_tokens: The maximum number of tokens to split @string into.
*     If this is less than 1, the string is split completely
*
* Splits @string into a number of tokens not containing any of the characters
* in @delimiter. A token is the (possibly empty) longest string that does not
* contain any of the characters in @delimiters. If @max_tokens is reached, the
* remainder is appended to the last token.
*
* For example the result of g_strsplit_set ("abc:def/ghi", ":/", -1) is a
* %NULL-terminated vector containing the three strings "abc", "def",
* and "ghi".
*
* The result of g_strsplit_set (":def/ghi:", ":/", -1) is a %NULL-terminated
* vector containing the four strings "", "def", "ghi", and "".
*
* As a special case, the result of splitting the empty string "" is an empty
* vector, not a vector containing a single string. The reason for this
* special case is that being able to represent a empty vector is typically
* more useful than consistent handling of empty elements. If you do need
* to represent empty elements, you'll need to check for the empty string
* before calling g_strsplit_set().
*
* Note that this function works on bytes not characters, so it can't be used
* to delimit UTF-8 strings for anything but ASCII characters.
*
* Returns: a newly-allocated %NULL-terminated array of strings. Use
*    g_strfreev() to free it.
*
* Since: 2.4
**/
char ** n_strsplit_set(const char *string, const char *delimiters, int32_t max_tokens)
{
	int delim_table[256];
	n_slist_t *tokens, *list;
	int32_t n_tokens;
	const char *s;
	const char *current;
	char *token;
	char **result;

	if (max_tokens < 1)
		max_tokens = INT_MAX;

	if (*string == '\0')
	{
		//result = g_new(char *, 1);
		result = malloc(1 * sizeof(char*));
		result[0] = NULL;
		return result;
	}

	memset(delim_table, FALSE, sizeof(delim_table));
	for (s = delimiters; *s != '\0'; ++s)
		delim_table[*(uint8_t *)s] = TRUE;

	tokens = NULL;
	n_tokens = 0;

	s = current = string;
	while (*s != '\0')
	{
		if (delim_table[*(uint8_t *)s] && n_tokens + 1 < max_tokens)
		{
			token = n_strndup(current, s - current);
			tokens = n_slist_prepend(tokens, token);
			++n_tokens;

			current = s + 1;
		}

		++s;
	}

	token = n_strndup(current, s - current);
	tokens = n_slist_prepend(tokens, token);
	++n_tokens;

	//result = g_new(char *, n_tokens + 1);
	result = malloc((n_tokens + 1) * sizeof(char*));

	result[n_tokens] = NULL;
	for (list = tokens; list != NULL; list = list->next)
		result[--n_tokens] = list->data;

	n_slist_free(tokens);

	return result;
}

/**
* g_strfreev:
* @str_array: (nullable): a %NULL-terminated array of strings to free
*
* Frees a %NULL-terminated array of strings, as well as each
* string it contains.
*
* If @str_array is %NULL, this function simply returns.
*/
void n_strfreev(char **str_array)
{
	if (str_array)
	{
		int i;

		for (i = 0; str_array[i] != NULL; i++)
			free(str_array[i]);

		free(str_array);
	}
}

/**
* n_memdup:
* @mem: the memory to copy.
* @byte_size: the number of bytes to copy.
*
* Allocates @byte_size bytes of memory, and copies @byte_size bytes into it
* from @mem. If @mem is %NULL it returns %NULL.
*
* Returns: a pointer to the newly-allocated copy of the memory, or %NULL if @mem
*  is %NULL.
*/
void * n_memdup(const void * mem, uint32_t  byte_size)
{
	void * new_mem;

	if (mem)
	{
		new_mem = malloc(byte_size);
		memcpy(new_mem, mem, byte_size);
	}
	else
		new_mem = NULL;

	return new_mem;
}