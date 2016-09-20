/* This file is part of the Nice GLib ICE library. */
/*
 * Example using libnice to negotiate a UDP connection between two clients,
 * possibly on the same network or behind different NATs and/or stateful
 * firewalls.
 *
 * Build:
 *   gcc -o threaded-example threaded-example.c `pkg-config --cflags --libs nice`
 *
 * Run two clients, one controlling and one controlled:
 *   threaded-example 0 $(host -4 -t A stun.stunprotocol.org | awk '{ print $4 }')
 *   threaded-example 1 $(host -4 -t A stun.stunprotocol.org | awk '{ print $4 }')
 */
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <ctype.h>

#include <agent.h>
#include "agent-priv.h"
#include <gio/gnetworking.h>
#include "uv.h"
#include "event.h"
#include "pthread.h"
#include "timer.h"

static GMainLoop * gloop;
static char * stun_addr = "107.191.106.104";
static uint32_t stun_port = 3478;
static int controlling = 1;
static int exit_thread, candidate_gathering_done, negotiation_done;
static GMutex gather_mutex, negotiate_mutex;
static GCond gather_cond, negotiate_cond;
FILE  * wfile_fp;
pthread_t  loop_tid;

static const char * candidate_type_name[] = {"host", "srflx", "prflx", "relay"};

static const char * state_name[] = {"disconnected", "gathering", "connecting", "connected", "ready", "failed"};

static int print_local_data(n_agent_t * agent, uint32_t stream_id, uint32_t component_id);
static int parse_remote_data(n_agent_t * agent, uint32_t stream_id, uint32_t component_id, char * line);
static void cb_cand_gathering_done(n_agent_t * agent, uint32_t stream_id, void * data);
static void cb_new_selected_pair(n_agent_t * agent, uint32_t stream_id, uint32_t component_id, char * lfoundation, char * rfoundation, void * data);
static void cb_comp_state_changed(n_agent_t * agent, uint32_t stream_id,  uint32_t component_id, uint32_t state, void * data);
static void cb_nice_recv(n_agent_t * agent, uint32_t stream_id, uint32_t component_id, uint32_t len, char * buf, void * data);

//void* nice_thread(void * data);

void nice_event_loop(void * data)
{
	int32_t  ret = 0, i = 0, events = 0;
	n_agent_t  * agent = (n_agent_t  *)data;
	void * n_data;

	agent->n_event = event_open();

	if (agent->n_event == -1)
	{
		nice_debug("can't creat event fd");
	}

	while (1)
	{
		if ((ret = event_wait(agent->n_event, 0xFFFFFFFF, &events, &n_data)) < 0)
		{
			g_usleep(10 * 1000);
			continue;
		}
		
		if (events & N_EVENT_CAND_GATHERING_DONE)
		{
			uint32_t * id = (uint32_t *) n_data;

			nice_debug("[CB_CAND_GATHERING_DONE] events(0x%x)  stream_id(%d)\n", events, *id);

			cb_cand_gathering_done(agent, *id, NULL);
		}


		if (events & N_EVENT_NEW_SELECTED_PAIR)
		{
			ev_new_pair_t * ev_new_pair = (ev_new_pair_t *)n_data;

			cb_new_selected_pair(agent, ev_new_pair->stream_id, ev_new_pair->component_id, 
												ev_new_pair->lfoundation, ev_new_pair->rfoundation, NULL);
		}

		if (events & N_EVENT_COMP_STATE_CHANGED)
		{
			ev_state_changed_t * ev_state_changed = (ev_state_changed_t *)n_data;
			cb_comp_state_changed(agent, ev_state_changed->stream_id, ev_state_changed->comp_id, ev_state_changed->state, NULL);
			nice_debug("\n[CB_COMP_STATE_CHANGED] events(0x%x)  state(%d)\n", events, ev_state_changed->state);
		}

		if (events & N_EVENT_NEW_CAND)
		{
			ev_new_cand_t * ev_new_cand = (ev_new_cand_t *)n_data;
			nice_debug("\n[CB_NEW_CAND] events(0x%x)  foundation(%s)\n", events, ev_new_cand->foundation);
		}

		if (events & N_EVENT_NEW_CAND_FULL)
		{
			n_cand_t * cand = (n_cand_t *)n_data;
			nice_debug("\n[CB_NEW_CAND_FULL] events(0x%x)  foundation(%s)\n", events, cand->foundation);
		}

		n_free(n_data);
	}
}

