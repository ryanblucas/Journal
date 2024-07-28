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
#define CONSOLE_MAX_PROMPT_LEN				80

typedef int attribute_t;
static attribute_t user_attribute = CONSOLE_CREATE_ATTRIBUTE(COLOR_LIGHT_GRAY, COLOR_BLACK);
static attribute_t footer_attribute = CONSOLE_CREATE_ATTRIBUTE(COLOR_WHITE, COLOR_DARK_GRAY);

static HANDLE input, output;
static COORD size;
static CHAR_INFO* buffer;
static coords_t cursor, camera;
static list_t lines;
static const char* footer_message;

static file_details_t current_file;
static char dir_buf[MAX_PATH];

static list_t actions;
static list_t undid_actions;

static bool selecting;
static coords_t selection_begin;

static int prompt_len;
static prompt_callback_t callback;
static list_t prev_lines;
static coords_t prev_cursor;

/* re-renders the screen */
static bool console_invalidate(void);

/* pauses application to ask user with prompt, calls callback when done and frees string passed after. */
void console_prompt_user(const char* prompt, prompt_callback_t _callback)
{
	assert(console_is_created());
	prev_lines = lines;
	lines = list_create(sizeof(line_t));
	line_t line = { .string = list_create_with_array(prompt, sizeof(char), (int)strnlen(prompt, CONSOLE_MAX_PROMPT_LEN)) };
	LIST_PUSH(lines, line);

	prompt_len = list_count(line.string);
	callback = _callback;
	prev_cursor = cursor;

	selecting = false;
	console_move_cursor((coords_t) { .column = list_count(line.string) });
}

/*	pauses application to ask user with prompt and series of possible choices.
	prompt is a string delimited by a newline. First line is the question, other lines are choices. */
void console_prompt_user_mc(const char* prompt, prompt_callback_t _callback)
{
	assert(console_is_created());
	prev_lines = lines;
	lines = editor_create_lines();
	coords_t temp = (coords_t){ 0 };
	editor_add_raw(lines, prompt, &temp);

	/* inline choices */
	for (int i = 1; i < list_count(lines); i++)
	{
		temp = (coords_t){ .column = 0, .row = i };
		editor_add_tab(lines, &temp);
	}

	console_move_cursor((coords_t) { .row = 1, .column = TAB_SIZE });

	prompt_len = list_count(LIST_GET(lines, 0, line_t)->string);
	callback = _callback;
	prev_cursor = cursor;
	selecting = false;
}

bool console_is_created(void)
{
	return !!output;
}

const file_details_t console_file(void)
{
	assert(console_is_created());
	return current_file;
}

list_t console_lines(void)
{
	assert(console_is_created());
	return lines;
}

coords_t console_cursor(void)
{
	assert(console_is_created());
	return cursor;
}

list_t console_actions(void)
{
	assert(console_is_created());
	return actions;
}

list_t console_undid_actions(void)
{
	assert(console_is_created());
	return undid_actions;
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
	editor_format_raw(temp);
	list_concat(str, temp, 0);
	list_destroy(temp);
	GlobalUnlock(buf_handle);
	CloseClipboard();
	return true;
}

static void console_set_title(const char* directory)
{
	char title_buf[128];
	char file_buf[128];
	file_get_name(directory, file_buf, sizeof file_buf);
	snprintf(title_buf, sizeof title_buf, "Journal - %s", file_buf);
	DEBUG_ON_FAILURE(SetConsoleTitleA(title_buf));
}

