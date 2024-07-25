/*
	console_win32.c ~ RL

	User interface - implemented using the Win32 Console API
*/

#include "console.h"
#include <assert.h>
#include "file.h"
#include "user.h"
#include <stdio.h>
#include <Windows.h>

#define CONSOLE_CREATE_ATTRIBUTE(fg, bg)	((fg) | (bg) << 4)
#define CONSOLE_INVERT_ATTRIBUTE(attrib)	(((attrib) >> 4) | (((attrib) << 4) & 0xF0))
#define CONSOLE_DEFAULT_ATTRIBUTE			(1 << 9)
#define CONSOLE_DEFAULT_CHAR				(1 << 17)

typedef int attribute_t;

static inline void console_queue_changes(void);
static inline bool console_submit_changes(void);
static inline bool console_is_point_renderable(coords_t coords);
static inline CHAR_INFO* console_get_cell(int row, int col);
static inline void console_set_cell(int row, int col, attribute_t attrib, int ch);
static inline void console_fill_line(int row, attribute_t attrib, int ch);
static void console_set_stringf(int row, int col, attribute_t attrib, const char* fmt, ...);
static void console_draw_selection(attribute_t attrib);
static inline void console_draw_line(int row, attribute_t attrib);
static inline void console_draw_footer(void);
static inline bool console_write_buffer(void);

static HANDLE input, output;
static COORD size;
static CHAR_INFO* buffer;
static coords_t cursor, camera;
static list_t lines;
static char current_directory[MAX_PATH];
static file_type_t current_type;

static list_t actions;
static list_t undid_actions;

static bool selecting;
static coords_t selection_begin;

static bool write_buffer = true;
static attribute_t user_attribute =		CONSOLE_CREATE_ATTRIBUTE(COLOR_LIGHT_GRAY, COLOR_BLACK);
static attribute_t footer_attribute =	CONSOLE_CREATE_ATTRIBUTE(COLOR_WHITE, COLOR_DARK_GRAY);

bool console_is_created(void)
{
	return !!output;
}

const list_t console_lines(void)
{
	assert(console_is_created());
	return lines;
}

coords_t console_cursor(void)
{
	assert(console_is_created());
	return cursor;
}

const line_t console_current_line(void)
{
	assert(console_is_created());
	return *LIST_GET(lines, cursor.row, line_t);
}

int console_copy_contents_string(list_t str)
{
	assert(console_is_created());
	return editor_copy_all_lines(lines, str);
}

const char* console_directory(void)
{
	assert(console_is_created());
	return current_directory;
}

const list_t console_actions(void)
{
	assert(console_is_created());
	return actions;
}

bool console_clipboard(list_t str)
{
	assert(str != NULL && list_element_size(str) == sizeof(char));
	if (!OpenClipboard(NULL))
		return false;
	HANDLE buf_handle = GetClipboardData(CF_TEXT);
	if (!buf_handle)
		return false;
	char* buf = GlobalLock(buf_handle);
	if (!buf)
		return false;

	list_t temp = list_create_with_array(buf, sizeof(char), (int)strnlen(buf, 0xFFFFFF) + 1);
	if (!temp)
		return false;
	editor_format_raw(temp);
	bool result = list_concat(str, temp, 0);
	list_destroy(temp);
	GlobalUnlock(buf_handle);
	CloseClipboard();
	return result;
}

/* set console's file details. File details are copied on the console's end */
void console_set_file_details(const file_details_t details)
{
	assert(details.directory && details.lines && list_element_size(details.lines) == sizeof(line_t));
	for (int i = 0; i < list_count(details.lines); i++)
		assert(LIST_GET(details.lines, i, line_t)->string && list_element_size(LIST_GET(details.lines, i, line_t)->string) == sizeof(char));

	editor_destroy_lines(lines);
	list_clear(lines);
	list_concat(lines, details.lines, 0);
	console_move_cursor((coords_t) { 0, 0 });
	console_invalidate();

	char title_buf[128];
	char file_buf[128];
	file_get_name(details.directory, file_buf, sizeof file_buf);
	snprintf(title_buf, sizeof title_buf, "Journal - %s", file_buf);
	DEBUG_ON_FAILURE(SetConsoleTitleA(title_buf));

	strncpy(current_directory, details.directory, sizeof current_directory);
	current_type = details.type;

	selecting = false;
}

