/* GLIB - Library of useful routines for C programming*/
 

#ifndef __N_LIST_H__
#define __N_LIST_H__

#include <stdint.h>

void * n_slice_alloc(uint32_t block_size);
void * n_slice_alloc0(uint32_t block_size);
void * n_slice_copy(uint32_t block_size, const void * mem_block);
void n_slice_free1(uint32_t block_size, void * mem_block);
void n_slice_free_chain_with_offset(uint32_t block_size, void * mem_chain,	uint32_t next_offset);
void n_free(void * mem);

#define  n_slice_new(type)      ((type*)n_slice_alloc(sizeof (type)))
#define  n_slice_new0(type)     ((type*)n_slice_alloc0(sizeof (type)))

#define n_slice_free(type, mem) \
do	 { \
  if (1) n_slice_free1 (sizeof (type), (mem)); \
  else   (void) ((type*) 0 == (mem)); \
} while(0)

#define n_slice_free_chain(type, mem_chain, next) \
do { \
  if (1) n_slice_free_chain_with_offset (sizeof (type), 	(mem_chain), offsetof (type, next)); \
  else   (void) ((type*) 0 == (mem_chain)); \
} while(0)

#define _n_dlist_alloc()         n_slice_new (n_dlist_t)
#define _n_dlist_alloc0()        n_slice_new0 (n_dlist_t)
#define _n_dlist_free1(list)     n_slice_free (n_dlist_t, list)

/* Doubly linked lists */
typedef struct _dlist_st  n_dlist_t;

struct _dlist_st
{
  void * data;
  n_dlist_t * next;
  n_dlist_t * prev;
};

typedef void(* n_destroy_notify) (void * data);
typedef int32_t(*n_compare_func) (const void * a, const void * b);
typedef int32_t(*n_compare_data_func)(const void * a, const void * b, void * user_data);
typedef void * (*n_copy_func) (const void * src, const void * data);
typedef void(*n_func) (void * data, void * user_data);

n_dlist_t * n_dlist_alloc (void);
void n_dlist_free (n_dlist_t * list);

void n_dlist_free_1 (n_dlist_t * list);
#define  n_dlist_free1 n_dlist_free_1

void n_dlist_free_full (n_dlist_t * list, n_destroy_notify free_func);
n_dlist_t * n_dlist_append (n_dlist_t * list, void * data) ;
n_dlist_t * n_dlist_prepend (n_dlist_t *list, void * data) ;
n_dlist_t * n_dlist_insert (n_dlist_t * list, void * data, int32_t position);
n_dlist_t * n_dlist_insert_sorted (n_dlist_t * list, void * data, n_compare_func func);
n_dlist_t * n_dlist_insert_sorted_with_data (n_dlist_t * list, void * data, n_compare_func  func, void * user_data);
n_dlist_t * n_dlist_insert_before (n_dlist_t * list, n_dlist_t * sibling,	 void * data);
n_dlist_t * n_dlist_concat (n_dlist_t * list1, n_dlist_t * list2);
n_dlist_t * n_dlist_remove (n_dlist_t * list, const void * data);
n_dlist_t * n_dlist_remove_all (n_dlist_t * list, const void * data);
n_dlist_t * n_dlist_remove_link (n_dlist_t * list, n_dlist_t * llink);
n_dlist_t * n_dlist_delete_link (n_dlist_t * list,	 n_dlist_t * link_);
n_dlist_t * n_dlist_reverse (n_dlist_t * list);
n_dlist_t * n_dlist_copy (n_dlist_t * list);
n_dlist_t * n_dlist_copy_deep (n_dlist_t * list, n_copy_func func, void * user_data);
n_dlist_t * n_dlist_nth (n_dlist_t * list, uint32_t n);
n_dlist_t * n_dlist_nth_prev (n_dlist_t * list, uint32_t n);
n_dlist_t * n_dlist_find (n_dlist_t * list, const void * data);
n_dlist_t * n_dlist_find_custom (n_dlist_t * list, const void * data, n_compare_func  func);
int32_t n_dlist_position (n_dlist_t * list, n_dlist_t * llink);
int32_t n_dlist_index(n_dlist_t * list, const void * data);
n_dlist_t * n_dlist_last(n_dlist_t * list);
n_dlist_t * n_dlist_first(n_dlist_t * list);
uint32_t n_dlist_length(n_dlist_t * list);
void n_dlist_foreach(n_dlist_t * list, n_func func, void * user_data);
n_dlist_t * n_dlist_sort (n_dlist_t * list, n_compare_func compare_func);
n_dlist_t * n_dlist_sort_with_data (n_dlist_t * list, n_compare_func  compare_func, void * user_data) ;
void * n_dlist_nth_data (n_dlist_t * list, uint32_t n);

