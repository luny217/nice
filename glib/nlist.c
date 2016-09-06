/* GLIB - Library of useful routines for C programming*/

#include "config.h"

#include "nlist.h"
//#include "gslice.h"
//#include "gmessages.h"

/**
 * n_list_t:
 * @data: holds the element's data, which can be a pointer to any kind
 *        of data, or any integer value using the
 *        [Type Conversion Macros][glib-Type-Conversion-Macros]
 * @next: contains the link to the next element in the list
 * @prev: contains the link to the previous element in the list
 *
 * The #n_list_t struct is used for each element in a doubly-linked list.
 **/

#define _n_list_alloc()         n_slice_new (n_list_t)
#define _n_list_alloc0()        n_slice_new0 (n_list_t)
#define _n_list_free1(list)     n_slice_free (n_list_t, list)

/**
 * n_list_alloc:
 *
 * Allocates space for one #n_list_t element. It is called by
 * n_list_append(), n_list_prepend(), n_list_insert() and
 * n_list_insert_sorted() and so is rarely used on its own.
 *
 * Returns: a pointer to the newly-allocated #n_list_t element
 **/
n_list_t * n_list_alloc(void)
{
    return _n_list_alloc0();
}

/**
 * n_list_free:
 * @list: a #n_list_t
 *
 * Frees all of the memory used by a #n_list_t.
 * The freed elements are returned to the slice allocator.
 *
 * If list elements contain dynamically-allocated memory, you should
 * either use n_list_free_full() or free them manually first.
 */
void
n_list_free(n_list_t * list)
{
    n_slice_free_chain(n_list_t, list, next);
}

/**
 * n_list_free_1:
 * @list: a #n_list_t element
 *
 * Frees one #n_list_t element, but does not update links from the next and
 * previous elements in the list, so you should not call this function on an
 * element that is currently part of a list.
 *
 * It is usually used after n_list_remove_link().
 */
/**
 * n_list_free1:
 *
 * Another name for n_list_free_1().
 **/
void
n_list_free_1(n_list_t * list)
{
    _n_list_free1(list);
}

/**
 * n_list_free_full:
 * @list: a pointer to a #n_list_t
 * @free_func: the function to be called to free each element's data
 *
 * Convenience method, which frees all the memory used by a #n_list_t,
 * and calls @free_func on every element's data.
 *
 * Since: 2.28
 */
void
n_list_free_full(n_list_t     *     list,
                 GDestroyNotify  free_func)
{
    n_list_foreach(list, (n_func) free_func, NULL);
    n_list_free(list);
}

/**
 * n_list_append:
 * @list: a pointer to a #n_list_t
 * @data: the data for the new element
 *
 * Adds a new element on to the end of the list.
 *
 * Note that the return value is the new start of the list,
 * if @list was empty; make sure you store the new value.
 *
 * n_list_append() has to traverse the entire list to find the end,
 * which is inefficient when adding multiple elements. A common idiom
 * to avoid the inefficiency is to use n_list_prepend() and reverse
 * the list with n_list_reverse() when all elements have been added.
 *
 * |[<!-- language="C" -->
 * // Notice that these are initialized to the empty list.
 * n_list_t *strinn_list = NULL, *number_list = NULL;
 *
 * // This is a list of strings.
 * strinn_list = n_list_append (strinn_list, "first");
 * strinn_list = n_list_append (strinn_list, "second");
 *
 * // This is a list of integers.
 * number_list = n_list_append (number_list, GINT_TO_POINTER (27));
 * number_list = n_list_append (number_list, GINT_TO_POINTER (14));
 * ]|
 *
 * Returns: either @list or the new start of the #n_list_t if @list was %NULL
 */
