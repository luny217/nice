/* GLIB - Library of useful routines for C programming */


#include <stdint.h>
#include "nlist.h"
#include "nqueue.h"

/**
* n_queue_new:
*
* Creates a new #n_queue_t.
*
* Returns: a newly allocated #n_queue_t
**/
n_queue_t * n_queue_new(void)
{
	return n_slice_new0(n_queue_t);
}

/**
* n_queue_free:
* @queue: a #n_queue_t
*
* Frees the memory allocated for the #n_queue_t. Only call this function
* if @queue was created with n_queue_new(). If queue elements contain
* dynamically-allocated memory, they should be freed first.
*
* If queue elements contain dynamically-allocated memory, you should
* either use n_queue_free_full() or free them manually first.
**/
void n_queue_free(n_queue_t *queue)
{

	n_dlist_free(queue->head);
	n_slice_free(n_queue_t, queue);
}

/**
* n_queue_free_full:
* @queue: a pointer to a #n_queue_t
* @free_func: the function to be called to free each element's data
*
* Convenience method, which frees all the memory used by a #n_queue_t,
* and calls the specified destroy function on every element's data.
*
*/
void n_queue_free_full(n_queue_t * queue, n_destroy_notify free_func)
{
	n_queue_foreach(queue, (n_func)free_func, NULL);
	n_queue_free(queue);
}

/**
* n_queue_init:
* @queue: an uninitialized #n_queue_t
*
* A statically-allocated #n_queue_t must be initialized with this function
* before it can be used. Alternatively you can initialize it with
* #G_QUEUE_INIT. It is not necessary to initialize queues created with
* n_queue_new().
*
*/
void n_queue_init(n_queue_t * queue)
{
	queue->head = queue->tail = NULL;
	queue->length = 0;
}

/**
* n_queue_clear:
* @queue: a #n_queue_t
*
* Removes all the elements in @queue. If queue elements contain
* dynamically-allocated memory, they should be freed first.
*
*/
void n_queue_clear(n_queue_t * queue)
{
	n_dlist_free(queue->head);
	n_queue_init(queue);
}

/**
* n_queue_is_empty:
* @queue: a #n_queue_t.
*
* Returns %TRUE if the queue is empty.
*
* Returns: %TRUE if the queue is empty
*/
int n_queue_is_empty(n_queue_t * queue)
{
	return queue->head == NULL;
}

/**
* n_queue_get_length:
* @queue: a #n_queue_t
*
* Returns the number of items in @queue.
*
* Returns: the number of items in @queue
*
*/
uint32_t n_queue_get_length(n_queue_t * queue)
{
	return queue->length;
}

/**
* n_queue_reverse:
* @queue: a #n_queue_t
*
* Reverses the order of the items in @queue.
*
*/
void n_queue_reverse(n_queue_t * queue)
{
	queue->tail = queue->head;
	queue->head = n_dlist_reverse(queue->head);
}

/**
* n_queue_copy:
* @queue: a #n_queue_t
*
* Copies a @queue. Note that is a shallow copy. If the elements in the
* queue consist of pointers to data, the pointers are copied, but the
* actual data is not.
*
* Returns: a copy of @queue
*/
n_queue_t * n_queue_copy(n_queue_t * queue)
{
	n_queue_t * result;
	n_dlist_t * list;

	result = n_queue_new();

	for (list = queue->head; list != NULL; list = list->next)
		n_queue_push_tail(result, list->data);

	return result;
}

/**
* n_queue_foreach:
* @queue: a #n_queue_t
* @func: the function to call for each element's data
* @user_data: user data to pass to @func
*
* Calls @func for each element in the queue passing @user_data to the
* function.
*
*/
void n_queue_foreach(n_queue_t * queue, n_func func, void *  user_data)
{
	n_dlist_t *list;

	list = queue->head;
	while (list)
	{
		n_dlist_t *next = list->next;
		func(list->data, user_data);
		list = next;
	}
}

