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

#define AES_EXTENSION			".aes"
#define DMC_EXTENSION			".dmc"
#define PLAIN_EXTENSION			".txt"
#define EXTENSION_LEN			4

static bool aes_open(const list_t in, list_t out);
static bool aes_save(const list_t in, list_t out);
static bool dmc_open(const list_t in, list_t out);
static bool dmc_save(const list_t in, list_t out);

static char aes_header[3] = { 0xAA, 0xEE, 0x17 };
static char dmc_header[3] = { 0xDD, 0x17, 0xCC };
static char* user_password;

/* sets password with a max len of 64 */
bool file_set_password(const char* password)
{
	if (!user_password)
	{
		user_password = malloc(64);
		if (!user_password)
			return false;
	}
	strncpy(user_password, password, 64);
	return true;
}

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
	FILE* file = fopen(directory, "rb");
	char header[4];
	if (!file)
		return false;
	bool read_all = fread(header, 1, 4, file) == 4;
	fclose(file);
	if (!read_all)
		return false;

	*type = TYPE_PLAIN;
	if (memcmp(header, dmc_header, sizeof dmc_header) == 0)
		*type = TYPE_COMPRESSED;
	return true;
}

/* determines type of file and then delegates open function to the appropriate function */
file_details_t file_open(const char* directory, file_type_t type)
{
	assert(directory != NULL);
	FILE* file = fopen(directory, "rb");
	if (!file)
		return FAILED_FILE_DETAILS;
	long size;
	char* buf = read_all_file(file, &size);
	fclose(file);
	if (!buf)
		return FAILED_FILE_DETAILS;

	list_t current = list_create_with_array(buf, sizeof(char), size);
	free(buf);
	if (!current)
		return FAILED_FILE_DETAILS;
	
	if (type & TYPE_ENCRYPTED)
	{
		list_t next = list_create(sizeof(char));
		if (!next || !aes_open(current, next))
		{
			list_destroy(next);
			list_destroy(current);
			return FAILED_FILE_DETAILS;
		}
		list_destroy(current);
		current = next;
	}

	if (type & TYPE_COMPRESSED)
	{
		list_t next = list_create(sizeof(char));
		if (!next || !dmc_open(current, next))
		{
			list_destroy(next);
			list_destroy(current);
			return FAILED_FILE_DETAILS;
		}
		list_destroy(current);
		current = next;
	}

	list_t lines = editor_create_lines();
	if (!lines)
	{
		list_destroy(current);
		return FAILED_FILE_DETAILS;
	}

	coords_t temp = { 0 };
	bool result = editor_format_raw(current)
		&& list_push_primitive(current, (void*)'\0')
		&& editor_add_raw(lines, list_element_array(current), &temp);

	list_destroy(current);
	if (!result)
	{
		editor_destroy_lines(lines);
		return FAILED_FILE_DETAILS;
	}
	return (file_details_t) { .directory = directory, .lines = lines };
}

/* saves console's file given user's current settings */
bool file_save(const file_details_t details, file_type_t type)
{
	assert(details.directory != NULL);
	FILE* file = fopen(details.directory, "wb");
	if (!file)
		return false;

	list_t current = list_create(sizeof(char));
	if (!current || !editor_copy_all_lines(details.lines, current))
	{
		list_destroy(current);
		fclose(file);
		return false;
	}

	list_pop(current, NULL);

	if (type & TYPE_COMPRESSED)
	{
		list_t next = list_create(sizeof(char));
		if (!next || !dmc_save(current, next))
		{
			list_destroy(current);
			fclose(file);
			clear_file(details.directory);
			return false;
		}
		current = next;
	}
	if (type & TYPE_ENCRYPTED)
	{
		list_t next = list_create(sizeof(char));
		if (!next || !aes_save(current, next))
		{
			list_destroy(current);
			fclose(file);
			clear_file(details.directory);
			return false;
		}
		current = next;
	}

	fwrite(list_element_array(current), 1, list_count(current), file);

	fclose(file);
	list_destroy(current);
	return true;
}

/* determines type of file to open */
file_details_t file_determine_and_open(const char* directory)
{
	file_type_t type;
	if (!file_get_type(directory, &type))
		return FAILED_FILE_DETAILS;
	return file_open(directory, type);
}