n_list_t * n_list_append(n_list_t * list, void  * data)
{
    n_list_t * new_list;
    n_list_t * last;

    new_list = _n_list_alloc();
    new_list->data = data;
    new_list->next = NULL;

    if (list)
    {
        last = n_list_last(list);
        /* n_assert (last != NULL); */
        last->next = new_list;
        new_list->prev = last;

        return list;
    }
    else
    {
        new_list->prev = NULL;
        return new_list;
    }
}

/**
 * n_list_prepend:
 * @list: a pointer to a #n_list_t, this must point to the top of the list
 * @data: the data for the new element
 *
 * Prepends a new element on to the start of the list.
 *
 * Note that the return value is the new start of the list,
 * which will have changed, so make sure you store the new value.
 *
 * |[<!-- language="C" -->
 * // Notice that it is initialized to the empty list.
 * n_list_t *list = NULL;
 *
 * list = n_list_prepend (list, "last");
 * list = n_list_prepend (list, "first");
 * ]|
 *
 * Do not use this function to prepend a new element to a different
 * element than the start of the list. Use n_list_insert_before() instead.
 *
 * Returns: a pointer to the newly prepended element, which is the new
 *     start of the #n_list_t
 */
n_list_t *
n_list_prepend(n_list_t  *  list,
               void  * data)
{
    n_list_t * new_list;

    new_list = _n_list_alloc();
    new_list->data = data;
    new_list->next = list;

    if (list)
    {
        new_list->prev = list->prev;
        if (list->prev)
            list->prev->next = new_list;
        list->prev = new_list;
    }
    else
        new_list->prev = NULL;

    return new_list;
}

/**
 * n_list_insert:
 * @list: a pointer to a #n_list_t, this must point to the top of the list
 * @data: the data for the new element
 * @position: the position to insert the element. If this is
 *     negative, or is larger than the number of elements in the
 *     list, the new element is added on to the end of the list.
 *
 * Inserts a new element into the list at the given position.
 *
 * Returns: the (possibly changed) start of the #n_list_t
 */
n_list_t *
n_list_insert(n_list_t  *  list,
              void  * data,
              gint      position)
{
    n_list_t * new_list;
    n_list_t * tmp_list;

    if (position < 0)
        return n_list_append(list, data);
    else if (position == 0)
        return n_list_prepend(list, data);

    tmp_list = n_list_nth(list, position);
    if (!tmp_list)
        return n_list_append(list, data);

    new_list = _n_list_alloc();
    new_list->data = data;
    new_list->prev = tmp_list->prev;
    tmp_list->prev->next = new_list;
    new_list->next = tmp_list;
    tmp_list->prev = new_list;

    return list;
}

/**
 * n_list_insert_before:
 * @list: a pointer to a #n_list_t, this must point to the top of the list
 * @sibling: the list element before which the new element
 *     is inserted or %NULL to insert at the end of the list
 * @data: the data for the new element
 *
 * Inserts a new element into the list before the given position.
 *
 * Returns: the (possibly changed) start of the #n_list_t
 */
n_list_t *
n_list_insert_before(n_list_t  *  list,
                     n_list_t  *  sibling,
                     void  * data)
{
    if (!list)
    {
        list = n_list_alloc();
        list->data = data;
        n_return_val_if_fail(sibling == NULL, list);
        return list;
    }
    else if (sibling)
    {
        n_list_t * node;

        node = _n_list_alloc();
        node->data = data;
        node->prev = sibling->prev;
        node->next = sibling;
        sibling->prev = node;
        if (node->prev)
        {
            node->prev->next = node;
            return list;
        }
        else
        {
            n_return_val_if_fail(sibling == list, node);
            return node;
        }
    }
    else
    {
        n_list_t * last;

        last = list;
        while (last->next)
            last = last->next;

        last->next = _n_list_alloc();
        last->next->data = data;
        last->next->prev = last;
        last->next->next = NULL;

        return list;
    }
}