/* set console's file details. File details are copied on the console's end */
void console_set_file_details(const file_details_t details)
{
	assert(details.directory && details.lines && list_element_size(details.lines) == sizeof(line_t));
	for (int i = 0; i < list_count(details.lines); i++)
		assert(LIST_GET(details.lines, i, line_t)->string && list_element_size(LIST_GET(details.lines, i, line_t)->string) == sizeof(char));

	editor_destroy_lines(current_file.lines);
	list_clear(current_file.lines);
	list_concat(lines, details.lines, 0);
	console_move_cursor((coords_t) { 0, 0 });

	console_set_title(details.directory);

	strncpy(dir_buf, details.directory, sizeof dir_buf);
	current_file.directory = dir_buf;
	current_file.type = details.type;

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

/* set color and foreground of console */
void console_set_color(color_t foreground, color_t background)
{
	user_attribute = CONSOLE_CREATE_ATTRIBUTE(foreground, background);
	console_invalidate();
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
	CHAR_INFO* temp_buffer = journal_malloc(buffer_size);
	memset(temp_buffer, 0, buffer_size);

	input = temp_input;
	output = temp_output;
	buffer = temp_buffer;
	size = temp_size;
	return true;
}

/* destroys the console if it is created then creates the console */
bool console_create(void)
{
	if (console_is_created())
		console_destroy();
	if (!console_create_interface())
		return false;

	lines = editor_create_lines();
	actions = list_create(sizeof(action_t));
	undid_actions = list_create(sizeof(action_t));

	current_file.lines = lines;

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
	CHAR_INFO* temp = journal_malloc(sizeof * temp * csbi.dwSize.X * csbi.dwSize.Y);
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
	return DEBUG_ON_FAILURE(SetWindowPos(GetConsoleWindow(), NULL, 0, 0, fitted.right - fitted.left, fitted.bottom - fitted.top, SWP_NOMOVE));
}

static bool console_paste_selection(void)
{
	list_t str = list_create(sizeof(char));
	if (!console_clipboard(str))
	{
		list_destroy(str);
		return false;
	}

	editor_add_raw(lines, list_element_array(str), &cursor);
	console_move_cursor(editor_overflow_cursor(lines, (coords_t) { .column = cursor.column + 1, .row = cursor.row }));
	list_destroy(str);
	return true;
}

static char* console_open_file_dialog(bool does_file_exist)
{
	static char buf[MAX_PATH];

	OPENFILENAMEA settings =
	{
		/* OFN_DONTADDTORECENT ~ Plain text files can be opened by anyone, but compressed and encrypted can't. This wouldn't make sense for those files */
		.Flags =			OFN_DONTADDTORECENT | (does_file_exist ? (OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST) : 0),
		.lStructSize =		sizeof settings,
		.lpstrFilter =		"Journal Text Files (.txt, .dmc, .aes)\0*.txt;*.dmc;*.aes\0\0",
		.nFilterIndex =		1,
		.lpstrInitialDir =	current_file.directory,
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
	list_t str = list_create(sizeof(char));
	console_copy_selection_string(str);
	return console_set_clipboard(list_element_array(str), list_count(str));
}

static inline void console_commit_action(action_t action)
{
	LIST_PUSH(actions, action);
	list_clear(undid_actions);
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
	if (!console_clipboard(str) || !console_paste_selection())
	{
		list_destroy(str);
		return false;
	}
	char* str_copy = journal_malloc(list_count(str));
	strncpy(str_copy, list_element_array(str), list_count(str));
	list_destroy(str);
	console_commit_action((action_t) 
	{
		.coupled = was_selecting,
		.cursor = prev,
		.start = prev,
		.end = editor_overflow_cursor(lines, (coords_t) { .column = cursor.column - 1, .row = cursor.row }),
		.did_remove = false,
		.str = str_copy
	});
	return true;
}

static void console_generic_do(bool direction, action_t* out)
{
	assert(console_is_created());
	list_t buffer = direction ? undid_actions : actions, other = direction ? actions : undid_actions;
	if (list_count(buffer) <= 0)
		return;

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
			editor_add_raw(lines, curr.str, &temp);
		}
		console_move_cursor(curr.cursor);
		LIST_ADD(other, curr, other_add);
	} while (curr.coupled && (list_pop(buffer, &curr), true));
	if (out)
		*out = head;
}

/* puts action to undo in out (IF NOT NULL) and then undoes it */
void console_undo(action_t* out)
{
	assert(console_is_created());
	console_generic_do(false, out);
}

/* redoes last undo and puts that action in out (IF NOT NULL) */
void console_redo(action_t* out)
{
	assert(console_is_created());
	console_generic_do(true, out);
}

#define CONSOLE_COLOR_CHOICES "Black\nDark blue\nDark green\nDark cyan\nDark red\nDark purple\nDark yellow\nLight gray\nDark gray\nLight blue\nLight green\nLight cyan\nLight red\nLight purple\nLight yellow\nWhite"

static color_t foreground;

