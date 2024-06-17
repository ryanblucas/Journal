/*
	file.c ~ RL

	Saves, compresses, and encrypts files.
*/

#include "file.h"
#include <assert.h>
#include "console.h"
#include "editor.h"
#include <limits.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define DMC_MAGIC_HEADER		"dmc"
#define DMC_MAGIC_HEADER_LEN	3
#define DMC_EXTENSION			".dmc"
#define PLAIN_EXTENSION			".txt"
#define EXTENSION_LEN			4

/* does file exist */
bool file_exists(const char* directory)
{
	if (!directory)
		return false;
	FILE* file = fopen(directory, "r");
	if (!file)
		return false;
	fclose(file);
	return true;
}

/* gets just the name of a file, no directory */
bool file_get_name(const char* directory, char* buf, int size)
{
	assert(directory != NULL && buf != NULL && size > 0);
	const char* file_name = strrchr(directory, '\\');
	if (file_name)
		file_name++; /* strrchr returns ptr to last "\\", so we have to increment to get to the file name */
	else
		file_name = directory;
	strncpy(buf, file_name, size);
	return true;
}

/* gets file type */
bool file_get_type(const char* directory, file_type_t* type)
{
	assert(directory != NULL && type);
	FILE* file = fopen(directory, "r");
	char header[4];
	if (!file || fread(header, 1, 4, file) != 4)
		return false;
	if (file_extension_to_type(strrchr(directory, '.')) == TYPE_COMPRESSED && memcmp(header, DMC_MAGIC_HEADER, DMC_MAGIC_HEADER_LEN) == 0)
		*type = /*TYPE_COMPRESSED*/TYPE_UNKNOWN;
	else
		*type = TYPE_PLAIN;
	return true;
}

static bool file_raw_to_lines(const char* buf, long size, list_t lines)
{
	line_t current = { .string = list_create(sizeof(char)) };
	if (!current.string)
		return false;
	for (long i = 0; i < size; i++)
	{
		if (CHECK_FOR_NEWLINE(buf[i]))
		{
			list_t new_string = list_create(sizeof(char));
			if (!LIST_PUSH(lines, current) || !new_string)
				return false;
			current.string = new_string;
		}
		else if (buf[i] == '\t')
		{
			int tabc = TAB_SIZE - list_count(current.string) % TAB_SIZE;
			for (int j = 0; j < tabc; j++)
			{
				if (!list_push_primitive(current.string, (void*)' '))
					return false;
			}
		}
		else if (!LIST_PUSH(current.string, buf[i]))
			return false;
	}
	return LIST_PUSH(lines, current);
}

/* determines type of file and then delegates open function to the appropriate function */
file_details_t file_open(const char* directory, open_func_t func)
{
	assert(directory != NULL && func != NULL);
	FILE* file = fopen(directory, "r");
	if (!file)
		return FAILED_FILE_DETAILS;
	long size;
	char* buf = read_all_file(file, &size);
	fclose(file);
	if (!buf)
		return FAILED_FILE_DETAILS;

	list_t parsed = list_create(sizeof(char));
	if (!parsed || !func(buf, size, parsed))
	{
		list_destroy(parsed);
		free(buf);
		return FAILED_FILE_DETAILS;
	}
	free(buf);

	list_t lines = list_create(sizeof(line_t));
	if (!lines)
	{
		list_destroy(parsed);
		return FAILED_FILE_DETAILS;
	}

	bool result = file_raw_to_lines(list_element_array(parsed), size, lines);
	list_destroy(parsed);
	if (!result)
	{
		editor_destroy_lines(lines);
		return FAILED_FILE_DETAILS;
	}
	return (file_details_t) { .directory = directory, .lines = lines };
}

/* saves console's file given user's current settings */
bool file_save(const file_details_t details, save_func_t func)
{
	assert(details.directory != NULL);
	FILE* file = fopen(details.directory, "w");
	if (!file)
		return false;

	list_t str = list_create(sizeof(char));
	if (!str || !editor_copy_all_lines(details.lines, str))
	{
		list_destroy(str);
		fclose(file);
		return false;
	}

	bool result = func(file, list_element_array(str), list_count(str));

	fclose(file);
	if (!result)
		clear_file(details.directory);
	list_destroy(str);
	return result;
}

/* determines type of file to open */
file_details_t file_determine_and_open(const char* directory)
{
	file_type_t type;
	if (!file_get_type(directory, &type))
		return FAILED_FILE_DETAILS;
	return file_open(directory, file_type_to_open_func(type));
}