/**
* n_queue_find:
* @queue: a #n_queue_t
* @data: data to find
*
* Finds the first link in @queue which contains @data.
*
* Returns: the first link in @queue which contains @data
*
*/
n_dlist_t * n_queue_find(n_queue_t * queue, const void * data)
{
	return n_dlist_find(queue->head, data);
}

/**
* n_queue_find_custom:
* @queue: a #n_queue_t
* @data: user data passed to @func
* @func: a #n_compare_func to call for each element. It should return 0
*     when the desired element is found
*
* Finds an element in a #n_queue_t, using a supplied function to find the
* desired element. It iterates over the queue, calling the given function
* which should return 0 when the desired element is found. The function
* takes two const void * arguments, the #n_queue_t element's data as the
* first argument and the given user data as the second argument.
*
* Returns: the found link, or %NULL if it wasn't found
*
*/
n_dlist_t * n_queue_find_custom(n_queue_t * queue, const void *  data, n_compare_func func)
{
	return n_dlist_find_custom(queue->head, data, func);
}

/**
* n_queue_sort:
* @queue: a #n_queue_t
* @compare_func: the #n_compare_data_func used to sort @queue. This function
*     is passed two elements of the queue and should return 0 if they are
*     equal, a negative value if the first comes before the second, and
*     a positive value if the second comes before the first.
* @user_data: user data passed to @compare_func
*
* Sorts @queue using @compare_func.
*
*/
void n_queue_sort(n_queue_t * queue, n_compare_data_func compare_func, void * user_data)
{
	queue->head = n_dlist_sort_with_data(queue->head, compare_func, user_data);
	queue->tail = n_dlist_last(queue->head);
}

/**
* n_queue_push_head:
* @queue: a #n_queue_t.
* @data: the data for the new element.
*
* Adds a new element at the head of the queue.
*/
void n_queue_push_head(n_queue_t   *queue, void *  data)
{
	queue->head = n_dlist_prepend(queue->head, data);
	if (!queue->tail)
		queue->tail = queue->head;
	queue->length++;
}

/**
* n_queue_push_nth:
* @queue: a #n_queue_t
* @data: the data for the new element
* @n: the position to insert the new element. If @n is negative or
*     larger than the number of elements in the @queue, the element is
*     added to the end of the queue.
*
* Inserts a new element into @queue at the given position.
*
*/
void n_queue_push_nth(n_queue_t * queue, void *  data, uint32_t  n)
{

	if (n >= queue->length)
	{
		n_queue_push_tail(queue, data);
		return;
	}

	n_queue_insert_before(queue, n_queue_peek_nth_link(queue, n), data);
}

/**
* n_queue_push_head_link:
* @queue: a #n_queue_t
* @link_: a single #n_dlist_t element, not a list with more than one element
*
* Adds a new element at the head of the queue.
*/
void n_queue_push_head_link(n_queue_t *queue, n_dlist_t  *link)
{
	link->next = queue->head;
	if (queue->head)
		queue->head->prev = link;
	else
		queue->tail = link;
	queue->head = link;
	queue->length++;
}

/**
* n_queue_push_tail:
* @queue: a #n_queue_t
* @data: the data for the new element
*
* Adds a new element at the tail of the queue.
*/
void n_queue_push_tail(n_queue_t * queue, void *  data)
{
	queue->tail = n_dlist_append(queue->tail, data);
	if (queue->tail->next)
		queue->tail = queue->tail->next;
	else
		queue->head = queue->tail;
	queue->length++;
}

/**
* n_queue_push_tail_link:
* @queue: a #n_queue_t
* @link_: a single #n_dlist_t element, not a list with more than one element
*
* Adds a new element at the tail of the queue.
*/
void n_queue_push_tail_link(n_queue_t *queue, n_dlist_t  *link)
{
	link->prev = queue->tail;
	if (queue->tail)
		queue->tail->next = link;
	else
		queue->head = link;
	queue->tail = link;
	queue->length++;
}