static inline color_t console_to_color(const char* color)
{
	/*	trailing new line because response_curr needs to be equal to 0; but if the user picks the last option, 
		response_curr will advance past that NUL terminator since choice_curr ends in that too */
	const char* choice_curr = CONSOLE_COLOR_CHOICES "\n";
	int i;
	for (i = 0; i < 16; i++)
	{
		const char* response_curr = color;
		while (*choice_curr != '\n')
		{
			if (*choice_curr++ == *response_curr)
				response_curr++;
		}
		choice_curr++;
		if (*response_curr == '\0')
			break;
	}
	assert(i < 16);
	return (color_t)i;
}

static void console_handle_background(const char* response)
{
	user_attribute = CONSOLE_CREATE_ATTRIBUTE(foreground, console_to_color(response));
}

static void console_handle_foreground(const char* response)
{
	foreground = console_to_color(response);
	console_prompt_user_mc("Background color:\n" CONSOLE_COLOR_CHOICES, console_handle_background);
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
		console_redo(NULL);
	case 'Z':
		console_undo(NULL);
	case 'P':
		console_prompt_user("Enter password: ", file_set_password);
		break;
	case 'U':
		console_prompt_user_mc("Text color:\n" CONSOLE_COLOR_CHOICES, console_handle_foreground);
		break;

	case 'A':
		selecting = true;
		selection_begin = (coords_t){ 0, 0 };
		console_move_cursor((coords_t) { list_count(LIST_GET(lines, list_count(lines) - 1, line_t)->string), list_count(lines) - 1 });
		break;

	case 'O':
		char* buf = console_open_file_dialog(true);
		if (!buf)
			return true;
		file_details_t details = file_open(buf);
		if (IS_BAD_DETAILS(details))
		{
			footer_message = "Failed to open file.";
			return false;
		}
		console_set_file_details(details);
		
		coords_t last_cursor = { 0 };
		list_t saves = user_get_latest().file_saves;
		for (int i = 0; i < list_count(saves); i++)
		{
			file_save_t* save = LIST_GET(saves, i, file_save_t);
			if (strncmp(save->directory, buf, MAX_PATH) == 0)
				last_cursor = save->cursor;
		}
		if (editor_is_valid_cursor(lines, last_cursor))
			console_move_cursor(last_cursor);
		else
			debug_format("Invalid cursor placement saved to user file.\n");
		footer_message = "Opened file.";
		return true;

	case 'S':
		if (!current_file.directory || shifting)
		{
			const char* dir = console_open_file_dialog(false);
			if (!dir)
				return true;
			current_file.type = file_extension_to_type(dir);
			strncpy(dir_buf, dir, MAX_PATH);
			current_file.directory = dir_buf;
			console_set_title(current_file.directory);
		}
		debug_format("Saving file \"%s\" with type %i.\n", current_file.directory, current_file.type);
		bool result = DEBUG_ON_FAILURE(user_save_file((file_save_t) { .directory = current_file.directory, .cursor = cursor })) &&
			DEBUG_ON_FAILURE(file_save(current_file));
		footer_message = "Saved file.";
		if (!result)
			footer_message = "Failed to save file.";
		return result;
	}
	return true;
}

/* handles an arrow key action */
void console_arrow_key(bool shifting, int dc, int dr)
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
	new_cursor = editor_overflow_cursor(lines, (coords_t) { .column = new_cursor.column + dc, .row = new_cursor.row });
	/* increment after overflow, otherwise if the line is too short, it will overflow into another row */
	new_cursor.row += dr;
	console_move_cursor(new_cursor);
}

static void console_act_delete_selection(void)
{
	coords_t start, end, prev = cursor;
	list_t str = list_create(sizeof(char));
	console_get_selection_region(&start, &end);
	console_copy_selection_string(str);
	console_delete_selection();

	char* str_copy = journal_malloc(list_count(str));
	strncpy(str_copy, (char*)list_element_array(str), list_count(str));
	list_destroy(str);
	action_t action = { .cursor = prev, .start = start, .end = end, .did_remove = true, .str = str_copy };
	console_commit_action(action);
}

