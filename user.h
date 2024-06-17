/*
	user.h ~ RL
	
	Parses and loads user settings
*/

#pragma once

#include "console.h"
#include "file.h"
#include "util.h"

typedef struct file_save
{
	const char* directory;
	coords_t cursor;
} file_save_t;

typedef struct user
{
	color_t foreground, background;
	file_type_t desired_save_type;
	list_t file_saves;
} user_t;

/* returns user directory size and writes to directory if non-null */
int user_get_user_directory(char* directory);
/* gets latest user loaded/saved or uses default if no cache is available */
user_t user_get_latest(void);
/* frees list of configs */
void user_unload(user_t* user);
/* loads user from disk */
bool user_load(user_t* user);
/* saves user to disk */
bool user_save(user_t user);
/* saves file save to disk, inserting it to the top of the list */
bool user_save_file(file_save_t save);