/* get file's extension given file type */
const char* file_type_to_extension(file_type_t type)
{
	switch (type)
	{
	case TYPE_COMPRESSED:
		return DMC_EXTENSION;
	case TYPE_ENCRYPTED:
		return AES_EXTENSION;
	case TYPE_COMPRESSED | TYPE_ENCRYPTED:
		return DMC_EXTENSION AES_EXTENSION;
	}
	return PLAIN_EXTENSION;
}

/* returns type from extension. You can also pass a file directory in */
file_type_t file_extension_to_type(const char* ext)
{
	const char* prev_last = NULL, *last = ext;
	while (*ext++)
	{
		if (*ext == '.')
		{
			prev_last = last;
			last = ext;
		}
	}

	file_type_t res = TYPE_PLAIN;

	if (memcmp(last, DMC_EXTENSION, EXTENSION_LEN) == 0)
		res |= TYPE_COMPRESSED;
	else if (memcmp(last, AES_EXTENSION, EXTENSION_LEN) == 0)
		res |= TYPE_ENCRYPTED;
	
	if (!prev_last)
		return res;

	if (memcmp(prev_last, DMC_EXTENSION, EXTENSION_LEN) == 0)
		res |= TYPE_COMPRESSED;
	else if (memcmp(prev_last, AES_EXTENSION, EXTENSION_LEN) == 0)
		res |= TYPE_ENCRYPTED;

	return res;
}

static bool aes_open(const list_t in, list_t out)
{
	assert(in && list_element_size(in) == sizeof(char) && out && list_element_size(out) == sizeof(char));
	return false;
}

static bool aes_save(const list_t in, list_t out)
{
	assert(in && list_element_size(in) == sizeof(char) && out && list_element_size(out) == sizeof(char));
	return false;
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
	float count[2];			/* how many transistions occured out of this state */
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
			state->nodes[j][i].count[0] = 0.2F;
			state->nodes[j][i].count[1] = 0.2F;
			state->nodes[j][i].next[0] = &state->nodes[j][2 * i + 1];
			state->nodes[j][i].next[1] = &state->nodes[j][2 * i + 2];
		}

		/* 8th bit */
		for (int i = 127; i < 255; i++)
		{
			state->nodes[j][i].count[0] = 0.2F;
			state->nodes[j][i].count[1] = 0.2F;
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
static float dmc_predictor(struct dmc_state* state)
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

#undef max
#undef min

static bool dmc_open(const list_t in, list_t out)
{
	assert(in && list_element_size(in) == sizeof(char) && out && list_element_size(out) == sizeof(char));
	
	int size = list_count(in);
	const char* buf = list_element_array(in);
	if (size < 6)
		return false;

	if (memcmp(buf, dmc_header, sizeof dmc_header) != 0)
		return false;
	
	struct dmc_state* state = malloc(sizeof * state);
	if (!state || !dmc_predictor_init(state))
		return false;

	int max = 0x1000000,
		min = 0,
		mid;

	int in_bytes = 3,
		out_bytes = 0,
		pin = in_bytes; /* TO DO: what does pin represent? */

	int val = (buf[3] << 16) + (buf[4] << 8) + buf[5];
	for (int i = 6; i < size; )
	{
		int ch = 0;
		for (int j = 0; j < 8; j++)
		{
			mid = (int)(min + (max - min - 1) * dmc_predictor(state));
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
				read_char(buf, i++, size, &nxt);
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

static bool dmc_save(const list_t in, list_t out)
{
	assert(in && list_element_size(in) == sizeof(char) && out && list_element_size(out) == sizeof(char));

	int size = list_count(in);
	const char* buf = list_element_array(in);

	if (!LIST_PUSH(out, dmc_header[0])
		|| !LIST_PUSH(out, dmc_header[1])
		|| !LIST_PUSH(out, dmc_header[2]))
		return false;
	
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
			bool bit = ((int)buf[i] << j) & 0x80;
			mid = (int)(min + (max - min - 1) * dmc_predictor(state));
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
				if (!list_push_primitive(out, (void*)(min >> 16)))
				{
					free(state);
					return false;
				}
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
	bool result = list_push_primitive(out, (void*)(min >> 16))
		&& list_push_primitive(out, (void*)((min >> 8) & 0xFF))
		&& list_push_primitive(out, (void*)(min & 0xFF));

	free(state);
	if (result)
		debug_format("Compressed file with Dynamic Markov Compression, in: %i, out: %i\n", in_bytes, out_bytes);
	return result;
}