/**
* n_queue_push_nth_link:
* @queue: a #n_queue_t
* @n: the position to insert the link. If this is negative or larger than
*     the number of elements in @queue, the link is added to the end of
*     @queue.
* @link_: the link to add to @queue
*
* Inserts @link into @queue at the given position.
*
*/
void n_queue_push_nth_link(n_queue_t *queue, uint32_t n, n_dlist_t  * link_)
{
	n_dlist_t * next;
	n_dlist_t * prev;

	if (n >= queue->length)
	{
		n_queue_push_tail_link(queue, link_);
		return;
	}

	next = n_queue_peek_nth_link(queue, n);
	prev = next->prev;

	if (prev)
		prev->next = link_;
	next->prev = link_;

	link_->next = next;
	link_->prev = prev;

	if (queue->head->prev)
		queue->head = queue->head->prev;

	if (queue->tail->next)
		queue->tail = queue->tail->next;

	queue->length++;
}

/**
* n_queue_pop_head:
* @queue: a #n_queue_t
*
* Removes the first element of the queue and returns its data.
*
* Returns: the data of the first element in the queue, or %NULL
*     if the queue is empty
*/
void * n_queue_pop_head(n_queue_t *queue)
{
	if (queue->head)
	{
		n_dlist_t *node = queue->head;
		void * data = node->data;

		queue->head = node->next;
		if (queue->head)
			queue->head->prev = NULL;
		else
			queue->tail = NULL;
		n_dlist_free_1(node);
		queue->length--;

		return data;
	}
	return NULL;
}

/**
* n_queue_pop_head_link:
* @queue: a #n_queue_t
*
* Removes and returns the first element of the queue.
*
* Returns: the #n_dlist_t element at the head of the queue, or %NULL
*     if the queue is empty
*/
n_dlist_t * n_queue_pop_head_link(n_queue_t *queue)
{
	if (queue->head)
	{
		n_dlist_t *node = queue->head;

		queue->head = node->next;
		if (queue->head)
		{
			queue->head->prev = NULL;
			node->next = NULL;
		}
		else
			queue->tail = NULL;
		queue->length--;

		return node;
	}

	return NULL;
}

/**
* n_queue_peek_head_link:
* @queue: a #n_queue_t
*
* Returns the first link in @queue.
*
* Returns: the first link in @queue, or %NULL if @queue is empty
*
* Since: 2.4
*/
n_dlist_t * n_queue_peek_head_link(n_queue_t *queue)
{
	return queue->head;
}

/**
* n_queue_peek_tail_link:
* @queue: a #n_queue_t
*
* Returns the last link in @queue.
*
* Returns: the last link in @queue, or %NULL if @queue is empty
*
*/
n_dlist_t * n_queue_peek_tail_link(n_queue_t *queue)
{
	return queue->tail;
}

/**
* n_queue_pop_tail:
* @queue: a #n_queue_t
*
* Removes the last element of the queue and returns its data.
*
* Returns: the data of the last element in the queue, or %NULL
*     if the queue is empty
*/
void * n_queue_pop_tail(n_queue_t *queue)
{
	if (queue->tail)
	{
		n_dlist_t *node = queue->tail;
		void * data = node->data;

		queue->tail = node->prev;
		if (queue->tail)
			queue->tail->next = NULL;
		else
			queue->head = NULL;
		queue->length--;
		n_dlist_free_1(node);

		return data;
	}

	return NULL;
}

/**
* n_queue_pop_nth:
* @queue: a #n_queue_t
* @n: the position of the element
*
* Removes the @n'th element of @queue and returns its data.
*
* Returns: the element's data, or %NULL if @n is off the end of @queue
*
* Since: 2.4
*/
void * n_queue_pop_nth(n_queue_t * queue, uint32_t   n)
{
	n_dlist_t *nth_link;
	void * result;

	if (n >= queue->length)
		return NULL;

	nth_link = n_queue_peek_nth_link(queue, n);
	result = nth_link->data;

	n_queue_delete_link(queue, nth_link);

	return result;
}