#define n_dlist_previous(list) ((list) ? (((n_dlist_t *)(list))->prev) : NULL)
#define n_dlist_next(list) ((list) ? (((n_dlist_t *)(list))->next) : NULL)

/* Singly linked lists */

#define _n_slist_alloc0() n_slice_new0 (n_slist_t)
#define _n_slist_alloc() n_slice_new (n_slist_t)
#define _n_slist_free1(slist) n_slice_free (n_slist_t, slist)

typedef struct _slist_st  n_slist_t;

struct _slist_st
{
	void * data;
	n_slist_t * next;
};

n_slist_t *  n_slist_alloc(void);
void n_slist_free(n_slist_t * list);
void n_slist_free_1(n_slist_t * list);
#define	 n_slist_free1 n_slist_free_1

void n_slist_free_full(n_slist_t * list, n_destroy_notify  free_func);
n_slist_t * n_slist_append(n_slist_t * list, void * data);
n_slist_t * n_slist_prepend(n_slist_t * list, void * data);
n_slist_t * n_slist_insert(n_slist_t * list, void * data, int32_t position);
n_slist_t * n_slist_insert_sorted(n_slist_t * list, void * data, n_compare_func func);
n_slist_t * n_slist_insert_sorted_with_data(n_slist_t * list,	void * data, n_compare_data_func  func, void * user_data);
n_slist_t * n_slist_insert_before(n_slist_t * slist, n_slist_t * sibling, void * data);
n_slist_t * n_slist_concat(n_slist_t * list1, n_slist_t * list2);
n_slist_t * n_slist_remove(n_slist_t * list,  const void * data);
n_slist_t * n_slist_remove_all(n_slist_t * list, const void * data);
n_slist_t * n_slist_remove_link(n_slist_t * list, n_slist_t * link_);
n_slist_t * n_slist_delete_link(n_slist_t * list, n_slist_t * link_);
n_slist_t * n_slist_reverse(n_slist_t * list);
n_slist_t * n_slist_copy(n_slist_t * list);
n_slist_t * n_slist_copy_deep(n_slist_t * list, n_copy_func  func, void * user_data);
n_slist_t * n_slist_nth(n_slist_t * list, uint32_t n);
n_slist_t * n_slist_find(n_slist_t * list, const void * data);
n_slist_t *  n_slist_find_custom(n_slist_t * list, const void * data, n_compare_func func);
int32_t n_slist_position(n_slist_t * list,	n_slist_t * llink);
int32_t  n_slist_index(n_slist_t * list, 	const void * data);
n_slist_t * n_slist_last(n_slist_t * list);
uint32_t n_slist_length(n_slist_t * list);
void  n_slist_foreach(n_slist_t * list, n_func func, void * user_data);
n_slist_t * n_slist_sort(n_slist_t * list, n_compare_func compare_func);
n_slist_t * n_slist_sort_with_data(n_slist_t * list, n_compare_data_func  compare_func, void * user_data);
void * n_slist_nth_data(n_slist_t * list, uint32_t n);
#define  n_slist_next(slist) ((slist) ? (((n_slist_t *)(slist))->next) : NULL)

#endif /* __G_LIST_H__ */