static void console_act_delete_char(coords_t prev)
{
	char newline = '\n';
	char* deleted = LIST_GET(LIST_GET(lines, cursor.row, line_t)->string, cursor.column, char);
	if (!deleted)
		deleted = &newline;

	line_t* line = LIST_GET(lines, cursor.row, line_t);
	if (cursor.column == list_count(line->string) && cursor.row + 1 < list_count(lines))
	{
		list_concat(line->string, LIST_GET(lines, cursor.row + 1, line_t)->string, list_count(line->string));
		list_remove(lines, cursor.row + 1);
	}
	else
		list_remove(((line_t*)LIST_GET(lines, cursor.row, line_t))->string, cursor.column);

	char* deleted_copy = journal_malloc(sizeof(char) * 2);
	deleted_copy[0] = *deleted;
	deleted_copy[1] = '\0';
	action_t action = { .cursor = prev, .start = cursor, .end = cursor, .did_remove = true, .str = deleted_copy };
	console_commit_action(action);
}

/* handles a DEL key command */
void console_delete(void)
{
	assert(console_is_created());
	if (selecting)
		console_act_delete_selection();
	else if (cursor.column < list_count(LIST_GET(lines, cursor.row, line_t)->string) || cursor.row + 1 < list_count(lines))
		console_act_delete_char(cursor);
}

/* handles a BKSPC key command */
void console_backspace(void)
{
	assert(console_is_created());
	coords_t start = cursor;
	if (selecting)
		console_act_delete_selection();
	else if (cursor.column != 0 || cursor.row != 0)
	{
		cursor = editor_overflow_cursor(lines, (coords_t) { .column = cursor.column - 1, .row = cursor.row });
		console_act_delete_char(start);
	}
}

/* handles an enter/return key command */
void console_return(void)
{
	assert(console_is_created());
	if (selecting)
		console_act_delete_selection();
	else
	{
		char* newline = journal_malloc(sizeof(char) * 2);
		newline[0] = '\n';
		newline[1] = '\0';
		coords_t end = (coords_t){ 0, cursor.row + 1 };
		action_t action = { .cursor = cursor, .did_remove = false, .start = cursor, .end = cursor, .str = newline };
		editor_add_newline(lines, cursor);
		console_move_cursor(end);
		console_commit_action(action);
	}
}

/* handles a tab key command */
void console_tab(void)
{
	assert(console_is_created());
	if (selecting)
		console_act_delete_selection();
	coords_t start = cursor, end = start;
	int tabc = TAB_SIZE - cursor.column % TAB_SIZE;
	char* tab_str = journal_malloc(tabc + 1);
	memset(tab_str, ' ', tabc);
	tab_str[tabc] = '\0';
	editor_add_raw(lines, tab_str, &end);

	console_move_cursor(end);
	action_t action = { .cursor = start, .did_remove = false, .start = start, .end = end, .str = tab_str };
	console_commit_action(action);
}

/* handles adding a character */
void console_character(int ch)
{
	assert(console_is_created());
	bool was_selecting = selecting;
	if (selecting)
		console_act_delete_selection();
	coords_t start = cursor;
	if (ch == '\n')
		editor_add_newline(lines, cursor);
	LIST_ADD(LIST_GET(lines, cursor.row, line_t)->string, (char)ch, cursor.column);
	console_move_cursor((coords_t) { cursor.column + 1, cursor.row });
	char* str = journal_malloc(sizeof(char) * 2);
	str[0] = (char)ch;
	str[1] = '\0';
	action_t action = { .cursor = start, .did_remove = false, .start = start, .end = start, .str = str, .coupled = was_selecting };
	console_commit_action(action);
}

static bool console_handle_key_event(KEY_EVENT_RECORD ker)
{
	if (!ker.bKeyDown)
		return true;

	bool result = true;
	switch (ker.wVirtualKeyCode)
	{
	case VK_LCONTROL:
	case VK_RCONTROL:
	case VK_CONTROL:
		result = true;
		break;

	case VK_DOWN:
	case VK_UP:
		console_arrow_key(ker.dwControlKeyState & SHIFT_PRESSED, 0, ker.wVirtualKeyCode - 0x27);
		break;
	case VK_RIGHT:
	case VK_LEFT:
		console_arrow_key(ker.dwControlKeyState & SHIFT_PRESSED, ker.wVirtualKeyCode - 0x26, 0);
		break;
	case VK_DELETE:
		console_delete();
		break;
	case VK_BACK:
		console_backspace();
		break;
	case VK_RETURN:
		console_return();
		break;
	case VK_TAB:
		console_tab();
		break;

	default:
		if (!ker.uChar.AsciiChar)
			break; /* a virtual key code we don't handle */
		if (ker.dwControlKeyState & (LEFT_CTRL_PRESSED | RIGHT_CTRL_PRESSED))
			result = console_handle_control_event(ker.wVirtualKeyCode, ker.dwControlKeyState & SHIFT_PRESSED);
		else
			console_character(ker.uChar.AsciiChar);
		break;
	}

	if (!(ker.dwControlKeyState & (SHIFT_PRESSED | LEFT_CTRL_PRESSED | RIGHT_CTRL_PRESSED)) && selecting)
		selecting = false;
	return result;
}

