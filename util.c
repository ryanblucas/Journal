/*
	util.c ~ RL

	Miscellaneous tools and data structures
*/

#include "util.h"
#include <assert.h>
#include <stdlib.h>
#include <string.h>

struct list
{
	int reserved, count;
	int element_size;
	char* element_array;
};

int list_reserved(const list_t list)
{
	assert(list != NULL);
	return list->reserved;
}

int list_count(const list_t list)
{
	assert(list != NULL);
	return list->count;
}

int list_element_size(const list_t list)
{
	assert(list != NULL);
	return list->element_size;
}

void* list_element_array(const list_t list)
{
	assert(list != NULL);
	return list->element_array;
}

void* list_get(const list_t list, int i)
{
	assert(list != NULL);
	if (i < 0 || i >= list->count)
		return NULL;
	return list->element_array + i * list->element_size;
}

list_t list_get_range(const list_t list, int start, int end)
{
	assert(list != NULL && start >= 0 && end >= start && end < list->count);
	list_t result = list_create(list->element_size);
	if (!result)
		return NULL;

	int space = end - start + 1;
	if (space >= result->reserved)
	{
		if (!list_reserve(result, round_to_power_of_two(space) - result->reserved))
		{
			list_destroy(list);
			return NULL;
		}
	}

	memcpy(result->element_array, list->element_array + start, space);
	result->count = space;
	return result;
}

uint64_t list_hash(const list_t list)
{
	assert(list != NULL);

	char* element_array = list->element_array;
	uint64_t hash = 5381;
	int c;

	while (c = *element_array++)
		hash = ((hash << 5) + hash) ^ c;

	return hash;
}

list_t list_create(int element_size)
{
	assert(element_size != 0);
	list_t result = malloc(sizeof * result);
	if (!result)
		return NULL;

	result->count = 0;
	result->reserved = STARTING_RESERVE;
	result->element_size = element_size;
	result->element_array = malloc((size_t)element_size * result->reserved);
	if (!result->element_array)
	{
		free(result);
		return NULL;
	}
	return result;
}

list_t list_create_with_array(const void* element_array, int element_size, int count)
{
	assert(element_size != 0);
	if (!element_array)
		return list_create(element_size);
	list_t result = malloc(sizeof * result);
	if (!result)
		return NULL;
	result->count = count;
	result->reserved = round_to_power_of_two(count);
	result->element_size = element_size;
	result->element_array = malloc((size_t)element_size * result->reserved);
	if (!result->element_array)
	{
		free(result);
		return NULL;
	}
	memcpy(result->element_array, element_array, count * element_size);
	return result;
}

void list_destroy(list_t list)
{
	if (list)
	{
		free(list->element_array);
		list->element_array = NULL;
	}
	free(list);
}

bool list_reserve(list_t list, int count)
{
	assert(list != NULL && count > 0);
	if (count == 0)
		return true;

	char* reallocated = realloc(list->element_array, (size_t)(list->reserved + count) * list->element_size);
	if (!reallocated)
		return false;
	
	list->element_array = reallocated;
	list->reserved += count;
	return true;
}

bool list_push(list_t list, const void* element)
{
	assert(list != NULL && element);
	memcpy(&list->element_array[list->element_size * list->count], element, list->element_size);
	if (++list->count >= list->reserved)
		return list_reserve(list, list->reserved);
	return true;
}

void list_pop(list_t list, void* out)
{
	assert(list != NULL);
	if (list->count <= 0)
	{
		if (out)
			memset(out, 0, list->element_size);
		return;
	}
	list->count--;
	if (out)
		memcpy(out, &list->element_array[list->element_size * list->count], list->element_size);
}

bool list_concat(list_t list, const list_t other, int pos)
{
	assert(list != NULL && other != NULL && pos >= 0 && pos <= list->count && other->element_size == list->element_size);
	int bound = max(list->count + other->count, pos + other->count * 2);
	if (list->reserved <= bound)
	{
		if (!list_reserve(list, round_to_power_of_two(bound) - list->reserved))
			return false;
	}

	memmove(&list->element_array[(pos + other->count) * list->element_size], &list->element_array[pos * list->element_size], (size_t)other->count * list->element_size);
	memcpy(&list->element_array[pos * list->element_size], other->element_array, (size_t)other->count * list->element_size);
	list->count += other->count;
	return true;
}