//#define G_LOG_DOMAIN    ((char*) 0)
#if 1
uv_thread_cb nice_thread(void * data)
{
    n_agent_t * agent;
    n_cand_t * local, *remote;
    GIOChannel * io_stdin;
    uint32_t stream_id;
    char * line = NULL;
    int rval;
	FILE  * file_fp;
	char test_file[] = "test.dat";
	char snd_buf[2048];
	int ret = 0, numread, numsend;

#ifdef G_OS_WIN32
    io_stdin = g_io_channel_win32_new_fd(_fileno(stdin));
#else
    io_stdin = g_io_channel_unix_new(fileno(stdin));
#endif
    g_io_channel_set_flags(io_stdin, G_IO_FLAG_NONBLOCK, NULL);

    // Create the nice agent
    agent = n_agent_new(g_main_loop_get_context(gloop));
    if (agent == NULL)
        g_error("Failed to create agent");

    // Set the STUN settings and controlling mode
    if (stun_addr)
    {
        //g_object_set(agent, "stun-server", stun_addr, NULL);
        //g_object_set(agent, "stun-server-port", stun_port, NULL);
		agent->stun_server_ip = g_strdup(stun_addr);
		agent->stun_server_port = stun_port;
    }
    //g_object_set(agent, "controlling-mode", controlling, NULL);
	agent->controlling_mode = controlling;

	//ret = uv_thread_create(&loop_tid, (uv_thread_cb)nice_event_loop, (void*)agent);
	ret = pthread_create(&loop_tid, 0, (void *)nice_event_loop, (void*)agent);

    // Connect to the signals
    //g_signal_connect(agent, "candidate-gathering-done", G_CALLBACK(cb_cand_gathering_done), NULL);
    //g_signal_connect(agent, "new-selected-pair", G_CALLBACK(cb_new_selected_pair), NULL);
    //g_signal_connect(agent, "component-state-changed", G_CALLBACK(cb_comp_state_changed), NULL);

    // Create a new stream with one component
    stream_id = n_agent_add_stream(agent, 1);
    if (stream_id == 0)
		nice_debug("Failed to add stream");

	n_agent_set_port_range(agent, stream_id, 1, 1024, 4096);

    // Attach to the component to receive the data
    // Without this call, candidates cannot be gathered
    n_agent_attach_recv(agent, stream_id, 1, g_main_loop_get_context(gloop), cb_nice_recv, NULL);

    //n_agent_set_relay_info(agent, stream_id, 1, stun_addr, stun_port, "test", "test", RELAY_TYPE_TURN_UDP);

    // Start gathering local candidates
    if (!n_agent_gather_cands(agent, stream_id))
		nice_debug("failed to start candidate gathering");

    nice_debug("waiting for candidate-gathering-done signal...");

    g_mutex_lock(&gather_mutex);
    while (!exit_thread && !candidate_gathering_done)
        g_cond_wait(&gather_cond, &gather_mutex);
    g_mutex_unlock(&gather_mutex);
    if (exit_thread)
        goto end;

    // Candidate gathering is done. Send our local candidates on stdout
	nice_debug("copy this line to remote client:\n");
	nice_debug("\n  ");
    print_local_data(agent, stream_id, 1);
	nice_debug("\n");

    // Listen on stdin for the remote candidate list
	nice_debug("enter remote data (single line, no wrapping):\n");
	nice_debug("> ");
    fflush(stdout);
    while (!exit_thread)
    {
        GIOStatus s = g_io_channel_read_line(io_stdin, &line, NULL, NULL, NULL);
        if (s == G_IO_STATUS_NORMAL)
        {
            // Parse remote candidate list and set it on the agent
            rval = parse_remote_data(agent, stream_id, 1, line);
            if (rval == EXIT_SUCCESS)
            {
                n_free(line);
                break;
            }
            else
            {
                fprintf(stderr, "error: failed to parse remote data\n");
                printf("Enter remote data (single line, no wrapping):\n");
                printf("> ");
                fflush(stdout);
            }
            n_free(line);
        }
        else if (s == G_IO_STATUS_AGAIN)
        {
            g_usleep(100000);
        }
    }

    nice_debug("waiting for state ready or failed signal...");
    g_mutex_lock(&negotiate_mutex);
    while (!exit_thread && !negotiation_done)
        g_cond_wait(&negotiate_cond, &negotiate_mutex);
    g_mutex_unlock(&negotiate_mutex);
    if (exit_thread)
        goto end;

    // Get current selected candidate pair and print IP address used
    if (n_agent_get_selected_pair(agent, stream_id, 1, &local, &remote))
    {
        char ipaddr[INET6_ADDRSTRLEN];

        nice_address_to_string(&local->addr, ipaddr);
		nice_debug("negotiation complete: ([%s]:%d,", ipaddr, nice_address_get_port(&local->addr));
        nice_address_to_string(&remote->addr, ipaddr);
		nice_debug(" [%s]:%d)\n", ipaddr, nice_address_get_port(&remote->addr));
    }

    // Listen to stdin and send data written to it
	nice_debug("send lines to remote (Ctrl-D to quit):\n");
	nice_debug("> ");
    fflush(stdout);

    if ((file_fp = fopen(test_file, "rb")) == NULL)
    {
        printf("Open %s failed:%s\n", test_file, strerror(errno));
        goto end;
    }

	printf("> ");
	fflush(stdout);
	GIOStatus s = g_io_channel_read_line(io_stdin, &line, NULL, NULL, NULL);	

    while (!exit_thread)
    {	
		if (agent->controlling_mode)
		{
			numread = fread(snd_buf, 500, 1, file_fp);
			if (numread > 0)
			{
resend:				
				numsend = n_agent_send(agent, stream_id, 1, 500, snd_buf);
				if (numsend < 0)
				{
					//nice_debug("send err!");
					g_usleep(500);
					goto resend;
				}
				else
				{
					//nice_debug("send %d bytes", numsend);
				}
			}
			else
			{
				g_usleep(100000);
				goto end;
			}
		}
		else
		{
			GIOStatus s = g_io_channel_read_line(io_stdin, &line, NULL, NULL, NULL);
			if (s == G_IO_STATUS_NORMAL)
			{
				n_agent_send(agent, stream_id, 1, strlen(line), line);
				n_free(line);
				printf("> ");
				fflush(stdout);
			}
			else if (s == G_IO_STATUS_AGAIN)
			{
				g_usleep(100000);
			}
			else
			{
				// Ctrl-D was pressed.
				n_agent_send(agent, stream_id, 1, 1, "\0");
				break;
			}
		}
    }

end:
	fclose(file_fp);
    g_io_channel_unref(io_stdin);
    g_object_unref(agent);
    g_main_loop_quit(gloop);

    return NULL;
}
#else
void* nice_thread(void * data)
{
	n_agent_t * agent;
	n_cand_t * local, *remote;
	GIOChannel * io_stdin;
	uint32_t stream_id;
	char * line = NULL;
	int rval;
	FILE  * file_fp;
	char test_file[] = "test.dat";
	char snd_buf[2048];
	int ret = 0, numread, numsend;

#ifdef G_OS_WIN32
	io_stdin = g_io_channel_win32_new_fd(_fileno(stdin));
#else
	io_stdin = g_io_channel_unix_new(fileno(stdin));
#endif
	g_io_channel_set_flags(io_stdin, G_IO_FLAG_NONBLOCK, NULL);

	// Create the nice agent
	agent = n_agent_new(g_main_loop_get_context(gloop));
	if (agent == NULL)
		g_error("Failed to create agent");

	// Set the STUN settings and controlling mode
	if (stun_addr)
	{
		//g_object_set(agent, "stun-server", stun_addr, NULL);
		//g_object_set(agent, "stun-server-port", stun_port, NULL);
		agent->stun_server_ip = g_strdup(stun_addr);
		agent->stun_server_port = stun_port;
	}
	//g_object_set(agent, "controlling-mode", controlling, NULL);
	agent->controlling_mode = controlling;

	// Connect to the signals
	g_signal_connect(agent, "candidate-gathering-done", G_CALLBACK(cb_cand_gathering_done), NULL);
	g_signal_connect(agent, "new-selected-pair", G_CALLBACK(cb_new_selected_pair), NULL);
	g_signal_connect(agent, "component-state-changed", G_CALLBACK(cb_comp_state_changed), NULL);

	// Create a new stream with one component
	stream_id = n_agent_add_stream(agent, 1);
	if (stream_id == 0)
		g_error("Failed to add stream");

	n_agent_set_port_range(agent, stream_id, 1, 1024, 4096);

	// Attach to the component to receive the data
	// Without this call, candidates cannot be gathered
	n_agent_attach_recv(agent, stream_id, 1, g_main_loop_get_context(gloop), cb_nice_recv, NULL);

	//n_agent_set_relay_info(agent, stream_id, 1, stun_addr, stun_port, "test", "test", NICE_RELAY_TYPE_TURN_UDP);

	// Start gathering local candidates
	if (!n_agent_gather_cands(agent, stream_id))
		g_error("Failed to start candidate gathering");

	nice_debug("waiting for candidate-gathering-done signal...");

	g_mutex_lock(&gather_mutex);
	while (!exit_thread && !candidate_gathering_done)
		g_cond_wait(&gather_cond, &gather_mutex);
	g_mutex_unlock(&gather_mutex);
	if (exit_thread)
		goto end;

	// Candidate gathering is done. Send our local candidates on stdout
	printf("Copy this line to remote client:\n");
	printf("\n  ");
	print_local_data(agent, stream_id, 1);
	printf("\n");

	// Listen on stdin for the remote candidate list
	printf("Enter remote data (single line, no wrapping):\n");
	printf("> ");
	fflush(stdout);
	while (!exit_thread)
	{
		GIOStatus s = g_io_channel_read_line(io_stdin, &line, NULL, NULL, NULL);
		if (s == G_IO_STATUS_NORMAL)
		{
			// Parse remote candidate list and set it on the agent
			rval = parse_remote_data(agent, stream_id, 1, line);
			if (rval == EXIT_SUCCESS)
			{
				n_free(line);
				break;
			}
			else
			{
				fprintf(stderr, "ERROR: failed to parse remote data\n");
				printf("Enter remote data (single line, no wrapping):\n");
				printf("> ");
				fflush(stdout);
			}
			n_free(line);
		}
		else if (s == G_IO_STATUS_AGAIN)
		{
			g_usleep(100000);
		}
	}

	nice_debug("waiting for state READY or FAILED signal...");
	g_mutex_lock(&negotiate_mutex);
	while (!exit_thread && !negotiation_done)
		g_cond_wait(&negotiate_cond, &negotiate_mutex);
	g_mutex_unlock(&negotiate_mutex);
	if (exit_thread)
		goto end;

	// Get current selected candidate pair and print IP address used
	if (n_agent_get_selected_pair(agent, stream_id, 1, &local, &remote))
	{
		char ipaddr[INET6_ADDRSTRLEN];

		nice_address_to_string(&local->addr, ipaddr);
		printf("\nNegotiation complete: ([%s]:%d,", ipaddr, nice_address_get_port(&local->addr));
		nice_address_to_string(&remote->addr, ipaddr);
		printf(" [%s]:%d)\n", ipaddr, nice_address_get_port(&remote->addr));
	}

	// Listen to stdin and send data written to it
	printf("\nSend lines to remote (Ctrl-D to quit):\n");
	printf("> ");
	fflush(stdout);

	if ((file_fp = fopen(test_file, "rb")) == NULL)
	{
		printf("Open %s failed:%s\n", test_file, strerror(errno));
		goto end;
	}

	printf("> ");
	fflush(stdout);
	GIOStatus s = g_io_channel_read_line(io_stdin, &line, NULL, NULL, NULL);

	while (!exit_thread)
	{
		if (agent->controlling_mode)
		{
			numread = fread(snd_buf, 500, 1, file_fp);
			if (numread > 0)
			{
			resend:
				numsend = n_agent_send(agent, stream_id, 1, 500, snd_buf);
				if (numsend < 0)
				{
					//nice_debug("send err!");
					g_usleep(500);
					goto resend;
				}
				else
				{
					//nice_debug("send %d bytes", numsend);
				}
			}
			else
			{
				g_usleep(100000);
				goto end;
			}
		}
		else
		{
			GIOStatus s = g_io_channel_read_line(io_stdin, &line, NULL, NULL, NULL);
			if (s == G_IO_STATUS_NORMAL)
			{
				n_agent_send(agent, stream_id, 1, strlen(line), line);
				n_free(line);
				printf("> ");
				fflush(stdout);
			}
			else if (s == G_IO_STATUS_AGAIN)
			{
				g_usleep(100000);
			}
			else
			{
				// Ctrl-D was pressed.
				n_agent_send(agent, stream_id, 1, 1, "\0");
				break;
			}
		}
	}

end:
	fclose(file_fp);
	g_io_channel_unref(io_stdin);
	g_object_unref(agent);
	g_main_loop_quit(gloop);

	return NULL;
}