static void console_handle_mouse_event(MOUSE_EVENT_RECORD mer)
{ 
	static bool mouse_state = false;
	if (mer.dwEventFlags == 0) /* mouse down or up */
	{
		mouse_state = mer.dwButtonState != 0;
		if (mouse_state) /* mouse down event */
		{
			console_move_cursor((coords_t) { mer.dwMousePosition.X + camera.column, mer.dwMousePosition.Y + camera.row });
			selection_begin = cursor;
		}
	}
	if (mouse_state && mer.dwEventFlags == MOUSE_MOVED)
	{
		selecting = true;
		console_move_cursor((coords_t) { mer.dwMousePosition.X + camera.column, mer.dwMousePosition.Y + camera.row });
	}
}

static bool console_handle_mcq_prompt(INPUT_RECORD record)
{
	switch (record.EventType)
	{
	case KEY_EVENT:
	{
		if (!record.Event.KeyEvent.bKeyDown)
			break;

		int vk = record.Event.KeyEvent.wVirtualKeyCode;
		if (vk == VK_UP || vk == VK_DOWN)
			console_arrow_key(false, 0, vk - 0x27);
		else if (vk == VK_LEFT || vk == VK_RIGHT)
			console_arrow_key(false, vk - 0x26, 0);
		else if (vk == VK_RETURN && cursor.row != 0) /* row 0 = the question line */
		{
			/* delete choices beyond the one selected */
			editor_delete_region(lines, (coords_t) 
			{ 
				.column = list_count(LIST_GET(lines, cursor.row, line_t)->string), 
				.row = cursor.row 
			}, (coords_t) 
			{ 
				.column = list_count(LIST_GET(lines, list_count(lines) - 1, line_t)->string) - 1,
				.row = list_count(lines) - 1
			});
			/* delete choices before but keep the prompt */
			editor_delete_region(lines, (coords_t) { .column = prompt_len, .row = 0 }, (coords_t) { .column = TAB_SIZE - 1, .row = cursor.row });
			return true;
		}
		break;
	}
	case MOUSE_EVENT:
		console_handle_mouse_event(record.Event.MouseEvent);
		selecting = false;
		break;
	}
	return false;
}

static bool console_handle_response_prompt(INPUT_RECORD record)
{
	switch (record.EventType)
	{
	case KEY_EVENT:
	{
		DEBUG_ON_FAILURE(console_handle_key_event(record.Event.KeyEvent));
		if (record.Event.KeyEvent.wVirtualKeyCode == VK_RETURN)
			list_remove(lines, cursor.row); /* remove new line */
		break;
	}
	case MOUSE_EVENT:
		console_handle_mouse_event(record.Event.MouseEvent);
		break;
	}

	cursor.column = max(cursor.column, prompt_len);
	selection_begin.column = max(selection_begin.column, prompt_len);
	return cursor.row >= 1;
}

/* returns once user escapes */
void console_loop(void)
{
	INPUT_RECORD record = { 0 };
	DWORD read;

	while (ReadConsoleInputA(input, &record, 1, &read)
		&& (record.EventType != KEY_EVENT || record.Event.KeyEvent.wVirtualKeyCode != VK_ESCAPE))
	{
		assert(read == 1);
		DEBUG_ON_FAILURE(console_handle_potential_resize());

		if (callback)
		{
			/* if prompting the user a multiple choice, list count will always be > 1 */
			if (((list_count(lines) <= 1 && console_handle_response_prompt(record))
				|| (list_count(lines) > 1 && console_handle_mcq_prompt(record))))
			{
				list_t str = list_create(sizeof(char));
				editor_copy_all_lines(lines, str);
				editor_destroy_lines(lines);
				list_destroy(lines);
				lines = prev_lines;
				console_move_cursor(prev_cursor);
				prev_lines = NULL;

				prompt_callback_t curr = callback;
				curr((char*)list_element_array(str) + prompt_len);
				list_destroy(str);

				if (curr == callback)
					callback = NULL;
			}
		}
		else if (record.EventType == KEY_EVENT)
			DEBUG_ON_FAILURE(console_handle_key_event(record.Event.KeyEvent));
		else if (record.EventType == MOUSE_EVENT)
			console_handle_mouse_event(record.Event.MouseEvent);

		console_invalidate();
	}
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
	*end = editor_overflow_cursor(lines, (coords_t) { .column = end->column - 1, .row = end->row });
	return selecting && is_selecting;
}

