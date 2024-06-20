/*
	file.h ~ RL

	Saves, compresses, and encrypts files.
*/

#pragma once

#include "editor.h"
#include <stdbool.h>
#include "util.h"

#define FAILED_FILE_DETAILS		((file_details_t) { 0 })
#define IS_BAD_DETAILS(d)		(!(d).lines)

typedef enum file_type
{
	TYPE_UNKNOWN,
	TYPE_PLAIN,
	TYPE_COMPRESSED,
} file_type_t;

typedef struct file_details
{
	const char* directory;
	list_t lines;
} file_details_t;

/* open_func_t(raw buffer straight from disk, size of raw buffer) returns success or failure */
typedef bool (*open_func_t)(const char*, long, list_t);
bool file_open_plain_func(const char* buf, long size, list_t out);
bool file_open_dynamic_markov_model_func(const char* buf, long size, list_t out);

/* save_func_t(file, raw buffer, size of buffer) returns success or failure, func clears file for you on failure */
typedef bool (*save_func_t)(FILE*, const char*, int);
bool file_save_plain_func(FILE* file, const char* src, int size);
bool file_save_dynamic_markov_model_func(FILE* file, const char* buf, int size);

/* does file exist */
bool file_exists(const char* directory);
/* gets just the name of a file, no directory */
bool file_get_name(const char* directory, char* buf, int size);
/* gets file type */
bool file_get_type(const char* directory, file_type_t* type);

/* opens file using function */
file_details_t file_open(const char* directory, open_func_t func);
/* saves file using function */
bool file_save(const file_details_t details, save_func_t func);
/* determines type of file to open */
file_details_t file_determine_and_open(const char* directory);

/* get file's extension given file type */
const char* file_type_to_extension(file_type_t type);
/* returns type from extension */
file_type_t file_extension_to_type(const char* ext);

/* TO DO: Macro */

extern inline open_func_t file_type_to_open_func(file_type_t type)
{
	switch (type)
	{
	case TYPE_PLAIN:
		return file_open_plain_func;
	case TYPE_COMPRESSED:
		return file_open_dynamic_markov_model_func;
	}
	return NULL;
}

extern inline save_func_t file_type_to_save_func(file_type_t type)
{
	switch (type)
	{
	case TYPE_PLAIN:
		return file_save_plain_func;
	case TYPE_COMPRESSED:
		return file_save_dynamic_markov_model_func;
	}
	return NULL;
}