/**
 * n_list_concat:
 * @list1: a #n_list_t, this must point to the top of the list
 * @list2: the #n_list_t to add to the end of the first #n_list_t,
 *     this must point  to the top of the list
 *
 * Adds the second #n_list_t onto the end of the first #n_list_t.
 * Note that the elements of the second #n_list_t are not copied.
 * They are used directly.
 *
 * This function is for example used to move an element in the list.
 * The following example moves an element to the top of the list:
 * |[<!-- language="C" -->
 * list = n_list_remove_link (list, llink);
 * list = n_list_concat (llink, list);
 * ]|
 *
 * Returns: the start of the new #n_list_t, which equals @list1 if not %NULL
 */
n_list_t *
n_list_concat(n_list_t * list1,
              n_list_t * list2)
{
    n_list_t * tmp_list;

    if (list2)
    {
        tmp_list = n_list_last(list1);
        if (tmp_list)
            tmp_list->next = list2;
        else
            list1 = list2;
        list2->prev = tmp_list;
    }

    return list1;
}

static inline n_list_t *
_n_list_remove_link(n_list_t * list,
                    n_list_t * link)
{
    if (link == NULL)
        return list;

    if (link->prev)
    {
        if (link->prev->next == link)
            link->prev->next = link->next;
        else
            n_warning("corrupted double-linked list detected");
    }
    if (link->next)
    {
        if (link->next->prev == link)
            link->next->prev = link->prev;
        else
            n_warning("corrupted double-linked list detected");
    }

    if (link == list)
        list = list->next;

    link->next = NULL;
    link->prev = NULL;

    return list;
}

/**
 * n_list_remove:
 * @list: a #n_list_t, this must point to the top of the list
 * @data: the data of the element to remove
 *
 * Removes an element from a #n_list_t.
 * If two elements contain the same data, only the first is removed.
 * If none of the elements contain the data, the #n_list_t is unchanged.
 *
 * Returns: the (possibly changed) start of the #n_list_t
 */
n_list_t *
n_list_remove(n_list_t     *    list,
              const void *  data)
{
    n_list_t * tmp;

    tmp = list;
    while (tmp)
    {
        if (tmp->data != data)
            tmp = tmp->next;
        else
        {
            list = _n_list_remove_link(list, tmp);
            _n_list_free1(tmp);

            break;
        }
    }
    return list;
}

/**
 * n_list_remove_all:
 * @list: a #n_list_t, this must point to the top of the list
 * @data: data to remove
 *
 * Removes all list nodes with data equal to @data.
 * Returns the new head of the list. Contrast with
 * n_list_remove() which removes only the first node
 * matching the given data.
 *
 * Returns: the (possibly changed) start of the #n_list_t
 */
n_list_t * n_list_remove_all(n_list_t * list, const void * data)
{
    n_list_t * tmp = list;

    while (tmp)
    {
        if (tmp->data != data)
            tmp = tmp->next;
        else
        {
            n_list_t * next = tmp->next;

            if (tmp->prev)
                tmp->prev->next = next;
            else
                list = next;
            if (next)
                next->prev = tmp->prev;

            _n_list_free1(tmp);
            tmp = next;
        }
    }
    return list;
}

/**
 * n_list_remove_link:
 * @list: a #n_list_t, this must point to the top of the list
 * @llink: an element in the #n_list_t
 *
 * Removes an element from a #n_list_t, without freeing the element.
 * The removed element's prev and next links are set to %NULL, so
 * that it becomes a self-contained list with one element.
 *
 * This function is for example used to move an element in the list
 * (see the example for n_list_concat()) or to remove an element in
 * the list before freeing its data:
 * |[<!-- language="C" -->
 * list = n_list_remove_link (list, llink);
 * free_some_data_that_may_access_the_list_again (llink->data);
 * n_list_free (llink);
 * ]|
 *
 * Returns: the (possibly changed) start of the #n_list_t
 */
n_list_t * n_list_remove_link(n_list_t * list,  n_list_t * llink)
{
    return _n_list_remove_link(list, llink);
}

