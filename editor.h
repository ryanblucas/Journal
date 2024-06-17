/*
	editor.h ~ RL

	Handles events, data structures, and algorithms
	Frontend for Journal
*/

#pragma once

#include "util.h"

#define CHECK_FOR_NEWLINE(ch)		((ch) == '\n' || (ch) == '\r')
#define TAB_SIZE					4

typedef struct coords
{
	int column;
	int row;
} coords_t;

typedef struct line
{
	list_t string;
} line_t;

/* frees lines' strings */
void editor_destroy_lines(list_t lines);

/* returns whether or not a cursor position is valid */
bool editor_is_valid_cursor(list_t lines, coords_t coords);
/* -1 if a < b, 0 if a == b, 1 if a > b */
int editor_compare_cursors(coords_t a, coords_t b);

/* adds new line character at position, splitting the line at position in two */
bool editor_add_newline(list_t lines, coords_t position);
/* copies raw string at position, incrementing position coords accordingly. Formats tabs */
bool editor_add_raw(list_t lines, const char* raw, coords_t* position);
/* adds tab at position in line list, incrementing position coords accordingly */
bool editor_add_tab(list_t lines, coords_t* position);
/* formats text (ex. "\\r\\n" -> "\\n") */
bool editor_format_raw(list_t str);

/* formats and copies all lines to string */
int editor_copy_all_lines(const list_t lines, list_t str);
/* writes to the out list as a string */
bool editor_copy_region(const list_t lines, list_t out, coords_t begin, coords_t end);
/* deletes region of lines */
void editor_delete_region(list_t lines, coords_t begin, coords_t end);