#endif

static void cb_cand_gathering_done(n_agent_t * agent, uint32_t stream_id, void * data)
{
    nice_debug("signal candidate gathering done\n");

    g_mutex_lock(&gather_mutex);
    candidate_gathering_done = TRUE;
    g_cond_signal(&gather_cond);
    g_mutex_unlock(&gather_mutex);
}

static void cb_comp_state_changed(n_agent_t * agent, uint32_t stream_id, uint32_t comp_id, uint32_t state, void * data)
{
    nice_debug("SIGNAL: state changed %d %d %s[%d]\n", stream_id, comp_id, state_name[state], state);

    if (state == COMP_STATE_READY)
    {
        g_mutex_lock(&negotiate_mutex);
        negotiation_done = TRUE;
        g_cond_signal(&negotiate_cond);
        g_mutex_unlock(&negotiate_mutex);
    }
    else if (state == COMP_STATE_FAILED)
    {
        g_main_loop_quit(gloop);
    }
}

static void cb_new_selected_pair(n_agent_t * agent, uint32_t stream_id,
                                 uint32_t comp_id, char * lfoundation,
                                 char * rfoundation, void * data)
{
    nice_debug("signal: selected pair %s %s", lfoundation, rfoundation);
}

static void cb_nice_recv(n_agent_t * agent, uint32_t stream_id, uint32_t component_id, uint32_t len, char * buf, void * data)
{
	int numwrite;

	if (agent->controlling_mode)
	{
		if (len == 1 && buf[0] == '\0')
			g_main_loop_quit(gloop);
		printf("%.*s", len, buf);
		fflush(stdout);
	}
	else
	{
		numwrite = fwrite(buf, len, 1, wfile_fp);
		if (numwrite != 1)
		{
			printf("Short write:%d writed, %d should (%s)\n", len, numwrite, strerror(errno));
		}
	}
}

