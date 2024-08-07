/*
	editor.c ~ RL

	Handles events, data structures, and algorithms
	Frontend for Journal
*/

#include "editor.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>

#define IS_LIST_VALID(lines)		(lines && list_element_size(lines) == sizeof(line_t))

/* creates valid list of lines */
list_t editor_create_lines(void)
{
	list_t result = list_create(sizeof(line_t));
	line_t elem = (line_t){ .string = list_create(sizeof(char)) };
	LIST_PUSH(result, elem);
	return result;
}

/* frees lines' strings */
void editor_destroy_lines(list_t lines)
{
	if (!lines)
		return;
	assert(IS_LIST_VALID(lines));
	for (int i = 0; i < list_count(lines); i++)
	{
		line_t* li = LIST_GET(lines, i, line_t);
		if (li->string)
			list_destroy(li->string);
	}
	list_clear(lines);
}

/* returns whether or not a cursor position is valid */
bool editor_is_valid_cursor(list_t lines, coords_t coords)
{
	assert(IS_LIST_VALID(lines));
	return coords.column >= 0 && coords.row >= 0 && coords.row < list_count(lines) && coords.column <= list_count(LIST_GET(lines, coords.row, line_t)->string);
}

/* -1 if a < b, 0 if a == b, 1 if a > b */
int editor_compare_cursors(coords_t a, coords_t b)
{
	if (a.row < b.row)
		return -1;
	else if (a.row > b.row)
		return 1;
	else if (a.column < b.column) /* rows are the same */
		return -1;
	else if (b.column > a.column)
		return 1;
	else /* row and column are the same */
		return 0;
}

/* adds new line character at position, splitting the line at position in two */
void editor_add_newline(list_t lines, coords_t position)
{
	assert(IS_LIST_VALID(lines) && editor_is_valid_cursor(lines, position));
	list_t string = LIST_GET(lines, position.row, line_t)->string, new_string;
	if (position.column < list_count(string))
	{
		new_string = list_get_range(string, position.column, list_count(string) - 1);
		list_splice(string, position.column, list_count(string) - 1);
	}
	else
		new_string = list_create(sizeof(char));
	line_t new_line = { .string = new_string };
	LIST_ADD(lines, new_line, position.row + 1);
}

/* copies raw string at position, incrementing position coords accordingly. Formats tabs */
void editor_add_raw(list_t lines, const char* raw, coords_t* position)
{
	assert(IS_LIST_VALID(lines) && raw && editor_is_valid_cursor(lines, *position));
	int ch;
	while (ch = *raw++)
	{
		if (CHECK_FOR_NEWLINE(ch))
		{
			editor_add_newline(lines, *position);
			*position = (coords_t){ 0, position->row + 1 };
		}
		else if (ch == '\t')
		{
			editor_add_tab(lines, position);
			position->column++;
		}
		else
			LIST_ADD(LIST_GET(lines, position->row, line_t)->string, (char)ch, position->column++);
	}
	position->column--;
}

/* adds tab at position in line list, incrementing position coords accordingly */
bool editor_add_tab(list_t lines, coords_t* position)
{
	assert(IS_LIST_VALID(lines) && editor_is_valid_cursor(lines, *position));
	list_t str = LIST_GET(lines, position->row, line_t)->string;
	int tabc = TAB_SIZE - position->column % TAB_SIZE;
	for (int i = 0; i < tabc; i++)
		list_add_primitive(str, (void*)' ', i + position->column);
	position->column += tabc - 1;
	return true;
}

/* formats text (ex. "\\r\\n" -> "\\n") */
bool editor_format_raw(list_t str)
{
	assert(str && list_element_size(str) == sizeof(char));
	char* arr = (char*)list_element_array(str);
	bool newline = false;
	for (int i = list_count(str) - 1; i >= 0; i--)
	{
		if (arr[i] == '\n')
		{
			newline = true;
			continue;
		}
		else if (arr[i] == '\r' && newline)
			list_remove(str, i);
		newline = false;
	}
	return true;
}