/* get file's extension given file type */
const char* file_type_to_extension(file_type_t type)
{
	switch (type)
	{
	case TYPE_PLAIN:
		return PLAIN_EXTENSION;
	case TYPE_COMPRESSED:
		return DMC_EXTENSION;
	}
	return NULL;
}

/* returns type from extension */
file_type_t file_extension_to_type(const char* ext)
{
	if (memcmp(ext, PLAIN_EXTENSION, EXTENSION_LEN) == 0)
		return TYPE_PLAIN;
	else if (memcmp(ext, DMC_EXTENSION, EXTENSION_LEN) == 0)
		return TYPE_COMPRESSED;
	return TYPE_UNKNOWN;
}

bool file_open_plain_func(const char* buf, long size, list_t out)
{
	/* TO DO: check BOM */
	list_t cpy = list_create_with_array(buf, sizeof(char), size);
	if (!cpy)
		return false;
	bool result = list_concat(out, cpy, 0);
	list_destroy(cpy);
	return result;
}

bool file_save_plain_func(FILE* file, const char* src, int size)
{
	return fwrite(src, 1, size - 1, file) == (size - 1);
}

/*	https://webhome.cs.uvic.ca/~nigelh/Publications/DMC.pd
	https://web.archive.org/web/20070630111546/http://plg.uwaterloo.ca/~ftp/dmc/dmc.c */

#define BIT_COUNT		CHAR_BIT
#define STRANDS			(1 << BIT_COUNT)
#define MIN_CNT1		2 /* Minimum "number of observed transitions from the current state to the candidate state" in order to clone */
#define MIN_CNT2		2 /* Minimum "number of observed transitions from all states other than the current state to the candidate state" in order to clone */
#define CLONE_COUNT		0x80000 /* sizeof(struct node) * CLONE_COUNT = 16mB */

struct node
{
	float count[2];		/* how many transistions occured out of this state */
	struct node* next[2];	/* next node(s) in tree */
};

struct dmc_state
{
	struct node* curr;
	struct node* clone_buf, *max_cb, *curr_cb; /* cb ~ Clone Buffer */
	struct node nodes[256][256];
};

/*	As described in the paper, the "braid" structure is best suited
	for byte-oriented data as it better remembers details between bytes.
	So, it's best to use it for a word processor. */
static void dmc_predictor_braid(struct dmc_state* state)
{
	/* TO DO: Adapt to BIT_COUNT and STRANDS macros */
	for (int j = 0; j < 256; j++)
	{
		/* 1-7th bit */
		for (int i = 0; i < 127; i++)
		{
			state->nodes[j][i].count[0] = 0.2;
			state->nodes[j][i].count[1] = 0.2;
			state->nodes[j][i].next[0] = &state->nodes[j][2 * i + 1];
			state->nodes[j][i].next[1] = &state->nodes[j][2 * i + 2];
		}

		/* 8th bit */
		for (int i = 127; i < 255; i++)
		{
			state->nodes[j][i].count[0] = 0.2;
			state->nodes[j][i].count[1] = 0.2;
			state->nodes[j][i].next[0] = &state->nodes[i + 1][0];
			state->nodes[j][i].next[1] = &state->nodes[i - 127][0];
		}
	}
	state->curr_cb = state->clone_buf;
	state->curr = &state->nodes[0][0];
}

/* Initializes clone buffer */
static bool dmc_predictor_init(struct dmc_state* state)
{
	state->clone_buf = malloc(sizeof * state->clone_buf * CLONE_COUNT);
	if (!state->clone_buf)
		return false;
	state->max_cb = state->clone_buf + CLONE_COUNT - 20; /* TO DO: why -20? */
	dmc_predictor_braid(state);
	return true;
}

/* returns chance for interval */
static int dmc_predictor(struct dmc_state* state)
{
	return state->curr->count[0] / (state->curr->count[0] + state->curr->count[1]);
}

static void dmc_predictor_update(struct dmc_state* state, bool bit)
{
	int i = (int)!!bit;
	struct node* p = state->curr;
	if (p->count[i] >= MIN_CNT1
		&& p->next[i]->count[0] + p->next[i]->count[1] >= MIN_CNT2 + p->count[i])
	{
		struct node* new = state->curr_cb++;
		float r = p->count[i] / (p->next[i]->count[1] + p->next[i]->count[0]);
		new->count[0] = p->next[i]->count[0] * r;
		new->count[1] = p->next[i]->count[1] * r;
		p->next[i]->count[0] -= new->count[0];
		p->next[i]->count[1] -= new->count[1];
		new->next[0] = p->next[i]->next[0];
		new->next[1] = p->next[i]->next[1];
		p->next[i] = new;
	}
	p->count[i]++;
	state->curr = p->next[i];
	if (state->curr_cb > state->max_cb)
	{
		debug_format("Ran out of predictor memory, flushing...\n");
		dmc_predictor_braid(state);
	}
}

