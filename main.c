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
	default_file = file_open(buf, TYPE_PLAIN);
	user_t user;
	if (user_load(&user))
	{
		for (int i = 0; i < list_count(user.file_saves); i++)
		{
			file_save_t* save = LIST_GET(user.file_saves, 0, file_save_t);
			file_type_t dir_type = file_extension_to_type(save->directory);
			file_details_t details = file_open(save->directory, dir_type);
			if (IS_BAD_DETAILS(details))
				continue;
			console_set_file_details(details);
			if (editor_is_valid_cursor(console_lines(), save->cursor))
				console_move_cursor(console_adjust_cursor(save->cursor, 0, 0));
			else
			{
				debug_format("Invalid cursor placement saved to user file.\n");
				continue;
			}
			return true;
		}
		console_set_file_details(default_file);
		file_save_t save = { .cursor = (coords_t) { 0 }, .directory = default_file.directory };
		if (LIST_PUSH(user.file_saves, save))
			return true;
	}
	return false;
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

	console_loop();
	console_destroy();
	user_t user = user_get_latest();
	user_unload(&user);
	return 0;
}

#endif