/* formats and copies all lines to string */
int editor_copy_all_lines(const list_t lines, list_t str)
{
	assert(IS_LIST_VALID(lines) && str && list_count(str) == 0 && list_element_size(str) == sizeof(char));
	int result = 0;
	for (int i = 0; i < list_count(lines); i++)
	{
		list_t curr = LIST_GET(lines, i, line_t)->string;
		list_concat(str, curr, result);
		list_push_primitive(str, (void*)'\n');
		result += list_count(curr) + 1;
	}
	list_pop(str, NULL);
	list_push_primitive(str, '\0');
	return result;
}

/* writes to the out list as a string */
void editor_copy_region(const list_t lines, list_t out, coords_t begin_coords, coords_t end_coords)
{
	assert(
		IS_LIST_VALID(lines) 
		&& list_count(out) == 0 
		&& list_element_size(out) == sizeof(char) 
		&& editor_is_valid_cursor(lines, begin_coords) 
		&& editor_is_valid_cursor(lines, end_coords)
		&& editor_compare_cursors(begin_coords, end_coords) <= 0);

	const line_t* start = LIST_GET(lines, begin_coords.row, line_t), *end = LIST_GET(lines, end_coords.row, line_t);
	list_concat(out, start->string, 0);
	if (start != end)
	{
		list_splice_count(out, 0, begin_coords.column);
		list_push_primitive(out, (void*)'\n');
		for (int i = begin_coords.row + 1; i < end_coords.row; i++)
		{
			list_concat(out, LIST_GET(lines, i, line_t)->string, list_count(out));
			list_push_primitive(out, (void*)'\n');
		}
		int pos = list_count(out);
		list_concat(out, end->string, pos);
		list_splice_count(out, pos + end_coords.column + 1, list_count(end->string) - end_coords.column - 1);
	}
	else
	{
		list_splice_count(out, end_coords.column + 1, list_count(out) - end_coords.column - 1);
		list_splice_count(out, 0, begin_coords.column);
		if (end_coords.column == list_count(out))
			list_push_primitive(out, (void*)'\n');
	}

	list_push_primitive(out, (void*)'\0');
}

/* deletes region of lines */
void editor_delete_region(list_t lines, coords_t begin, coords_t end)
{
	assert(IS_LIST_VALID(lines) && editor_is_valid_cursor(lines, begin) && editor_is_valid_cursor(lines, end) && editor_compare_cursors(begin, end) <= 0);
	list_t begin_string = LIST_GET(lines, begin.row, line_t)->string;
	int first_row_end;
	if (begin.row != end.row)
	{
		first_row_end = list_count(begin_string) - 1;
		list_splice_count(LIST_GET(lines, end.row, line_t)->string, 0, end.column + 1);
		list_concat(begin_string, LIST_GET(lines, end.row, line_t)->string, list_count(begin_string));
		for (int i = begin.row + 1; i < end.row; i++)
			list_destroy(LIST_GET(lines, i, line_t)->string);
		list_splice(lines, begin.row + 1, end.row);
	}
	else
	{
		first_row_end = end.column;
		if (first_row_end == list_count(begin_string))
		{
			assert(end.row + 1 < list_count(lines));
			list_t next_string = LIST_GET(lines, begin.row + 1, line_t)->string;
			list_concat(begin_string, next_string, list_count(begin_string));
			list_destroy(next_string);
			list_remove(lines, begin.row + 1);
			/* we removed the newline so we move our cursor back */
			first_row_end--;
		}
	}

	if (first_row_end >= begin.column)
		list_splice(begin_string, begin.column, first_row_end);
}

/* adds character position to cursor */
coords_t editor_overflow_cursor(list_t lines, coords_t cursor)
{
	cursor.row = min(max(0, cursor.row), list_count(lines) - 1);
	while (cursor.row > 0 && cursor.column < 0)
	{
		cursor.row--;
		cursor.column += list_count(LIST_GET(lines, cursor.row, line_t)->string) + 1;
	}
	while (cursor.row < list_count(lines) - 1 && cursor.column > list_count(LIST_GET(lines, cursor.row, line_t)->string))
	{
		cursor.column -= list_count(LIST_GET(lines, cursor.row, line_t)->string) + 1;
		cursor.row++;
	}
	cursor.column = min(cursor.column, list_count(LIST_GET(lines, cursor.row, line_t)->string));
	return cursor;
}