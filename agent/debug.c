/* This file is part of the Nice GLib ICE library. */

#include <config.h>
#include <string.h>
#include <stdio.h>
#include <stdarg.h>
#include "debug.h"
#include "stunagent.h"
#include "pseudotcp.h"
#include "agent-priv.h"

static int debug_enabled = 0;

#define NICE_DEBUG_STUN 1
#define NICE_DEBUG_NICE 2
#define NICE_DEBUG_PSEUDOTCP 4
#define NICE_DEBUG_PSEUDOTCP_VERBOSE 8

static const GDebugKey keys[] =
{
    { (gchar *)"stun",  NICE_DEBUG_STUN },
    { (gchar *)"nice",  NICE_DEBUG_NICE },
    { (gchar *)"pseudotcp",  NICE_DEBUG_PSEUDOTCP },
    { (gchar *)"pseudotcp-verbose",  NICE_DEBUG_PSEUDOTCP_VERBOSE },
    { NULL, 0},
};

static void stun_handler(const char * format, va_list ap) G_GNUC_PRINTF(1, 0);

static void stun_handler(const char * format, va_list ap)
{
    g_logv("libnice-stun", G_LOG_LEVEL_DEBUG, format, ap);
}

void nice_debug_init(void)
{
    static int debug_initialized = FALSE;
    const char * flags_string;
    const char * gflags_string;
    uint32_t flags = 0;

    if (!debug_initialized)
    {
        debug_initialized = TRUE;

        flags_string = g_getenv("NICE_DEBUG");
        gflags_string = g_getenv("G_MESSAGES_DEBUG");

        if (flags_string)
            flags = g_parse_debug_string(flags_string, keys,  4);
        if (gflags_string && strstr(gflags_string, "libnice-pseudotcp-verbose"))
            flags |= NICE_DEBUG_PSEUDOTCP_VERBOSE;

        stun_set_debug_handler(stun_handler);
        nice_debug_enable(TRUE);

        /* Set verbose before normal so that if we use 'all', then only
           normal debug is enabled, we'd need to set pseudotcp-verbose without the
           pseudotcp flag in order to actually enable verbose pseudotcp */
        
        pseudo_tcp_set_debug_level(PSEUDO_TCP_DEBUG_VERBOSE);
        
    }
}

#ifndef NDEBUG
gboolean nice_debug_is_enabled(void)
{
    return debug_enabled;
}
#else
/* Defined in agent-priv.h. */
#endif

void nice_debug_enable(gboolean with_stun)
{
    nice_debug_init();
    debug_enabled = 1;
    if (with_stun)
        stun_debug_enable();
}
void nice_debug_disable(gboolean with_stun)
{
    nice_debug_init();
    debug_enabled = 0;
    if (with_stun)
        stun_debug_disable();
}

static void default_handler(const char * format, va_list ap)
{
	vfprintf(stderr, format, ap);
	fprintf(stderr, "\n");
}

static StunDebugHandler handler = default_handler;

#ifndef NDEBUG
void nice_debug(const char * fmt, ...)
{
    va_list ap;
    if (debug_enabled)
    {
        va_start(ap, fmt);
        //g_logv(G_LOG_DOMAIN, G_LOG_LEVEL_MESSAGE, fmt, ap);
		handler(fmt, ap);
        va_end(ap);
    }
}
#else
/* Defined in agent-priv.h. */
#endif