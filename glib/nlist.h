/* GLIB - Library of useful routines for C programming*/
 

#ifndef __N_LIST_H__
#define __N_LIST_H__

//#include <glib/gmem.h>
//#include <glib/gnode.h>
#include <stdint.h>

typedef struct _list_st  n_list_t;

struct _list_st
{
  void * data;
  n_list_t * next;
  n_list_t * prev;
};

/* Doubly linked lists
 */

#define  n_slice_new(type)      ((type*)n_slice_alloc(sizeof (type)))
#define  n_slice_new0(type)     ((type*)n_slice_alloc0(sizeof (type)))

#define n_slice_free(type, mem) \
do {                                                  \
  if (1) n_slice_free1 (sizeof (type), (mem));	\
  else   (void) ((type*) 0 == (mem));	\
} while(0)

#define n_slice_free_chain(type, mem_chain, next)	\
do {                                                  \
  if (1) n_slice_free_chain_with_offset (sizeof (type),		\
                 (mem_chain), offsetof (type, next)); 	\
  else   (void) ((type*) 0 == (mem_chain));	\
} while(0)

#define _n_list_alloc()         n_slice_new (n_list_t)
#define _n_list_alloc0()        n_slice_new0 (n_list_t)
#define _n_list_free1(list)     n_slice_free (n_list_t, list)

typedef void(* n_destroy_notify) (void * data);

typedef int32_t(*n_compare_func) (const void * a, const void * b);
typedef int32_t(*n_compare_data_func)(const void * a, const void * b, void * user_data);
typedef void * (*n_copy_func) (const void *  src, const void * data);
typedef void(*n_func) (void * data, void * user_data);

n_list_t * n_list_alloc (void);
void n_list_free (n_list_t * list);

void n_list_free_1 (n_list_t * list);
#define  n_list_free1 n_list_free_1

void n_list_free_full (n_list_t * list, n_destroy_notify free_func);
n_list_t * n_list_append (n_list_t * list, void * data) ;
n_list_t * n_list_prepend (n_list_t *list, void * data) ;
n_list_t * n_list_insert (n_list_t * list, void * data, int32_t position);
n_list_t * n_list_insert_sorted (n_list_t * list, void * data, n_compare_func func);
n_list_t * n_list_insert_sorted_with_data (n_list_t * list, void * data, n_compare_func  func, void * user_data);
n_list_t * n_list_insert_before (n_list_t * list,	 n_list_t * sibling,	 void * data);
n_list_t * n_list_concat (n_list_t * list1, n_list_t * list2);
n_list_t * n_list_remove (n_list_t * list, const void * data);
n_list_t * n_list_remove_all (n_list_t * list, const void * data);
n_list_t * n_list_remove_link (n_list_t * list, n_list_t * llink);
n_list_t * n_list_delete_link (n_list_t * list,	 n_list_t * link_);
n_list_t * n_list_reverse (n_list_t * list);
n_list_t * n_list_copy (n_list_t * list);
n_list_t * n_list_copy_deep (n_list_t * list, n_copy_func func, void * user_data);
n_list_t * n_list_nth (n_list_t * list, uint32_t n);
n_list_t * n_list_nth_prev (n_list_t * list, uint32_t n);
n_list_t * n_list_find (n_list_t * list, const void * data);
n_list_t * n_list_find_custom (n_list_t * list, const void * data, n_compare_func  func);
int32_t n_list_position (n_list_t * list, n_list_t * llink);
int32_t n_list_index(n_list_t * list, const void * data);
n_list_t * n_list_last(n_list_t * list);
n_list_t * n_list_first(n_list_t * list);
uint32_t n_list_length(n_list_t * list);
void n_list_foreach(n_list_t * list, n_func func, void * user_data);
n_list_t * n_list_sort (n_list_t * list, n_compare_func compare_func);
n_list_t * n_list_sort_with_data (n_list_t * list, n_compare_func  compare_func, void * user_data) ;
void * n_list_nth_data (n_list_t * list, uint32_t n);

#define n_list_previous(list) ((list) ? (((n_list_t *)(list))->prev) : NULL)
#define n_list_next(list) ((list) ? (((n_list_t *)(list))->next) : NULL)

#endif /* __G_LIST_H__ */