/**
 * n_list_delete_link:
 * @list: a #n_list_t, this must point to the top of the list
 * @link_: node to delete from @list
 *
 * Removes the node link_ from the list and frees it.
 * Compare this to n_list_remove_link() which removes the node
 * without freeing it.
 *
 * Returns: the (possibly changed) start of the #n_list_t
 */
n_list_t * n_list_delete_link(n_list_t * list, n_list_t * link_)
{
    list = _n_list_remove_link(list, link_);
    _n_list_free1(link_);

    return list;
}

/**
 * n_list_copy:
 * @list: a #n_list_t, this must point to the top of the list
 *
 * Copies a #n_list_t.
 *
 * Note that this is a "shallow" copy. If the list elements
 * consist of pointers to data, the pointers are copied but
 * the actual data is not. See n_list_copy_deep() if you need
 * to copy the data as well.
 *
 * Returns: the start of the new list that holds the same data as @list
 */
n_list_t * n_list_copy(n_list_t * list)
{
    return n_list_copy_deep(list, NULL, NULL);
}

/**
 * n_list_copy_deep:
 * @list: a #n_list_t, this must point to the top of the list
 * @func: a copy function used to copy every element in the list
 * @user_data: user data passed to the copy function @func, or %NULL
 *
 * Makes a full (deep) copy of a #n_list_t.
 *
 * In contrast with n_list_copy(), this function uses @func to make
 * a copy of each list element, in addition to copying the list
 * container itself.
 *
 * @func, as a #n_copy_func, takes two arguments, the data to be copied
 * and a @user_data pointer. It's safe to pass %NULL as user_data,
 * if the copy function takes only one argument.
 *
 * For instance, if @list holds a list of GObjects, you can do:
 * |[<!-- language="C" -->
 * another_list = n_list_copy_deep (list, (n_copy_func) n_object_ref, NULL);
 * ]|
 *
 * And, to entirely free the new list, you could do:
 * |[<!-- language="C" -->
 * n_list_free_full (another_list, n_object_unref);
 * ]|
 *
 * Returns: the start of the new list that holds a full copy of @list,
 *     use n_list_free_full() to free it
 *
 * Since: 2.34
 */
n_list_t * n_list_copy_deep(n_list_t * list, n_copy_func  func, void * user_data)
{
    n_list_t * new_list = NULL;

    if (list)
    {
        n_list_t * last;

        new_list = _n_list_alloc();
        if (func)
            new_list->data = func(list->data, user_data);
        else
            new_list->data = list->data;
        new_list->prev = NULL;
        last = new_list;
        list = list->next;
        while (list)
        {
            last->next = _n_list_alloc();
            last->next->prev = last;
            last = last->next;
            if (func)
                last->data = func(list->data, user_data);
            else
                last->data = list->data;
            list = list->next;
        }
        last->next = NULL;
    }

    return new_list;
}

/**
 * n_list_reverse:
 * @list: a #n_list_t, this must point to the top of the list
 *
 * Reverses a #n_list_t.
 * It simply switches the next and prev pointers of each element.
 *
 * Returns: the start of the reversed #n_list_t
 */
n_list_t * n_list_reverse(n_list_t * list)
{
    n_list_t * last;

    last = NULL;
    while (list)
    {
        last = list;
        list = last->next;
        last->next = last->prev;
        last->prev = list;
    }

    return last;
}

/**
 * n_list_nth:
 * @list: a #n_list_t, this must point to the top of the list
 * @n: the position of the element, counting from 0
 *
 * Gets the element at the given position in a #n_list_t.
 *
 * This iterates over the list until it reaches the @n-th position. If you
 * intend to iterate over every element, it is better to use a for-loop as
 * described in the #n_list_t introduction.
 *
 * Returns: the element, or %NULL if the position is off
 *     the end of the #n_list_t
 */
n_list_t * n_list_nth(n_list_t * list, guint  n)
{
    while ((n-- > 0) && list)
        list = list->next;

    return list;
}