/**
* n_queue_pop_tail_link:
* @queue: a #n_queue_t
*
* Removes and returns the last element of the queue.
*
* Returns: the #n_dlist_t element at the tail of the queue, or %NULL
*     if the queue is empty
*/
n_dlist_t * n_queue_pop_tail_link(n_queue_t *queue)
{
	if (queue->tail)
	{
		n_dlist_t *node = queue->tail;

		queue->tail = node->prev;
		if (queue->tail)
		{
			queue->tail->next = NULL;
			node->prev = NULL;
		}
		else
			queue->head = NULL;
		queue->length--;

		return node;
	}

	return NULL;
}

/**
* n_queue_pop_nth_link:
* @queue: a #n_queue_t
* @n: the link's position
*
* Removes and returns the link at the given position.
*
* Returns: the @n'th link, or %NULL if @n is off the end of @queue
*
* Since: 2.4
*/
n_dlist_t * n_queue_pop_nth_link(n_queue_t * queue, uint32_t   n)
{
	n_dlist_t *link;

	if (n >= queue->length)
		return NULL;

	link = n_queue_peek_nth_link(queue, n);
	n_queue_unlink(queue, link);

	return link;
}

/**
* n_queue_peek_nth_link:
* @queue: a #n_queue_t
* @n: the position of the link
*
* Returns the link at the given position
*
* Returns: the link at the @n'th position, or %NULL
*     if @n is off the end of the list
*
*/
n_dlist_t * n_queue_peek_nth_link(n_queue_t * queue, uint32_t n)
{
	n_dlist_t *link;
	uint32_t i;

	if (n >= queue->length)
		return NULL;

	if (n > queue->length / 2)
	{
		n = queue->length - n - 1;

		link = queue->tail;
		for (i = 0; i < n; ++i)
			link = link->prev;
	}
	else
	{
		link = queue->head;
		for (i = 0; i < n; ++i)
			link = link->next;
	}

	return link;
}

/**
* n_queue_link_index:
* @queue: a #n_queue_t
* @link_: a #n_dlist_t link
*
* Returns the position of @link_ in @queue.
*
* Returns: the position of @link_, or -1 if the link is
*     not part of @queue
*
*/
int32_t n_queue_link_index(n_queue_t *queue, n_dlist_t  *link_)
{
	return n_dlist_position(queue->head, link_);
}

/**
* n_queue_unlink:
* @queue: a #n_queue_t
* @link_: a #n_dlist_t link that must be part of @queue
*
* Unlinks @link_ so that it will no longer be part of @queue.
* The link is not freed.
*
* @link_ must be part of @queue.
*
*/
void n_queue_unlink(n_queue_t *queue, n_dlist_t  *link_)
{
	if (link_ == queue->tail)
		queue->tail = queue->tail->prev;

	queue->head = n_dlist_remove_link(queue->head, link_);
	queue->length--;
}

/**
* n_queue_delete_link:
* @queue: a #n_queue_t
* @link_: a #n_dlist_t link that must be part of @queue
*
* Removes @link_ from @queue and frees it.
*
* @link_ must be part of @queue.
*
* Since: 2.4
*/
void n_queue_delete_link(n_queue_t *queue, n_dlist_t  *link_)
{
	n_queue_unlink(queue, link_);
	n_dlist_free(link_);
}

/**
* n_queue_peek_head:
* @queue: a #n_queue_t
*
* Returns the first element of the queue.
*
* Returns: the data of the first element in the queue, or %NULL
*     if the queue is empty
*/
void * n_queue_peek_head(n_queue_t *queue)
{
	return queue->head ? queue->head->data : NULL;
}

/**
* n_queue_peek_tail:
* @queue: a #n_queue_t
*
* Returns the last element of the queue.
*
* Returns: the data of the last element in the queue, or %NULL
*     if the queue is empty
*/
void * n_queue_peek_tail(n_queue_t *queue)
{
	return queue->tail ? queue->tail->data : NULL;
}