static n_cand_t * parse_candidate(char * scand, uint32_t stream_id)
{
    n_cand_t * cand = NULL;
    n_cand_type_e ntype;
    char ** tokens = NULL;
    uint32_t i;

    tokens = g_strsplit(scand, ",", 5);
    for (i = 0; tokens[i]; i++);
    if (i != 5)
        goto end;

    for (i = 0; i < G_N_ELEMENTS(candidate_type_name); i++)
    {
        if (strcmp(tokens[4], candidate_type_name[i]) == 0)
        {
            ntype = i;
            break;
        }
    }
    if (i == G_N_ELEMENTS(candidate_type_name))
        goto end;

    cand = n_cand_new(ntype);
    cand->component_id = 1;
    cand->stream_id = stream_id;
    cand->transport = CAND_TRANS_UDP;
    strncpy(cand->foundation, tokens[0], CAND_MAX_FOUNDATION);
    cand->foundation[CAND_MAX_FOUNDATION - 1] = 0;
    cand->priority = atoi(tokens[1]);

    if (!nice_address_set_from_string(&cand->addr, tokens[2]))
    {
        g_message("failed to parse addr: %s", tokens[2]);
        n_cand_free(cand);
        cand = NULL;
        goto end;
    }

    nice_address_set_port(&cand->addr, atoi(tokens[3]));

end:
    g_strfreev(tokens);

    return cand;
}