/**
 * n_list_nth_prev:
 * @list: a #n_list_t
 * @n: the position of the element, counting from 0
 *
 * Gets the element @n places before @list.
 *
 * Returns: the element, or %NULL if the position is
 *     off the end of the #n_list_t
 */
n_list_t * n_list_nth_prev(n_list_t * list, guint n)
{
    while ((n-- > 0) && list)
        list = list->prev;

    return list;
}

/**
 * n_list_nth_data:
 * @list: a #n_list_t, this must point to the top of the list
 * @n: the position of the element
 *
 * Gets the data of the element at the given position.
 *
 * This iterates over the list until it reaches the @n-th position. If you
 * intend to iterate over every element, it is better to use a for-loop as
 * described in the #n_list_t introduction.
 *
 * Returns: the element's data, or %NULL if the position
 *     is off the end of the #n_list_t
 */
void * n_list_nth_data(n_list_t * list, guint  n)
{
    while ((n-- > 0) && list)
        list = list->next;

    return list ? list->data : NULL;
}

/**
 * n_list_find:
 * @list: a #n_list_t, this must point to the top of the list
 * @data: the element data to find
 *
 * Finds the element in a #n_list_t which contains the given data.
 *
 * Returns: the found #n_list_t element, or %NULL if it is not found
 */
n_list_t * n_list_find(n_list_t * list, const void * data)
{
    while (list)
    {
        if (list->data == data)
            break;
        list = list->next;
    }

    return list;
}

/**
 * n_list_find_custom:
 * @list: a #n_list_t, this must point to the top of the list
 * @data: user data passed to the function
 * @func: the function to call for each element.
 *     It should return 0 when the desired element is found
 *
 * Finds an element in a #n_list_t, using a supplied function to
 * find the desired element. It iterates over the list, calling
 * the given function which should return 0 when the desired
 * element is found. The function takes two #const void * arguments,
 * the #n_list_t element's data as the first argument and the
 * given user data.
 *
 * Returns: the found #n_list_t element, or %NULL if it is not found
 */
n_list_t * n_list_find_custom(n_list_t * list, const void * data, n_compare_func func)
{
    n_return_val_if_fail(func != NULL, list);

    while (list)
    {
        if (! func(list->data, data))
            return list;
        list = list->next;
    }

    return NULL;
}

/**
 * n_list_position:
 * @list: a #n_list_t, this must point to the top of the list
 * @llink: an element in the #n_list_t
 *
 * Gets the position of the given element
 * in the #n_list_t (starting from 0).
 *
 * Returns: the position of the element in the #n_list_t,
 *     or -1 if the element is not found
 */
gint n_list_position(n_list_t * list, n_list_t * llink)
{
    gint i;

    i = 0;
    while (list)
    {
        if (list == llink)
            return i;
        i++;
        list = list->next;
    }

    return -1;
}

/**
 * n_list_index:
 * @list: a #n_list_t, this must point to the top of the list
 * @data: the data to find
 *
 * Gets the position of the element containing
 * the given data (starting from 0).
 *
 * Returns: the index of the element containing the data,
 *     or -1 if the data is not found
 */
gint n_list_index(n_list_t * list, const void *  data)
{
    gint i;

    i = 0;
    while (list)
    {
        if (list->data == data)
            return i;
        i++;
        list = list->next;
    }

    return -1;
}

/**
 * n_list_last:
 * @list: any #n_list_t element
 *
 * Gets the last element in a #n_list_t.
 *
 * Returns: the last element in the #n_list_t,
 *     or %NULL if the #n_list_t has no elements
 */
n_list_t * n_list_last(n_list_t * list)
{
    if (list)
    {
        while (list->next)
            list = list->next;
    }

    return list;
}

/**
 * n_list_first:
 * @list: any #n_list_t element
 *
 * Gets the first element in a #n_list_t.
 *
 * Returns: the first element in the #n_list_t,
 *     or %NULL if the #n_list_t has no elements
 */
