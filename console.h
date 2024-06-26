/*
	console.h ~ RL

	User interface
*/

#pragma once

#include "editor.h"
#include "file.h"
#include <stdbool.h>
#include "util.h"

typedef enum color
{
	COLOR_BLACK,		// 000
	COLOR_DARK_BLUE,	// 001
	COLOR_DARK_GREEN,	// 010
	COLOR_DARK_CYAN,	// 011
	COLOR_DARK_RED,		// 100
	COLOR_DARK_PURPLE,	// 101
	COLOR_DARK_YELLOW,	// 110
	COLOR_LIGHT_GRAY,	// 111
	COLOR_DARK_GRAY,
	COLOR_LIGHT_BLUE,
	COLOR_LIGHT_GREEN,
	COLOR_LIGHT_CYAN,
	COLOR_LIGHT_RED,
	COLOR_LIGHT_PURPLE,
	COLOR_LIGHT_YELLOW,
	COLOR_WHITE,
} color_t;

typedef struct action
{
	coords_t start, end, cursor;
	bool did_remove;
	bool coupled; /* coupled with previous action in buffer */
	char* str;
} action_t;

bool console_is_created(void);
const list_t console_lines(void);
coords_t console_cursor(void);
const line_t console_current_line(void);
int console_copy_contents_string(list_t str);
const char* console_directory(void);
const list_t console_actions(void);
bool console_clipboard(list_t str);

/* set console's file details. File details are copied on the console's end */
void console_set_file_details(const file_details_t details);
/* sets clipboard */
bool console_set_clipboard(const char* str, size_t size);

/* destroys the console */
void console_destroy(void);
/* destroys the console if it is created then creates the console */
bool console_create(void);
/* re-renders the screen */
bool console_invalidate(void);

/* moves cursor given delta parameters */
coords_t console_adjust_cursor(coords_t coords, int dc, int dr);
/* physically moves cursor */
bool console_move_cursor(coords_t coords);
/* returns whether or not a cursor position is valid */
bool console_is_valid_cursor(coords_t coords);
/* adds raw text to coordinates, formatting appropriately */
bool console_add_raw(const char* raw, coords_t* coords);

/* returns false if there is no selection, otherwise sets pointers to cursor positions */
bool console_get_selection_region(coords_t* begin, coords_t* end);
/* sets contents of assumed empty list "str" to the contents of the selection */
bool console_copy_selection_string(list_t str);
/* deletes contents of selection */
bool console_delete_selection(void);

/* clears action buffer entirely */
void console_clear_buffer(void);

/*
	THE BELOW ARE EVENT HANDLERS THAT ADD TO THE ACTION BUFFER
*/

/* blocking function that parses and returns upon input */
bool console_poll_events(void);
/* handles an arrow key action */
bool console_arrow_key(bool shifting, int dc, int dr);
/* handles a DEL key command */
bool console_delete(void);
/* handles a BKSPC key command */
bool console_backspace(void);
/* handles an enter/return key command */
bool console_return(void);
/* handles a tab key command */
bool console_tab(void);
/* handles adding a character */
bool console_character(int ch);
/* copies to clipboard -- has nothing to add to an action buffer */
bool console_copy(void);
/* pastes from clipboard to current position */
bool console_paste(void);
/* puts action to undo in out (IF NOT NULL) and then undoes it */
bool console_undo(action_t* out);
/* redoes last undo and puts that action in out (IF NOT NULL) */
bool console_redo(action_t* out);