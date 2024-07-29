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

static void journal_panic(void)
{
	static bool already_called = false;
	if (already_called)
	{
		/* debug_format uses regular malloc, not journal_malloc so it can't panic more than twice. */
		debug_format("Paniced twice!! Dropping everything.\n");
		return;
	}
	already_called = true;

	user_t user = user_get_latest();
	user_unload(&user);
	const file_details_t details = console_file();
	console_destroy();
	bool saved_file = file_save(details); /* free memory before trying to save, file_save allocates memory. */
	debug_format("Ran out of memory, %s current file.\n", saved_file ? "successfully saved" : "failed to save");
}

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
	default_file = file_open(buf);
	user_t user;
	if (!user_load(&user))
		return false;
	console_set_color(user.foreground, user.background);
	console_set_font(user.font);
	for (int i = 0; i < list_count(user.file_saves); i++)
	{
		file_save_t* save = LIST_GET(user.file_saves, 0, file_save_t);
		file_details_t details = file_open(save->directory);
		if (IS_BAD_DETAILS(details))
			continue;
		debug_format("Saved cursor location for file \"%s\" is (%i, %i)\n", save->directory, save->cursor.column + 1, save->cursor.row + 1);
		console_set_file_details(details);
		console_move_cursor(save->cursor);
		return true;
	}
	console_set_file_details(default_file);
	file_save_t save = { .cursor = (coords_t) { 0 }, .directory = default_file.directory };
	LIST_PUSH(user.file_saves, save);
	return true;
}

int main(int argc, char** argv)
{
	panic_callback = journal_panic;

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