/**
* n_queue_peek_nth:
* @queue: a #n_queue_t
* @n: the position of the element
*
* Returns the @n'th element of @queue.
*
* Returns: the data for the @n'th element of @queue,
*     or %NULL if @n is off the end of @queue
*
*/
void * n_queue_peek_nth(n_queue_t *queue, uint32_t   n)
{
	n_dlist_t *link;

	link = n_queue_peek_nth_link(queue, n);

	if (link)
		return link->data;

	return NULL;
}

/**
* n_queue_index:
* @queue: a #n_queue_t
* @data: the data to find
*
* Returns the position of the first element in @queue which contains @data.
*
* Returns: the position of the first element in @queue which
*     contains @data, or -1 if no element in @queue contains @data
*
*/
int32_t n_queue_index(n_queue_t * queue, const void * data)
{
	return n_dlist_index(queue->head, data);
}

/**
* n_queue_remove:
* @queue: a #n_queue_t
* @data: the data to remove
*
* Removes the first element in @queue that contains @data.
*
* Returns: %TRUE if @data was found and removed from @queue
*
*/
int n_queue_remove(n_queue_t * queue, const void * data)
{
	n_dlist_t *link;

	link = n_dlist_find(queue->head, data);

	if (link)
		n_queue_delete_link(queue, link);

	return (link != NULL);
}

/**
* n_queue_remove_all:
* @queue: a #n_queue_t
* @data: the data to remove
*
* Remove all elements whose data equals @data from @queue.
*
* Returns: the number of elements removed from @queue
*
*/
uint32_t n_queue_remove_all(n_queue_t * queue, const void *  data)
{
	n_dlist_t *list;
	uint32_t old_length;

	old_length = queue->length;

	list = queue->head;
	while (list)
	{
		n_dlist_t *next = list->next;

		if (list->data == data)
			n_queue_delete_link(queue, list);

		list = next;
	}

	return (old_length - queue->length);
}

/**
* n_queue_insert_before:
* @queue: a #n_queue_t
* @sibling: (nullable): a #n_dlist_t link that must be part of @queue, or %NULL to
*   push at the tail of the queue.
* @data: the data to insert
*
* Inserts @data into @queue before @sibling.
*
* @sibling must be part of @queue. Since GLib 2.44 a %NULL sibling pushes the
* data at the tail of the queue.
*
*/
void n_queue_insert_before(n_queue_t * queue, n_dlist_t * sibling, void * data)
{
	if (sibling == NULL)
	{
		/* We don't use n_dlist_insert_before() with a NULL sibling because it
		* would be a O(n) operation and we would need to update manually the tail
		* pointer.
		*/
		n_queue_push_tail(queue, data);
	}
	else
	{
		queue->head = n_dlist_insert_before(queue->head, sibling, data);
		queue->length++;
	}
}

/**
* n_queue_insert_after:
* @queue: a #n_queue_t
* @sibling: (nullable): a #n_dlist_t link that must be part of @queue, or %NULL to
*   push at the head of the queue.
* @data: the data to insert
*
* Inserts @data into @queue after @sibling.
*
* @sibling must be part of @queue. Since GLib 2.44 a %NULL sibling pushes the
* data at the head of the queue.
*
*/
void n_queue_insert_after(n_queue_t * queue, n_dlist_t * sibling, void * data)
{
	if (sibling == NULL)
		n_queue_push_head(queue, data);
	else
		n_queue_insert_before(queue, sibling->next, data);
}

/**
* n_queue_insert_sorted:
* @queue: a #n_queue_t
* @data: the data to insert
* @func: the #n_compare_data_func used to compare elements in the queue. It is
*     called with two elements of the @queue and @user_data. It should
*     return 0 if the elements are equal, a negative value if the first
*     element comes before the second, and a positive value if the second
*     element comes before the first.
* @user_data: user data passed to @func
*
* Inserts @data into @queue using @func to determine the new position.
*
*/
void n_queue_insert_sorted(n_queue_t * queue, void * data, n_compare_data_func  func, void * user_data)
{
	n_dlist_t *list;

	list = queue->head;
	while (list && func(list->data, data, user_data) < 0)
		list = list->next;

	n_queue_insert_before(queue, list, data);
}
