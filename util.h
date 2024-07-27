/*
	util.h ~ RL

	Miscellaneous tools and data structures
*/

#pragma once

#include <assert.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdint.h>

typedef void (*panic_callback_t)(void);
extern panic_callback_t panic_callback;

extern inline void* journal_malloc(size_t sz)
{
	void* res = malloc(sz);
	if (!res)
	{
		if (panic_callback)
			panic_callback();
		exit(1);
	}
	return res;
}

#define STARTING_RESERVE		(64)
#define ELEMENT_NOT_FOUND		(-1)

#define LIST_ASSERT_TYPEOF(list, v)		(assert(sizeof(v) == list_element_size(list)))
#define LIST_GET_ARRAY(list, type)		((type*)(LIST_ASSERT_TYPEOF(list, type), list_element_array(list)))
#define LIST_GET(list, index, type)		((type*)(LIST_ASSERT_TYPEOF(list, type), list_get(list, index)))
#define LIST_PUSH(list, element)		(LIST_ASSERT_TYPEOF(list, element), list_push(list, &element))
#define LIST_ADD(list, element, pos)	(LIST_ASSERT_TYPEOF(list, element), list_add(list, &element, pos))

typedef struct list* list_t;

int list_reserved(const list_t list);
int list_count(const list_t list);
int list_element_size(const list_t list);
void* list_element_array(const list_t list);
void* list_get(const list_t list, int i);
list_t list_get_range(const list_t list, int start, int end);
uint64_t list_hash(const list_t list);

list_t list_create(int element_size);
list_t list_create_with_array(const void* element_array, int element_size, int count);
void list_destroy(list_t list);
bool list_reserve(list_t list, int count);
bool list_push(list_t list, const void* element);
void list_pop(list_t list, void* out);
bool list_concat(list_t list, const list_t other, int pos);
bool list_add(list_t list, const void* element, int pos);
void list_remove(list_t list, int pos);
void list_splice(list_t list, int start, int end);
void list_clear(list_t list);

extern inline bool list_push_primitive(list_t list, void* primitive)
{
	return list_push(list, &primitive);
}

extern inline bool list_add_primitive(list_t list, void* primitive, int pos)
{
	return list_add(list, &primitive, pos);
}

extern inline void list_splice_count(list_t list, int start, int count)
{
	int end = start + count - 1;
	if (count > 0)
		list_splice(list, start, end >= list_count(list) ? (list_count(list) - 1) : end);
}

#define __STR2(s) __STR(s)
#define __STR(s) #s
#define DEBUG_ON_FAILURE(func)			((func) || debug_format(#func " failed at line " __STR2(__LINE__) ".\n"))

/* ALWAYS returns false. Look at macro above */
bool debug_format(const char* fmt, ...);

extern inline int round_to_power_of_two(int i)
{
	i--;
	i |= i >> 1;
	i |= i >> 2;
	i |= i >> 4;
	i |= i >> 8;
	i |= i >> 16;
	return ++i;
}

/* ints are saved to disk with 4 bytes, not sizeof(int) on this platform */
#define INT_SIZE 4
#define CHAR_SIZE 1

/* you must free the pointer returned by this function */
char* read_all_file(FILE* file, long* size);
bool read_int(const char* buf, long pos, long size, int* out);
bool read_char(const char* buf, long pos, long size, char* out);
bool write_int(FILE* file, int in);
bool write_char(FILE* file, char ch);
bool clear_file(const char* dir);