static int print_local_data(n_agent_t * agent, uint32_t stream_id, uint32_t component_id)
{
    int result = EXIT_FAILURE;
    char * local_ufrag = NULL;
    char * local_password = NULL;
    char ipaddr[INET6_ADDRSTRLEN];
	n_slist_t * cand_lists = NULL, *item;

    if (!n_agent_get_local_credentials(agent, stream_id,  &local_ufrag, &local_password))
        goto end;

    cand_lists = n_agent_get_local_cands(agent, stream_id, component_id);
    if (cand_lists == NULL)
        goto end;

    printf("%s  %s", local_ufrag, local_password);

    for (item = cand_lists; item; item = item->next)
    {
        n_cand_t * c = (n_cand_t *)item->data;

        nice_address_to_string(&c->addr, ipaddr);

        // (foundation),(prio),(addr),(port),(type)
        printf(" %s,%u,%s,%u,%s",
               c->foundation,
               c->priority,
               ipaddr,
               nice_address_get_port(&c->addr),
               candidate_type_name[c->type]);
    }
    printf("\n");
    result = EXIT_SUCCESS;

end:
    if (local_ufrag)
        free(local_ufrag);
    if (local_password)
        free(local_password);
    if (cand_lists)
        n_slist_free_full(cand_lists, (GDestroyNotify)&n_cand_free);

    return result;
}


