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
	TYPE_PLAIN =		0x00,
	TYPE_COMPRESSED =	0x01,
	TYPE_ENCRYPTED =	0x02
} file_type_t;

typedef struct file_details
{
	const char* directory;
	file_type_t type;
	list_t lines;
} file_details_t;

/* sets password with a max len of 64 */
void file_set_password(const char* password);

/* does file exist */
bool file_exists(const char* directory);
/* gets just the name of a file, no directory */
bool file_get_name(const char* directory, char* buf, int size);
/* gets file type */
bool file_get_type(const char* directory, file_type_t* type);

/* opens file using function */
file_details_t file_open(const char* directory, file_type_t type);
/* saves file using function */
bool file_save(const file_details_t details, file_type_t type);
/* determines type of file to open */
file_details_t file_determine_and_open(const char* directory);

/* get file's extension given file type */
const char* file_type_to_extension(file_type_t type);
/* returns type from extension. You can also pass a file directory in */
file_type_t file_extension_to_type(const char* ext);