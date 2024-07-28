/*
	user.h ~ RL

	Parses and loads user settings
*/

#include "user.h"
#include <assert.h>
#include <stdio.h>
#if _WIN32
#include <Windows.h>
#include <ShlObj.h>
#define MAX_PATH_LEN MAX_PATH
#else
#error "Invalid platform."
#endif

#define DIRECTORY_NAME "Journal"

static char* user_directory;
static bool has_cache;
static user_t cache;

static bool user_find_directory(void)
{
#if _WIN32
	char app_data[MAX_PATH];
	if (FAILED(SHGetFolderPathA(NULL, CSIDL_APPDATA, NULL, SHGFP_TYPE_CURRENT, app_data)))
		return false;
	user_directory = journal_malloc(MAX_PATH);
	snprintf(user_directory, MAX_PATH, "%s\\" DIRECTORY_NAME, app_data);
	DWORD attribs = GetFileAttributesA(user_directory);
	if (attribs == INVALID_FILE_ATTRIBUTES || attribs ^ FILE_ATTRIBUTE_DIRECTORY)
	{
		if (!CreateDirectoryA(user_directory, NULL))
			return false;
	}
	return true;
#else
#error "Unsupported platform."
#endif
}

/* returns user directory size and writes to directory if non-null */
int user_get_user_directory(char* directory)
{
	if (!user_directory && !user_find_directory())
		return 0;
	if (directory)
		strncpy(directory, user_directory, MAX_PATH);
	return (int)strnlen(user_directory, MAX_PATH);
}

static list_t blank_saves;
static user_t user_default(void)
{
	if (!blank_saves && !(blank_saves = list_create(sizeof(file_save_t))))
	{
		debug_format("Failed to create fallback saves, aborting.\n");
		exit(1); /* TO DO: this is really bad */
	}
	return (user_t)
	{
		.background = COLOR_DARK_BLUE,
		.foreground = COLOR_LIGHT_YELLOW,
		.desired_save_type = TYPE_PLAIN,
		.file_saves = blank_saves
	};
}

/* gets latest user loaded/saved or uses default if no cache is available */
user_t user_get_latest(void)
{
	if (!has_cache)
		return user_default();
	return cache;
}

static void user_unload_saves(list_t saves)
{
	if (!saves)
		return;
	for (int i = 0; i < list_count(saves); i++)
		free(LIST_GET(saves, i, file_save_t)->directory); /* TO DO: error here when exiting application */
	list_destroy(saves);
}

/* frees list of configs */
void user_unload(user_t* user)
{
	if (!user)
		return;
	user_unload_saves(user->file_saves);
	has_cache = user->file_saves != cache.file_saves;
	user->file_saves = NULL;
}

static list_t user_load_saves(const char* state, long pos, long size)
{
	list_t result = list_create(sizeof(file_save_t));
	if (!result)
		return NULL;
	file_save_t current = { 0 };
	for (long i = pos, inc = 0; i < size; i += inc)
	{
		/* file saves are layed out like "directory", NUL terminator, saved column, saved row. */
		char buf[MAX_PATH_LEN];
		int dir_size = snprintf(buf, sizeof buf, "%s", &state[i]) + 1; /* snprintf's return value does not include NUL terminator */
		inc = dir_size + INT_SIZE * 2;
		if (!file_exists(buf) || !(current.directory = journal_malloc(dir_size)))
			continue;
		strncpy(current.directory, buf, dir_size);
		read_int(state, i + dir_size, size, &current.cursor.column);
		read_int(state, i + dir_size + INT_SIZE, size, &current.cursor.row);
		LIST_PUSH(result, current);
	}
	return result;
}

/* loads user from disk */
bool user_load(user_t* user)
{
	assert(user);
	memset(user, 0, sizeof * user);
	user->file_saves = list_create(sizeof(file_save_t));
	if (!user->file_saves)
		return false;

	if (!user_directory && !user_find_directory())
		return false;

	char state_dir[MAX_PATH_LEN];
	snprintf(state_dir, MAX_PATH_LEN, "%s\\state", user_directory);
	FILE* state_file = fopen(state_dir, "rb");
	if (!state_file)
	{
		/* creates file */
		state_file = fopen(state_dir, "wb");
		if (!state_file)
			return false;
		fclose(state_file);
		state_file = fopen(state_dir, "rb");
		if (!state_file)
			return false;
	}
	long size;
	char* state = read_all_file(state_file, &size);
	fclose(state_file);
	if (!state)
		return false;

	bool success = size > 0
		&& read_int(state, 0, size, (int*)&user->foreground)
		&& read_int(state, INT_SIZE, size, (int*)&user->background)
		&& read_int(state, INT_SIZE * 2, size, (int*)&user->desired_save_type)
		&& (user->file_saves = user_load_saves(state, INT_SIZE * 3, size));
	
	free(state);
	if (!success)
	{
		user_t temp;
		return user_save(user_default()) && user_load(&temp);
	}

	if (has_cache)
	{
		user_unload_saves(cache.file_saves);
		debug_format("Unloaded cached user\n");
	}
	has_cache = true;
	cache = *user;
	debug_format("Cached latest load request for user data\n");
	return true;
}

/* saves user to disk */
bool user_save(user_t user)
{
	assert(user.file_saves && list_element_size(user.file_saves) == sizeof(file_save_t));
	char state_dir[MAX_PATH];
	snprintf(state_dir, MAX_PATH, "%s\\state", user_directory);
	FILE* state_file = fopen(state_dir, "wb");
	if (!state_file)
		return false;

	bool success = write_int(state_file, user.foreground)
		&& write_int(state_file, user.background)
		&& write_int(state_file, user.desired_save_type);

	for (int i = 0; success && i < list_count(user.file_saves); i++)
	{
		file_save_t* iter = LIST_GET(user.file_saves, i, file_save_t);
		size_t expected = strnlen(iter->directory, MAX_PATH) + 1; /* +1 for NUL character */
		success &= fwrite(iter->directory, 1, expected, state_file) == expected
			&& write_int(state_file, iter->cursor.column)
			&& write_int(state_file, iter->cursor.row);
	}

	fclose(state_file);
	
	if (!success)
	{
		clear_file(state_dir);
		return false;
	}
	
	if (has_cache && cache.file_saves != user.file_saves)
	{
		user_unload_saves(cache.file_saves);
		debug_format("Unloaded cached user\n");
	}
	cache = user;
	has_cache = true;
	debug_format("Cached latest save for user data\n");
	return true;
}

/* saves file save to disk, inserting it to the top of the list */
bool user_save_file(file_save_t save)
{
	assert(save.directory);
	if (!user_directory && !user_find_directory())
		return false;

	/* first, search if a config with the given directory exists */
	user_t user = user_get_latest();
	size_t size = strnlen(save.directory, MAX_PATH);
	bool was_added = false;
	for (int i = 0; i < list_count(user.file_saves); i++)
	{
		if (strncmp(LIST_GET(user.file_saves, i, file_save_t)->directory, save.directory, size) == 0)
		{
			file_save_t temp = *LIST_GET(user.file_saves, i, file_save_t);
			list_remove(user.file_saves, i);
			temp.cursor = save.cursor;
			LIST_ADD(user.file_saves, temp, 0);
			was_added = true;
			break;
		}
	}

	/* otherwise, add it */
	if (!was_added)
		LIST_ADD(user.file_saves, save, 0);

	/* save list to disk */
	return user_save(user);
}