/* sets contents of assumed empty list "str" to the contents of the selection */
void console_copy_selection_string(list_t str)
{
	assert(console_is_created() && str != NULL && list_count(str) == 0);
	coords_t begin_coords, end_coords;
	if (!console_get_selection_region(&begin_coords, &end_coords))
	{
		list_push_primitive(str, 0);
		return;
	}
	editor_copy_region(lines, str, begin_coords, end_coords);
}

/* deletes contents of selection */
void console_delete_selection(void)
{
	assert(console_is_created());
	coords_t begin, end;
	if (!console_get_selection_region(&begin, &end))
		return;

	editor_delete_region(lines, begin, end);
	selecting = false;
	console_move_cursor(begin);
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
	if (!console_is_point_renderable((coords_t) { col, row }))
		return;
	if (attrib != CONSOLE_DEFAULT_ATTRIBUTE)	console_get_cell(row, col)->Attributes = attrib;
	if (ch != CONSOLE_DEFAULT_CHAR)				console_get_cell(row, col)->Char.AsciiChar = ch;
}

static inline void console_draw_line(int row, attribute_t attrib)
{
	const line_t* line = (line_t*)list_get(lines, row);
	if (!line)
		return;
	for (int col = camera.column; col < min(camera.column + size.X, list_count(line->string)); col++)
		console_set_cell(row, col, attrib, *LIST_GET(line->string, col, char));
}

static inline void console_fill_line(int row, attribute_t attrib, int ch)
{
	for (int col = camera.column; col < camera.column + size.X; col++)
		console_set_cell(row, col, attrib, ch);
}

static void console_set_stringf(int row, int col, attribute_t attrib, const char* fmt, ...)
{
	static char temp[1024];
	va_list args;
	va_start(args, fmt);
	vsnprintf(temp, sizeof temp, fmt, args);
	va_end(args);

	char* str = temp;
	for (; *str; str++, col++)
		console_set_cell(row, col, attrib, *str);
}

static inline void console_draw_footer(void)
{
	console_fill_line(camera.row + size.Y - 1, footer_attribute, ' ');
	console_set_stringf(camera.row + size.Y - 1, camera.column + 00, footer_attribute, "Cursor: (%i, %i)", cursor.column + 1, cursor.row + 1);
	console_set_stringf(camera.row + size.Y - 1, camera.column + 24, footer_attribute, "Camera: (%i, %i)", camera.column + 1, camera.row + 1);
	if (selecting)
	{
		static list_t buf;
		if (!buf)
			buf = list_create(sizeof(char));
		list_clear(buf);
		console_copy_selection_string(buf);
		console_set_stringf(camera.row + size.Y - 1, camera.column + 48, footer_attribute, "Characters selected: %i", list_count(buf) - 1); /* -1 for NUL character */
	}
	if (footer_message)
	{
		console_set_stringf(camera.row + size.Y - 1, camera.column + 80, footer_attribute, "%s", footer_message);
		footer_message = NULL;
	}
}

static void console_draw_selection(attribute_t attrib)
{
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
	return WriteConsoleOutputA(output, buffer, size, (COORD) { 0, 0 }, &region) 
		&& SetConsoleCursorPosition(output, (COORD) { (SHORT)(cursor.column - camera.column), (SHORT)(cursor.row - camera.row) });
}

/* physically moves cursor */
void console_move_cursor(coords_t coords)
{
	assert(console_is_created());
	coords.row = min(max(0, coords.row), list_count(lines) - 1);
	coords.column = min(max(0, coords.column), list_count(LIST_GET(lines, coords.row, line_t)->string));
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
	}
	cursor = coords;
}

/* re-renders the screen */
static bool console_invalidate(void)
{
	assert(console_is_created());
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