bool list_add(list_t list, const void* element, int pos)
{
	assert(list != NULL && pos >= 0 && pos <= list->count);
	int offset = list->element_size * pos;
	memmove(&list->element_array[list->element_size + offset], &list->element_array[offset], (size_t)list->count * list->element_size - offset);
	memcpy(&list->element_array[offset], element, list->element_size);
	if (++list->count >= list->reserved)
		return list_reserve(list, list->reserved);
	return true;
}

void list_remove(list_t list, int pos)
{
	assert(list != NULL && pos >= 0 && pos < list->count);
	memmove(&list->element_array[pos * list->element_size], &list->element_array[pos * list->element_size + list->element_size], (size_t)(list->count - pos + 1) * list->element_size);
	list->count--;
}

void list_splice(list_t list, int start, int end)
{
	assert(list != NULL && start >= 0 && end >= start && end < list->count);
	memmove(&list->element_array[start * list->element_size], &list->element_array[end * list->element_size + list->element_size], (size_t)(list->count - end + 1) * list->element_size);
	list->count -= end - start + 1;
}

void list_clear(list_t list)
{
	assert(list != NULL);
	list->count = 0;
}

#ifdef _WIN32
#include <Windows.h>
#include <strsafe.h>

bool debug_format(const char* fmt, ...)
{
	static char* current_buffer = NULL;
	static int size;

	if (!current_buffer)
	{
		size = 256;
		current_buffer = malloc(size * sizeof * current_buffer);
		if (!current_buffer)
			exit(1); /* The program most certainly couldn't run if it can't allocate 256 bytes */
	}

	va_list list;
	va_start(list, fmt);
	while (StringCbVPrintfA(current_buffer, size, fmt, list) == STRSAFE_E_INSUFFICIENT_BUFFER)
	{
		free(current_buffer);
		size *= 2;
		current_buffer = malloc(size * sizeof * current_buffer);
		if (!current_buffer)
			exit(1);
	}
	va_end(list);

	OutputDebugStringA(current_buffer);
	return false;
}
#endif

/* you must free the pointer returned by this function */
char* read_all_file(FILE* file, long* size)
{
	assert(file && size);
	if (fseek(file, 0, SEEK_END) != 0)
		return NULL;
	*size = ftell(file);
	if (*size == -1 || fseek(file, 0, SEEK_SET) != 0)
		return NULL;

	char* buf = malloc(*size);
	if (!buf)
		return NULL;

	*size = (long)fread(buf, 1, *size, file);
	return buf;
}

bool read_int(const char* buf, long pos, long size, int* out)
{
	assert(buf && pos >= 0 && size > 0 && out);
	if (size < pos + INT_SIZE)
	{
		*out = 0;
		debug_format("Read int outside bounds!\n");
		return false;
	}
	*out = (int)buf[pos] | ((int)buf[pos + 1] << 8) | ((int)buf[pos + 2] << 16) | ((int)buf[pos + 3] << 24);
	return true;
}

bool read_char(const char* buf, long pos, long size, char* out)
{
	assert(buf && pos >= 0 && size > 0 && out);
	if (size < pos + CHAR_SIZE)
	{
		*out = 0;
		debug_format("Read char outside bounds!\n");
		return false;
	}
	*out = (char)buf[pos];
	return true;
}

bool write_int(FILE* file, int in)
{
	assert(file);
	char buf[INT_SIZE] = { in & 0xFF, (in >> 8) & 0xFF, (in >> 16) & 0xFF, (in >> 24) & 0xFF };
	return fwrite(buf, 1, INT_SIZE, file) == INT_SIZE;
}

bool write_char(FILE* file, char ch)
{
	assert(file);
	return fwrite(&ch, 1, 1, file);
}

bool clear_file(const char* dir)
{
	assert(dir);
	FILE* f = fopen(dir, "w");
	if (f)
		fclose(f);
	return !!f;
}