/* sets clipboard */
bool console_set_clipboard(const char* str, size_t size)
{
	assert(str);
	HGLOBAL buf_handle = GlobalAlloc(GMEM_MOVEABLE, size);
	if (!buf_handle)
		return false;
	char* buf = GlobalLock(buf_handle);
	if (!buf)
		return false;
	memcpy(buf, str, size);
	GlobalUnlock(buf_handle);
	if (!OpenClipboard(NULL))
		return false;
	if (!SetClipboardData(CF_TEXT, buf_handle))
		return false;
	CloseClipboard();
	return true;
}

static void console_destroy_interface(void)
{
	input = NULL;
	CloseHandle(output);
	output = NULL;
	FreeConsole();
}

static void console_destroy_physical(void)
{
	editor_destroy_lines(lines);
	list_destroy(lines);

	console_clear_buffer();
	list_destroy(actions);
	list_destroy(undid_actions);
}

/* destroys the console */
void console_destroy(void)
{
	if (!console_is_created())
		return;
	console_destroy_interface();
	console_destroy_physical();
}

static bool console_create_interface(void)
{
	AllocConsole();
	HANDLE temp_input = GetStdHandle(STD_INPUT_HANDLE);
	if (temp_input == INVALID_HANDLE_VALUE)
		return false;
	else if (temp_input == NULL)
	{
		temp_input = CreateFileW(L"CONIN$", GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
		if (temp_input == INVALID_HANDLE_VALUE || !SetStdHandle(STD_INPUT_HANDLE, temp_input))
			return false;
	}
	HANDLE temp_output = CreateConsoleScreenBuffer(GENERIC_READ | GENERIC_WRITE, 0, NULL, CONSOLE_TEXTMODE_BUFFER, NULL);
	if (!temp_output)
		return false;

	if (!SetConsoleMode(temp_output, DISABLE_NEWLINE_AUTO_RETURN) || !SetConsoleMode(temp_input, ENABLE_MOUSE_INPUT | ENABLE_WINDOW_INPUT))
	{
		CloseHandle(temp_output);
		return false;
	}

	if (!SetConsoleActiveScreenBuffer(temp_output))
	{
		CloseHandle(temp_output);
		return false;
	}

	CONSOLE_SCREEN_BUFFER_INFO csbi;
	if (!GetConsoleScreenBufferInfo(temp_output, &csbi))
	{
		CloseHandle(temp_output);
		return false;
	}
	COORD temp_size = csbi.dwSize;
	int buffer_size = sizeof * buffer * temp_size.X * temp_size.Y;
	CHAR_INFO* temp_buffer = malloc(buffer_size);
	if (!temp_buffer)
	{
		CloseHandle(temp_output);
		return false;
	}
	memset(temp_buffer, 0, buffer_size);

	input = temp_input;
	output = temp_output;
	buffer = temp_buffer;
	size = temp_size;
	return true;
}

static bool console_create_editor(void)
{
	lines =			editor_create_lines();
	actions =		list_create(sizeof(action_t));
	undid_actions = list_create(sizeof(action_t));
	if (lines != NULL && actions != NULL && undid_actions != NULL)
		return true;
	list_destroy(undid_actions);
	list_destroy(actions);
	list_destroy(lines);
	return false;
}

/* destroys the console if it is created then creates the console */
bool console_create(void)
{
	if (console_is_created())
		console_destroy();
	if (!console_create_interface())
		return false;
	if (!console_create_editor())
	{
		console_destroy_interface();
		return false;
	}
	DEBUG_ON_FAILURE(console_invalidate()); /* If it fails to draw, then it's not really an initialization problem like one might expect from a false return value */
	return true;
}

static bool console_handle_potential_resize(void)
{
	/* resize buffer to window */
	CONSOLE_SCREEN_BUFFER_INFOEX csbi = { .cbSize = sizeof csbi };
	if (!GetConsoleScreenBufferInfoEx(output, &csbi))
		return false;
	if (csbi.dwSize.X == size.X && csbi.dwSize.Y == size.Y)
		return true;
	csbi.dwSize = (COORD){ csbi.srWindow.Right + 1, csbi.srWindow.Bottom + 1 };
	if (!SetConsoleScreenBufferInfoEx(output, &csbi))
		return false;
	CHAR_INFO* temp = malloc(sizeof * temp * csbi.dwSize.X * csbi.dwSize.Y);
	if (!temp)
		return false;
	memset(temp, 0, sizeof * temp * csbi.dwSize.X * csbi.dwSize.Y);
	free(buffer);
	buffer = temp;
	size = csbi.dwSize;

	/* remove scrollbar */
	CONSOLE_FONT_INFO cfi;
	if (!GetCurrentConsoleFont(output, FALSE, &cfi))
		return false;
	RECT fitted = (RECT){ .right = size.X * cfi.dwFontSize.X, .bottom = size.Y * cfi.dwFontSize.Y };
	assert(AdjustWindowRectEx(&fitted, GetWindowLong(GetConsoleWindow(), GWL_STYLE), FALSE, GetWindowLong(GetConsoleWindow(), GWL_EXSTYLE)));
	return DEBUG_ON_FAILURE(SetWindowPos(GetConsoleWindow(), NULL, 0, 0, fitted.right - fitted.left, fitted.bottom - fitted.top, SWP_NOMOVE)) && 
		DEBUG_ON_FAILURE(console_move_cursor(cursor)) && DEBUG_ON_FAILURE(console_invalidate());
}

static bool console_copy_selection_to_clipboard(void)
{
	list_t str = list_create(sizeof(char));
	if (!str || !console_copy_selection_string(str))
	{
		list_destroy(str);
		return false;
	}
	return console_set_clipboard(list_element_array(str), list_count(str));
}

static bool console_paste_selection(void)
{
	list_t str = list_create(sizeof(char));
	if (!str)
		return false;
	if (!console_clipboard(str))
	{
		list_destroy(str);
		return false;
	}

	bool result = console_add_raw(list_element_array(str), &cursor);
	DEBUG_ON_FAILURE(console_move_cursor(console_adjust_cursor(cursor, 1, 0)));
	list_destroy(str);
	return result;
}

static char* console_open_file_dialog(bool does_file_exist)
{
	static char buf[MAX_PATH];

	OPENFILENAMEA settings =
	{
		/* OFN_DONTADDTORECENT ~ Plain text files can be opened by anyone, but compressed and encrypted can't. This wouldn't make sense for those files */
		.Flags =			OFN_DONTADDTORECENT | (does_file_exist ? (OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST) : 0),
		.lStructSize =		sizeof settings,
		.lpstrFilter =		"Journal Text Files (.txt, .dmc)\0*.txt;*.dmc\0\0",
		.nFilterIndex =		1,
		.lpstrInitialDir =	current_directory,
		.lpstrFile =		buf,
		.nMaxFile =			sizeof buf,
	};
	if (!GetOpenFileNameA(&settings))
		return NULL;
	return buf;
}

/* copies to clipboard -- has nothing to add to an action buffer */
bool console_copy(void)
{
	assert(console_is_created());
	if (!selecting)
		return true;
	return console_copy_selection_to_clipboard();
}

/* pastes from clipboard to current position */
bool console_paste(void)
{
	assert(console_is_created());
	bool was_selecting = selecting;
	if (selecting)
		console_delete_selection();
	coords_t prev = cursor;
	list_t str = list_create(sizeof(char));
	if (!str)
		return false;
	if (!console_clipboard(str) || !console_paste_selection())
	{
		list_destroy(str);
		return false;
	}
	char* str_copy = malloc(list_count(str));
	if (!str_copy)
	{
		list_destroy(str);
		return false;
	}
	strncpy(str_copy, list_element_array(str), list_count(str));
	list_destroy(str);
	action_t action = { .coupled = was_selecting, .cursor = prev, .start = prev, .end = console_adjust_cursor(cursor, -1, 0), .did_remove = false, .str = str_copy };
	return LIST_PUSH(actions, action);
}

static bool console_generic_do(bool direction, action_t* out)
{
	assert(console_is_created());
	list_t buffer = direction ? undid_actions : actions, other = direction ? actions : undid_actions;
	if (list_count(buffer) <= 0)
		return true;

	int other_add = list_count(other);
	action_t head, curr;
	list_pop(buffer, &head);
	curr = head;
	do
	{
		/* true for redo */
		bool adjusted = direction ? curr.did_remove : !curr.did_remove;
		if (adjusted)
			editor_delete_region(lines, curr.start, curr.end);
		else
		{
			coords_t temp = curr.start;
			if (!editor_add_raw(lines, curr.str, &temp))
				return false;
		}
		if (!console_move_cursor(console_adjust_cursor(curr.cursor, 0, 0)))
			return false;
		DEBUG_ON_FAILURE(LIST_ADD(other, curr, other_add));
	} while (curr.coupled && (list_pop(buffer, &curr), true));
	if (out)
		*out = head;
	return true;
}

/* puts action to undo in out (IF NOT NULL) and then undoes it */
bool console_undo(action_t* out)
{
	assert(console_is_created());
	return console_generic_do(false, out);
}

/* redoes last undo and puts that action in out (IF NOT NULL) */
bool console_redo(action_t* out)
{
	assert(console_is_created());
	return console_generic_do(true, out);
}

static bool console_handle_control_event(int ch, bool shifting)
{
	switch (ch)
	{
	case 'C':
		return console_copy();
	case 'V':
		return console_paste();
	case 'Y':
		return console_redo(NULL);
	case 'Z':
		return console_undo(NULL);

	case 'A':
		selecting = true;
		selection_begin = (coords_t){ 0, 0 };
		console_move_cursor((coords_t) { list_count(LIST_GET(lines, list_count(lines) - 1, line_t)->string), list_count(lines) - 1 });
		break;

	case 'O':
		char* buf = console_open_file_dialog(true);
		if (!buf)
			return true;
		file_details_t details = file_determine_and_open(buf);
		if (IS_BAD_DETAILS(details))
			return false;
		console_set_file_details(details);
		
		coords_t last_cursor = { 0 };
		list_t saves = user_get_latest().file_saves;
		for (int i = 0; i < list_count(saves); i++)
		{
			file_save_t* save = LIST_GET(saves, i, file_save_t);
			if (strncmp(save->directory, buf, MAX_PATH) == 0)
				last_cursor = save->cursor;
		}
		if (console_is_valid_cursor(last_cursor))
			console_move_cursor(last_cursor);
		else
			debug_format("Invalid cursor placement saved to user file.\n");
		return true;

	case 'S':
		char* dir = current_directory;
		if (!dir || shifting)
		{
			dir = console_open_file_dialog(false);
			if (!dir)
				return true;
		}
		debug_format("Saving file \"%s\" with type %i.\n", dir, current_type);
		return DEBUG_ON_FAILURE(user_save_file((file_save_t) { .directory = dir, .cursor = cursor })) &&
			DEBUG_ON_FAILURE(file_save((file_details_t) { .directory = dir, .lines = lines, .type = current_type }));
	}
	return true;
}

/* handles an arrow key action */
bool console_arrow_key(bool shifting, int dc, int dr)
{
	assert(console_is_created());
	coords_t new_cursor = cursor, begin, end;
	if (shifting && !selecting)
	{
		selection_begin = cursor;
		selecting = true;
	}
	else if (!shifting && console_get_selection_region(&begin, &end))
	{
		selecting = false;
		if (dc < 0 || dr < 0)
			new_cursor = begin;
		else
			new_cursor = end;
	}
	new_cursor = console_adjust_cursor(new_cursor, dc, dr);
	if (console_is_valid_cursor(new_cursor))
		return console_move_cursor(new_cursor);
	return true;
}

static bool console_add_char_to_line(int ch, coords_t coords)
{
	assert(console_is_created() && coords.row < list_count(lines));
	list_t string = LIST_GET(lines, coords.row, line_t)->string;
	assert(list_count(string) >= coords.column);
	if (ch == '\n')
	{
		return editor_add_newline(lines, coords) &&
			console_invalidate();
	}
	if (!LIST_ADD(string, (char)ch, coords.column))
		return false;

	console_fill_line(coords.row, user_attribute, ' ');
	console_draw_line(coords.row, user_attribute);
	DEBUG_ON_FAILURE(console_write_buffer());

	return true;
}

static bool console_remove_line(int pos)
{
	assert(console_is_created() && pos < list_count(lines));

	list_remove(lines, pos);
	for (int i = pos; i < camera.row + size.Y; i++)
	{
		console_fill_line(i, user_attribute, ' ');
		console_draw_line(i, user_attribute);
	}
	DEBUG_ON_FAILURE(console_write_buffer());
	return true;
}

static bool console_remove_char_from_line(coords_t coords)
{
	assert(console_is_created() && coords.row < list_count(lines));
	line_t* line = LIST_GET(lines, coords.row, line_t);
	assert(coords.column <= list_count(line->string));
	if (coords.column == list_count(line->string) && coords.row + 1 < list_count(lines))
	{
		bool result =
			list_concat(line->string, LIST_GET(lines, coords.row + 1, line_t)->string, list_count(line->string)) &&
			console_remove_line(coords.row + 1);
		console_draw_line(coords.row, user_attribute);
		DEBUG_ON_FAILURE(console_write_buffer());
		return result;
	}

	list_remove(((line_t*)LIST_GET(lines, coords.row, line_t))->string, coords.column);

	console_fill_line(coords.row, user_attribute, ' ');
	console_draw_line(coords.row, user_attribute);
	DEBUG_ON_FAILURE(console_write_buffer());

	return true;
}

static bool console_add_line(line_t line, int pos)
{
	assert(console_is_created() && pos < list_count(lines));
	char* str = LIST_GET_ARRAY(line.string, char);
	do
	{
		assert(!CHECK_FOR_NEWLINE(*str));
	} while (*str++);
	if (!LIST_ADD(lines, line, pos))
		return false;

	for (int i = pos; i < camera.row + size.Y; i++)
	{
		console_fill_line(i, user_attribute, ' ');
		console_draw_line(i, user_attribute);
	}
	DEBUG_ON_FAILURE(console_write_buffer());

	return true;
}

static bool console_act_delete_selection(void)
{
	coords_t start, end, prev = cursor;
	list_t str = list_create(sizeof(char));
	console_get_selection_region(&start, &end);
	if (!str)
		return false;

	if (!console_copy_selection_string(str) || !console_delete_selection())
	{
		list_destroy(str);
		return false;
	}

	char* str_copy = malloc(list_count(str));
	if (!str_copy)
	{
		list_destroy(str);
		return false;
	}
	strncpy(str_copy, (char*)list_element_array(str), list_count(str));
	list_destroy(str);
	action_t action = { .cursor = prev, .start = start, .end = end, .did_remove = true, .str = str_copy };
	if (!LIST_PUSH(actions, action))
		return debug_format("Failed to add delete action to buffer.\n");
	return true;
}

static bool console_act_delete_char(coords_t prev)
{
	char deleted = *LIST_GET(LIST_GET(lines, cursor.row, line_t)->string, cursor.column, char);

	if (!console_remove_char_from_line(cursor))
		return false;

	char* deleted_copy = malloc(sizeof(char) * 2);
	if (!deleted_copy)
		return debug_format("Failed to add delete action to buffer.\n");

	deleted_copy[0] = deleted;
	deleted_copy[1] = '\0';
	action_t action = { .cursor = prev, .start = cursor, .end = cursor, .did_remove = true, .str = deleted_copy };
	return LIST_PUSH(actions, action);
}

/* handles a DEL key command */
bool console_delete(void)
{
	assert(console_is_created());
	if (selecting)
		return console_act_delete_selection();
	else if (cursor.column < list_count(LIST_GET(lines, cursor.row, line_t)->string) || cursor.row + 1 < list_count(lines))
		return console_act_delete_char(cursor);
	return true;
}

/* handles a BKSPC key command */
bool console_backspace(void)
{
	assert(console_is_created());
	coords_t start = cursor;
	if (selecting)
		return console_act_delete_selection();
	else if (cursor.column != 0 || cursor.row != 0)
		return console_move_cursor(console_adjust_cursor(cursor, -1, 0)) &&
			console_act_delete_char(start);
	return true;
}

/* handles an enter/return key command */
bool console_return(void)
{
	assert(console_is_created());
	if (selecting)
		return console_act_delete_selection();
	else
	{
		char* newline = malloc(sizeof(char) * 2);
		if (newline)
		{
			newline[0] = '\n';
			newline[1] = '\0';
		}
		coords_t end = (coords_t){ 0, cursor.row + 1 };
		action_t action = { .cursor = cursor, .did_remove = false, .start = cursor, .end = cursor, .str = newline };
		if (!editor_add_newline(lines, cursor) || !console_move_cursor(end))
			return false;
		return LIST_PUSH(actions, action);
	}
}

/* handles a tab key command */
bool console_tab(void)
{
	assert(console_is_created());
	if (selecting && !console_act_delete_selection())
		return false;
	coords_t start = cursor, end = start;
	int tabc = TAB_SIZE - cursor.column % TAB_SIZE;
	char* tab_str = malloc(tabc + 1);
	if (!tab_str)
		return false;
	memset(tab_str, ' ', tabc);
	tab_str[tabc] = '\0';
	if (console_add_raw(tab_str, &end) && console_move_cursor(end))
	{
		action_t action = { .cursor = start, .did_remove = false, .start = start, .end = end, .str = tab_str };
		return LIST_PUSH(actions, action);
	}
	return false;
}

/* handles adding a character */
bool console_character(int ch)
{
	assert(console_is_created());
	bool was_selecting = selecting;
	if (selecting)
	{
		if (!console_act_delete_selection())
			return false;
	}
	coords_t start = cursor;
	if (console_add_char_to_line(ch, cursor) && console_move_cursor((coords_t) { cursor.column + 1, cursor.row }))
	{
		char* str = malloc(sizeof(char) * 2);
		if (!str)
			return false;
		str[0] = (char)ch;
		str[1] = '\0';
		action_t action = { .cursor = start, .did_remove = false, .start = start, .end = start, .str = str, .coupled = was_selecting };
		return LIST_PUSH(actions, action);
	}
	return false;
}

static bool console_handle_key_event(KEY_EVENT_RECORD ker)
{
	if (!ker.bKeyDown)
		return true;

	bool result = true;
	console_queue_changes();
	switch (ker.wVirtualKeyCode)
	{
	case VK_LCONTROL:
	case VK_RCONTROL:
	case VK_CONTROL:
		result = true;

	case VK_DOWN:
	case VK_UP:
		result = console_arrow_key(ker.dwControlKeyState & SHIFT_PRESSED, 0, ker.wVirtualKeyCode - 0x27);
		break;
	case VK_RIGHT:
	case VK_LEFT:
		result = console_arrow_key(ker.dwControlKeyState & SHIFT_PRESSED, ker.wVirtualKeyCode - 0x26, 0);
		break;
	case VK_DELETE:
		result = console_delete();
		break;
	case VK_BACK:
		result = console_backspace();
		break;
	case VK_RETURN:
		result = console_return();
		break;
	case VK_TAB:
		result = console_tab();
		break;

	default:
		if (!ker.uChar.AsciiChar)
			break; /* a virtual key code we don't handle */
		if (ker.dwControlKeyState & (LEFT_CTRL_PRESSED | RIGHT_CTRL_PRESSED))
			result = console_handle_control_event(ker.wVirtualKeyCode, ker.dwControlKeyState & SHIFT_PRESSED);
		else
			result = console_character(ker.uChar.AsciiChar);
		break;
	}

	if (!(ker.dwControlKeyState & (SHIFT_PRESSED | LEFT_CTRL_PRESSED | RIGHT_CTRL_PRESSED)) && selecting)
		selecting = false;
	console_submit_changes();
	return result;
}

static bool console_handle_mouse_event(MOUSE_EVENT_RECORD mer)
{ 
	static bool mouse_state = false;
	console_queue_changes();
	if (mer.dwEventFlags == 0) /* mouse down or up */
	{
		mouse_state = mer.dwButtonState != 0;
		if (mouse_state) /* mouse down event */
		{
			coords_t translated = { mer.dwMousePosition.X + camera.column, mer.dwMousePosition.Y + camera.row };
			translated = console_adjust_cursor(translated, 0, 0);
			console_move_cursor(translated);
			selection_begin = cursor;
		}
	}
	if (mouse_state && mer.dwEventFlags == MOUSE_MOVED)
	{
		selecting = true;
		coords_t translated = { mer.dwMousePosition.X + camera.column, mer.dwMousePosition.Y + camera.row };
		translated = console_adjust_cursor(translated, 0, 0);
		console_move_cursor(translated);
	}
	console_submit_changes();
	return true;
}

/* blocking function that parses and returns upon input */
bool console_poll_events(void)
{
	INPUT_RECORD record;
	DWORD read;
	if (!ReadConsoleInputA(input, &record, 1, &read))
		return false;
	assert(read == 1);
	DEBUG_ON_FAILURE(console_handle_potential_resize());
	switch (record.EventType)
	{
	case KEY_EVENT:
		DEBUG_ON_FAILURE(console_handle_key_event(record.Event.KeyEvent));
		break;
	case MOUSE_EVENT:
		DEBUG_ON_FAILURE(console_handle_mouse_event(record.Event.MouseEvent));
		break;
	}

	return true;
}

/* re-renders the screen */
bool console_invalidate(void)
{
	assert(console_is_created());
	if (!write_buffer)
		return true;

	for (int row = camera.row; row < camera.row + size.Y - 1; row++)
	{
		console_fill_line(row, user_attribute, ' ');
		console_draw_line(row, user_attribute);
	}
	
	coords_t temp;
	if (console_get_selection_region(&temp, &temp))
		console_draw_selection(user_attribute);
	console_draw_footer();

	return console_write_buffer();
}

/* moves cursor given delta parameters */
coords_t console_adjust_cursor(coords_t coords, int dc, int dr)
{
	if (dc == 0 && dr == 0)
	{
		coords.row = min(coords.row, list_count(lines) - 1);
		coords.column = min(coords.column, list_count(LIST_GET(lines, coords.row, line_t)->string));
		return coords;
	}

	if (dr > 0 || coords.row >= -dr)
	{
		coords.row = min(coords.row + dr, list_count(lines) - 1);
		coords.column = min(coords.column, list_count(LIST_GET(lines, coords.row, line_t)->string));
	}
	if (dc > 0 || coords.column >= -dc)
	{
		if (coords.column + dc > list_count(LIST_GET(lines, coords.row, line_t)->string))
		{
			coords.row++;
			coords.column = 0;
			dc--;
		}
		coords.column += dc;
	}
	else if (coords.row != 0) /* dc is negative */
	{
		coords.row--;
		coords.column = list_count(LIST_GET(lines, coords.row, line_t)->string) + dc + 1;
	}
	return coords;
}

/* physically moves cursor */
bool console_move_cursor(coords_t coords)
{
	assert(console_is_created() && console_is_valid_cursor(coords));
	if (!console_is_point_renderable(coords) || coords.row >= camera.row + size.Y - 1) /* accounting for footer */
	{
		if (camera.column > coords.column)
			camera.column = coords.column;
		else if (camera.column + size.X <= coords.column)
			camera.column = coords.column - size.X + 1;

		if (camera.row > coords.row)
			camera.row = coords.row;
		else if (camera.row + size.Y - 1 <= coords.row)
			camera.row = coords.row - size.Y + 2;
		console_invalidate();
	}
	cursor = coords;
	console_draw_footer();
	console_write_buffer();
	return !write_buffer || SetConsoleCursorPosition(output, (COORD) { (SHORT)(cursor.column - camera.column), (SHORT)(cursor.row - camera.row) });
}

/* returns whether or not a cursor position is valid */
bool console_is_valid_cursor(coords_t coords)
{
	assert(console_is_created());
	return editor_is_valid_cursor(lines, coords);
}

/* adds raw text to coordinates, formatting appropriately */
bool console_add_raw(const char* raw, coords_t* coords)
{
	assert(console_is_created() && raw != NULL && console_is_valid_cursor(*coords));
	if (!editor_add_raw(lines, raw, coords))
		return false;
	console_invalidate();
	return true;
}

/* returns false if there is no selection, otherwise sets pointers to cursor positions */
bool console_get_selection_region(coords_t* begin, coords_t* end)
{
	assert(console_is_created());
	if (selection_begin.row > cursor.row || (selection_begin.row == cursor.row && selection_begin.column > cursor.column))
	{
		*begin = cursor;
		*end = selection_begin;
	}
	else
	{
		*begin = selection_begin;
		*end = cursor;
	}
	bool is_selecting = selection_begin.row != cursor.row || selection_begin.column != cursor.column;
	*end = console_adjust_cursor(*end, -1, 0);
	return selecting && is_selecting;
}

/* sets contents of assumed empty list "str" to the contents of the selection */
bool console_copy_selection_string(list_t str)
{
	assert(console_is_created() && str != NULL &&list_count(str) == 0);
	coords_t begin_coords, end_coords;
	if (!console_get_selection_region(&begin_coords, &end_coords))
		return list_push_primitive(str, 0);
	return editor_copy_region(lines, str, begin_coords, end_coords);
}

/* deletes contents of selection */
bool console_delete_selection(void)
{
	assert(console_is_created());
	coords_t begin, end;
	if (!console_get_selection_region(&begin, &end))
		return true;

	editor_delete_region(lines, begin, end);
	selecting = false;
	console_move_cursor(begin);
	DEBUG_ON_FAILURE(console_write_buffer());
	return true;
}

/* clears action buffer entirely */
void console_clear_buffer(void)
{
	for (int i = 0; i < list_count(actions); i++)
		free(LIST_GET(actions, i, action_t)->str);
	for (int i = 0; i < list_count(undid_actions); i++)
		free(LIST_GET(undid_actions, i, action_t)->str);
	list_clear(actions);
	list_clear(undid_actions);
}

/*
	RENDER
*/

static inline void console_queue_changes(void)
{
	write_buffer = false;
}

static inline bool console_submit_changes(void)
{
	write_buffer = true;
	return console_move_cursor(cursor) && console_invalidate();
}

static inline bool console_is_point_renderable(coords_t coords)
{
	return camera.column <= coords.column && camera.column + size.X > coords.column &&
		camera.row <= coords.row && camera.row + size.Y > coords.row;
}

static inline CHAR_INFO* console_get_cell(int row, int col)
{
	SHORT tx = (SHORT)(col - camera.column), ty = (SHORT)(row - camera.row);
	assert(tx + ty * size.X < (size.X * size.Y));
	return &buffer[tx + ty * size.X];
}

static inline void console_set_cell(int row, int col, attribute_t attrib, int ch)
{
	if (!write_buffer || !console_is_point_renderable((coords_t) { col, row }))
		return;
	if (attrib != CONSOLE_DEFAULT_ATTRIBUTE)	console_get_cell(row, col)->Attributes = attrib;
	if (ch != CONSOLE_DEFAULT_CHAR)				console_get_cell(row, col)->Char.AsciiChar = ch;
}

static inline void console_draw_line(int row, attribute_t attrib)
{
	const line_t* line = (line_t*)list_get(lines, row);
	if (!write_buffer || !line)
		return;
	for (int col = camera.column; col < min(camera.column + size.X, list_count(line->string)); col++)
		console_set_cell(row, col, attrib, *LIST_GET(line->string, col, char));
}

static inline void console_draw_footer(void)
{
	if (!write_buffer)
		return;
	console_fill_line(camera.row + size.Y - 1, footer_attribute, ' ');
	console_set_stringf(camera.row + size.Y - 1, camera.column + 00, footer_attribute, "Cursor: (%i, %i)", cursor.column + 1, cursor.row + 1);
	console_set_stringf(camera.row + size.Y - 1, camera.column + 24, footer_attribute, "Camera: (%i, %i)", camera.column + 1, camera.row + 1);
	if (selecting)
	{
		static list_t buf;
		if (!buf && !(buf = list_create(sizeof(char))))
		{
			debug_format("Failed to allocate buffer for the \"characters selected\" footer element.\n");
			return;
		}
		list_clear(buf);
		DEBUG_ON_FAILURE(console_copy_selection_string(buf));
		console_set_stringf(camera.row + size.Y - 1, camera.column + 48, footer_attribute, "Characters selected: %i", list_count(buf) - 1); /* -1 for NUL character */
	}
}

static inline void console_fill_line(int row, attribute_t attrib, int ch)
{
	if (!write_buffer)
		return;
	for (int col = camera.column; col < camera.column + size.X; col++)
		console_set_cell(row, col, attrib, ch);
}

static void console_set_stringf(int row, int col, attribute_t attrib, const char* fmt, ...)
{
	if (!write_buffer)
		return;
	static char temp[1024];
	va_list args;
	va_start(args, fmt);
	vsnprintf(temp, sizeof temp, fmt, args);
	va_end(args);

	char* str = temp;
	for (; *str; str++, col++)
		console_set_cell(row, col, attrib, *str);
}

static void console_draw_selection(attribute_t attrib)
{
	if (!write_buffer)
		return;
	coords_t begin, end;
	if (console_get_selection_region(&begin, &end))
		attrib = CONSOLE_INVERT_ATTRIBUTE(attrib);
	else
		return;
	int first_line_end;
	if (begin.row != end.row)
	{
		first_line_end = list_count(LIST_GET(lines, begin.row, line_t)->string);
		for (int i = begin.row + 1; i < end.row; i++)
		{
			console_draw_line(i, attrib);
			console_set_cell(i, list_count(LIST_GET(lines, i, line_t)->string), attrib, CONSOLE_DEFAULT_CHAR);
		}
		for (int i = 0; i <= end.column; i++)
			console_set_cell(end.row, i, attrib, CONSOLE_DEFAULT_CHAR);
	}
	else
		first_line_end = end.column;

	for (int i = begin.column; i <= first_line_end; i++)
		console_set_cell(begin.row, i, attrib, CONSOLE_DEFAULT_CHAR);
}

static inline bool console_write_buffer(void)
{
	SMALL_RECT region = (SMALL_RECT){ 0, 0, size.X - 1, size.Y - 1 };
	return !write_buffer || WriteConsoleOutputA(output, buffer, size, (COORD) { 0, 0 }, & region);
}