bool file_open_dynamic_markov_model_func(const char* buf, long size, list_t out)
{
	assert(buf && size > 0 && out && list_element_size(out) == sizeof(char));
	{
		if (memcmp(buf, DMC_MAGIC_HEADER, DMC_MAGIC_HEADER_LEN) != 0)
			return false;
	}
	
	struct dmc_state* state = malloc(sizeof * state);
	if (!state || !dmc_predictor_init(state))
		return false;

	int max = 0x1000000,
		min = 0,
		mid;

	int in_bytes = 3,
		out_bytes = 0,
		pin = in_bytes; /* TO DO: what does pin represent? */

	int val;
	{
		char val_init[3];
		read_char(buf, 3, size, val_init);
		read_char(buf, 4, size, val_init);
		read_char(buf, 5, size, val_init);
		val = (val_init[0] << 16) + (val_init[1] << 8) + val_init[2];
	}

	for (int i = 6; i < size; i++)
	{
		if (val == (max - 1))
			break; /* EOF */
		int ch = 0;
		for (int j = 0; j < 8; j++)
		{
			mid = min + (max - min - 1) * dmc_predictor(state);
			if (mid == min)
				mid++;
			if (mid == (max - 1))
				mid--;
			bool bit = false;
			if (val >= mid)
			{
				bit = true;
				min = mid;
			}
			else
				max = mid;
			dmc_predictor_update(state, bit);
			ch = (ch << 1) + bit;
			while ((max - min) < 0x100)
			{
				if (bit)
					max--;
				in_bytes++;
				char nxt;
				read_char(buf, i, size, &nxt);
				val = (val << 8) & 0xFFFF00 | (nxt & 0xFF);
				min = (min << 8) & 0xFFFF00;
				max = (max << 8) & 0xFFFF00;
				if (min >= max)
					max = 0x1000000;
			}
		}
		LIST_PUSH(out, (char)ch);
		if (!(++out_bytes & 0xFF))
		{
			if (in_bytes - pin > 0x100)
				dmc_predictor_braid(state);
			pin = in_bytes;
		}
	}

	debug_format("Opened file compressed with Dynamic Markov Compression, in: %i, out: %i\n", in_bytes, out_bytes);
	return true;
}

bool file_save_dynamic_markov_model_func(FILE* file, const char* buf, int size)
{
	assert(file && buf && size > 0);
	
	fwrite(DMC_MAGIC_HEADER, 1, 3, file);
	
	struct dmc_state* state = malloc(sizeof * state);
	if (!state || !dmc_predictor_init(state))
		return false;
	
	/* interval variables */
	int max = 0x1000000,
		min = 0,
		mid;

	int in_bytes = 0, 
		out_bytes = 3, 
		pout = out_bytes; /* TO DO: what does pout represent? */

	for (int i = 0; i < size; i++)
	{
		for (int j = 0; j < BIT_COUNT; j++)
		{
			bool bit = (buf[i] << j) & 0x80;
			mid = min + (max - min - 1) * dmc_predictor(state);
			dmc_predictor_update(state, bit);
			if (mid == min)
				mid++;
			if (mid == (max - 1))
				mid--;
			if (bit)
				min = mid;
			else
				max = mid;

			while ((max - min) < 0x100)
			{
				if (bit)
					max--;
				write_char(file, min >> 16);
				out_bytes++;
				min = (min << 8) & 0xFFFF00;
				max = (max << 8) & 0xFFFF00;
				if (min >= max)
					max = 0x1000000;
			}
		}

		if (!(++in_bytes & 0xFF))
		{
			if (out_bytes - pout > 0x100)
				dmc_predictor_braid(state);
			pout = out_bytes;
		}
	}

	min = max - 1;
	write_char(file, min & 0xFF);
	write_char(file, (min >> 8) & 0xFF);
	write_char(file, (min >> 16) & 0xFF);

	free(state);
	debug_format("Compressed file with Dynamic Markov Compression, in: %i, out: %i\n", in_bytes, out_bytes);
	return true;
}