static int parse_remote_data(n_agent_t * agent, uint32_t stream_id, uint32_t component_id, char * line)
{
    n_slist_t  * remote_candidates = NULL;
    char ** line_argv = NULL;
    const char * ufrag = NULL;
    const char * passwd = NULL;
    int result = EXIT_FAILURE;
    int i;

    line_argv = g_strsplit_set(line, " \t\n", 0);
    for (i = 0; line_argv && line_argv[i]; i++)
    {
        if (strlen(line_argv[i]) == 0)
            continue;

        // first two args are remote ufrag and password
        if (!ufrag)
        {
            ufrag = line_argv[i];
        }
        else if (!passwd)
        {
            passwd = line_argv[i];
        }
        else
        {
            // Remaining args are serialized canidates (at least one is required)
            n_cand_t * c = parse_candidate(line_argv[i], stream_id);

            if (c == NULL)
            {
                g_message("failed to parse candidate: %s", line_argv[i]);
                goto end;
            }
            remote_candidates = n_slist_prepend(remote_candidates, c);
        }
    }
    if (ufrag == NULL || passwd == NULL || remote_candidates == NULL)
    {
        g_message("line must have at least ufrag, password, and one candidate");
        goto end;
    }

    if (!n_agent_set_remote_credentials(agent, stream_id, ufrag, passwd))
    {
        g_message("failed to set remote credentials");
        goto end;
    }

    // Note: this will trigger the start of negotiation.
    if (n_agent_set_remote_cands(agent, stream_id, component_id, remote_candidates) < 1)
    {
        g_message("failed to set remote candidates");
        goto end;
    }

    result = EXIT_SUCCESS;

end:
    if (line_argv != NULL)
        g_strfreev(line_argv);
    if (remote_candidates != NULL)
        n_slist_free_full(remote_candidates, (n_destroy_notify)&n_cand_free);

    return result;
}

#if 0
int main(int argc, char * argv[])
{
	char write_file[] = "wtest.dat";

	g_networking_init();

	g_log_set_handler(G_LOG_DOMAIN, G_LOG_LEVEL_DEBUG, g_log_default_handler, NULL);

	if ((wfile_fp = fopen(write_file, "wb+")) == NULL)
	{
		printf("Open %s failed:%s\n", write_file, strerror(errno));
		return -1;
	}

	uv_loop_t  * loop = uv_default_loop();
	uv_thread_t  tid;
	int ret = -1;

	ret = uv_thread_create(&tid, (uv_thread_cb)nice_thread, NULL);

	/* Start the event loop.  Control continues in do_bind(). */
	if (uv_run(loop, UV_RUN_DEFAULT))
	{
		abort();
	}

	uv_thread_join(&tid);

	uv_loop_delete(loop);

	return EXIT_SUCCESS;
}
#else
int main(int argc, char * argv[])
{
	//GThread * gexamplethread;
	char write_file[] = "wtest.dat";
	pthread_t tid;
	int ret;

	g_networking_init();

	g_log_set_handler(G_LOG_DOMAIN, G_LOG_LEVEL_DEBUG, g_log_default_handler, NULL);

	if ((wfile_fp = fopen(write_file, "wb+")) == NULL)
	{
		printf("Open %s failed:%s\n", write_file, strerror(errno));
		return -1;
	}

	ret = timer_open();

	ret = pthread_create(&tid, 0, (void *)nice_thread, NULL);


	gloop = g_main_loop_new(NULL, FALSE);

	// Run the main loop and the example thread
	exit_thread = FALSE;
	//gexamplethread = g_thread_new("example thread", &nice_thread, NULL);
	g_main_loop_run(gloop);
	exit_thread = TRUE;

	//g_thread_join(gexamplethread);
	g_main_loop_unref(gloop);

	pthread_join(tid, NULL);

	return EXIT_SUCCESS;
}
#endif