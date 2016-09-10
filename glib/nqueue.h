#ifndef __N_QUEUE_H__
#define __N_QUEUE_H__

#include <stdint.h>
#include "nlist.h"

typedef struct _queue_st  n_queue_t;

/**
* n_queue_t:
* @head: a pointer to the first element of the queue
* @tail: a pointer to the last element of the queue
* @length: the number of elements in the queue
*
* Contains the public fields of a
* [Queue][glib-Double-ended-Queues].
*/

struct _queue_st
{
	n_dlist_t * head;
	n_dlist_t * tail;
	uint32_t  length;
};

/**
* G_QUEUE_INIT:
*
* A statically-allocated #n_queue_t must be initialized with this
* macro before it can be used. This macro can be used to initialize
* a variable, but it cannot be assigned to a variable. In that case
* you have to use n_queue_init().
*
* |[
* n_queue_t my_queue = G_QUEUE_INIT;
* ]|
*
*/
#define N_QUEUE_INIT { NULL, NULL, 0 }

/* queues
*/
n_queue_t*  n_queue_new(void);
void n_queue_free(n_queue_t * queue);
void n_queue_free_full(n_queue_t * queue, n_destroy_notify free_func);
void n_queue_init(n_queue_t * queue);
void n_queue_clear(n_queue_t * queue);
int n_queue_is_empty(n_queue_t * queue);
uint32_t n_queue_get_length(n_queue_t * queue);
void n_queue_reverse(n_queue_t * queue);
n_queue_t * n_queue_copy(n_queue_t * queue);
void n_queue_foreach(n_queue_t * queue, n_func func, void * user_data);
n_dlist_t *  n_queue_find(n_queue_t * queue,	const void * data);
n_dlist_t *  n_queue_find_custom(n_queue_t * queue, const void * data, n_compare_func func);
void n_queue_sort(n_queue_t * queue, n_compare_data_func  compare_func, void * user_data);
void n_queue_push_head(n_queue_t * queue,	void * data);
void n_queue_push_tail(n_queue_t * queue, void * data);
void n_queue_push_nth(n_queue_t * queue, void * data, uint32_t n);
void * n_queue_pop_head(n_queue_t * queue);
void * n_queue_pop_tail(n_queue_t * queue);
void * n_queue_pop_nth(n_queue_t * queue, uint32_t n);
void * n_queue_peek_head(n_queue_t * queue);
void * n_queue_peek_tail(n_queue_t * queue);
void * n_queue_peek_nth(n_queue_t * queue, uint32_t n);
int32_t n_queue_index(n_queue_t * queue, const void * data);
int n_queue_remove(n_queue_t * queue, const void * data);
uint32_t n_queue_remove_all(n_queue_t * queue, const void * data);
void n_queue_insert_before(n_queue_t * queue,	n_dlist_t * sibling,	void * data);
void n_queue_insert_after(n_queue_t * queue, n_dlist_t * sibling, void * data);
void n_queue_insert_sorted(n_queue_t * queue, void * data, n_compare_data_func  func, void * user_data);

void n_queue_push_head_link(n_queue_t * queue, n_dlist_t * link_);
void n_queue_push_tail_link(n_queue_t * queue,	n_dlist_t * link_);
void n_queue_push_nth_link(n_queue_t * queue, uint32_t n, n_dlist_t * link_);
n_dlist_t * n_queue_pop_head_link(n_queue_t * queue);
n_dlist_t * n_queue_pop_tail_link(n_queue_t * queue);
n_dlist_t * n_queue_pop_nth_link(n_queue_t * queue, uint32_t n);
n_dlist_t * n_queue_peek_head_link(n_queue_t * queue);
n_dlist_t * n_queue_peek_tail_link(n_queue_t * queue);
n_dlist_t * n_queue_peek_nth_link(n_queue_t * queue, uint32_t n);
int32_t n_queue_link_index(n_queue_t * queue, n_dlist_t * link_);
void n_queue_unlink(n_queue_t * queue, n_dlist_t * link_);
void n_queue_delete_link(n_queue_t * queue, n_dlist_t * link_);

typedef struct _invector_st n_invector_t;

struct _invector_st
{
	void * buffer;
	uint32_t size;
};

typedef struct _outvector_st n_outvector_t;

struct _outvector_st 
{
	const void * buffer;
	uint32_t size;
};

#endif /* __G_QUEUE_H__ */