n_list_t * n_list_first(n_list_t * list)
{
    if (list)
    {
        while (list->prev)
            list = list->prev;
    }

    return list;
}

/**
 * n_list_length:
 * @list: a #n_list_t, this must point to the top of the list
 *
 * Gets the number of elements in a #n_list_t.
 *
 * This function iterates over the whole list to count its elements.
 * Use a #GQueue instead of a n_list_t if you regularly need the number
 * of items. To check whether the list is non-empty, it is faster to check
 * @list against %NULL.
 *
 * Returns: the number of elements in the #n_list_t
 */
guint n_list_length(n_list_t * list)
{
    guint length;

    length = 0;
    while (list)
    {
        length++;
        list = list->next;
    }

    return length;
}

/**
 * n_list_foreach:
 * @list: a #n_list_t, this must point to the top of the list
 * @func: the function to call with each element's data
 * @user_data: user data to pass to the function
 *
 * Calls a function for each element of a #n_list_t.
 */
/**
 * n_func:
 * @data: the element's data
 * @user_data: user data passed to n_list_foreach() or n_slist_foreach()
 *
 * Specifies the type of functions passed to n_list_foreach() and
 * n_slist_foreach().
 */
void n_list_foreach(n_list_t * list, n_func func, void * user_data)
{
    while (list)
    {
        n_list_t * next = list->next;
        (*func)(list->data, user_data);
        list = next;
    }
}

static n_list_t * n_list_insert_sorted_real(n_list_t * list, void * data, n_func func, void  * user_data)
{
    n_list_t * tmp_list = list;
    n_list_t * new_list;
    gint cmp;

    n_return_val_if_fail(func != NULL, list);

    if (!list)
    {
        new_list = _n_list_alloc0();
        new_list->data = data;
        return new_list;
    }

    cmp = ((n_compare_func) func)(data, tmp_list->data, user_data);

    while ((tmp_list->next) && (cmp > 0))
    {
        tmp_list = tmp_list->next;

        cmp = ((n_compare_func) func)(data, tmp_list->data, user_data);
    }

    new_list = _n_list_alloc0();
    new_list->data = data;

    if ((!tmp_list->next) && (cmp > 0))
    {
        tmp_list->next = new_list;
        new_list->prev = tmp_list;
        return list;
    }

    if (tmp_list->prev)
    {
        tmp_list->prev->next = new_list;
        new_list->prev = tmp_list->prev;
    }
    new_list->next = tmp_list;
    tmp_list->prev = new_list;

    if (tmp_list == list)
        return new_list;
    else
        return list;
}

/**
 * n_list_insert_sorted:
 * @list: a pointer to a #n_list_t, this must point to the top of the
 *     already sorted list
 * @data: the data for the new element
 * @func: the function to compare elements in the list. It should
 *     return a number > 0 if the first parameter comes after the
 *     second parameter in the sort order.
 *
 * Inserts a new element into the list, using the given comparison
 * function to determine its position.
 *
 * If you are adding many new elements to a list, and the number of
 * new elements is much larger than the length of the list, use
 * n_list_prepend() to add the new items and sort the list afterwards
 * with n_list_sort().
 *
 * Returns: the (possibly changed) start of the #n_list_t
 */
n_list_t * n_list_insert_sorted(n_list_t * list, void * data, n_compare_func func)
{
    return n_list_insert_sorted_real(list, data, (n_func) func, NULL);
}

/**
 * n_list_insert_sorted_with_data:
 * @list: a pointer to a #n_list_t, this must point to the top of the
 *     already sorted list
 * @data: the data for the new element
 * @func: the function to compare elements in the list. It should
 *     return a number > 0 if the first parameter  comes after the
 *     second parameter in the sort order.
 * @user_data: user data to pass to comparison function
 *
 * Inserts a new element into the list, using the given comparison
 * function to determine its position.
 *
 * If you are adding many new elements to a list, and the number of
 * new elements is much larger than the length of the list, use
 * n_list_prepend() to add the new items and sort the list afterwards
 * with n_list_sort().
 *
 * Returns: the (possibly changed) start of the #n_list_t
 *
 * Since: 2.10
 */
