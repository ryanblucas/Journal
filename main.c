/*
	main.c ~ RL
*/

#include "console.h"
#include "file.h"
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include "user.h"
#include "util.h"

#ifndef TEST

static file_details_t default_file;

static bool load_config(void)
{
	char directory[260];
	if (!user_get_user_directory(directory))
		return false;
	static char buf[260];
	snprintf(buf, 260, "%s\\Plain.txt", directory);
	if (!file_exists(buf))
	{
		FILE* temp = fopen(buf, "w");
		if (temp)
			fclose(temp);
	}
	default_file = file_open(buf, file_open_plain_func);
	user_t user;
	bool result = false;
	if (user_load(&user))
	{
		result = true;
		file_save_t* last = LIST_GET(user.file_saves, 0, file_save_t);
		if (last)
		{
			file_type_t dir_type = file_extension_to_type(strrchr(last->directory, '.'));
			file_details_t last_loaded = file_open(last->directory, file_type_to_open_func(dir_type));
			if (!IS_BAD_DETAILS(last_loaded))
			{
				console_set_file_details(last_loaded);
				console_move_cursor(console_adjust_cursor(last->cursor, 0, 0));
			}
		}
		else
		{
			console_set_file_details(default_file);
			file_save_t save = { .cursor = (coords_t) { 0 }, .directory = default_file.directory };
			if (!LIST_PUSH(user.file_saves, default_file))
				result = false;
		}
	}
	return result;
}

int main(int argc, char** argv)
{
	if (!console_create())
		return 1;
	if (!load_config())
	{
		debug_format("Failed to load config\n");
		if (!default_file.lines)
			debug_format("Failed to load default file\n");
		else
			console_set_file_details(default_file);
	}

	while (console_poll_events());
	console_destroy();
	user_t user = user_get_latest();
	user_unload(&user);
	return 0;
}

#endif