n_list_t * n_list_insert_sorted_with_data(n_list_t * list, void * data, n_compare_func func, void * user_data)
{
    return n_list_insert_sorted_real(list, data, (n_func) func, user_data);
}

static n_list_t * n_list_sort_merge(n_list_t * l1, n_list_t * l2, n_func compare_func, void * user_data)
{
    n_list_t list, *l, *lprev;
    gint cmp;

    l = &list;
    lprev = NULL;

    while (l1 && l2)
    {
        cmp = ((n_compare_func) compare_func)(l1->data, l2->data, user_data);

        if (cmp <= 0)
        {
            l->next = l1;
            l1 = l1->next;
        }
        else
        {
            l->next = l2;
            l2 = l2->next;
        }
        l = l->next;
        l->prev = lprev;
        lprev = l;
    }
    l->next = l1 ? l1 : l2;
    l->next->prev = l;

    return list.next;
}

static n_list_t * n_list_sort_real(n_list_t * list, n_func compare_func, void * user_data)
{
    n_list_t * l1, *l2;

    if (!list)
        return NULL;
    if (!list->next)
        return list;

    l1 = list;
    l2 = list->next;

    while ((l2 = l2->next) != NULL)
    {
        if ((l2 = l2->next) == NULL)
            break;
        l1 = l1->next;
    }
    l2 = l1->next;
    l1->next = NULL;

    return n_list_sort_merge(n_list_sort_real(list, compare_func, user_data),
                             n_list_sort_real(l2, compare_func, user_data),
                             compare_func,
                             user_data);
}

/**
 * n_list_sort:
 * @list: a #n_list_t, this must point to the top of the list
 * @compare_func: the comparison function used to sort the #n_list_t.
 *     This function is passed the data from 2 elements of the #n_list_t
 *     and should return 0 if they are equal, a negative value if the
 *     first element comes before the second, or a positive value if
 *     the first element comes after the second.
 *
 * Sorts a #n_list_t using the given comparison function. The algorithm
 * used is a stable sort.
 *
 * Returns: the (possibly changed) start of the #n_list_t
 */
/**
 * n_compare_func:
 * @a: a value
 * @b: a value to compare with
 *
 * Specifies the type of a comparison function used to compare two
 * values.  The function should return a negative integer if the first
 * value comes before the second, 0 if they are equal, or a positive
 * integer if the first value comes after the second.
 *
 * Returns: negative value if @a < @b; zero if @a = @b; positive
 *          value if @a > @b
 */
n_list_t * n_list_sort(n_list_t * list, n_compare_func compare_func)
{
    return n_list_sort_real(list, (n_func)compare_func, NULL);
}

/**
 * n_list_sort_with_data:
 * @list: a #n_list_t, this must point to the top of the list
 * @compare_func: comparison function
 * @user_data: user data to pass to comparison function
 *
 * Like n_list_sort(), but the comparison function accepts
 * a user data argument.
 *
 * Returns: the (possibly changed) start of the #n_list_t
 */
/**
 * n_compare_func:
 * @a: a value
 * @b: a value to compare with
 * @user_data: user data
 *
 * Specifies the type of a comparison function used to compare two
 * values.  The function should return a negative integer if the first
 * value comes before the second, 0 if they are equal, or a positive
 * integer if the first value comes after the second.
 *
 * Returns: negative value if @a < @b; zero if @a = @b; positive
 *          value if @a > @b
 */
n_list_t * n_list_sort_with_data(n_list_t * list, n_compare_func compare_func, void * user_data)
{
    return n_list_sort_real(list, (n_func)compare_func, user_data);
}
