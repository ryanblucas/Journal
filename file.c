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
static char user_password[64] = { 0 };

/* sets password with a max len of 64 */
void file_set_password(const char* password)
{
	memset(user_password, 0, sizeof user_password);
	strncpy(user_password, password, sizeof user_password);
#if _DEBUG
	debug_format("Set password to \"%s\"\n", user_password);
#endif
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

static file_type_t file_read_header(const list_t buffer)
{
	file_type_t type = TYPE_PLAIN;
	if (list_count(buffer) >= 3)
	{
		if (memcmp(list_element_array(buffer), dmc_header, sizeof dmc_header) == 0)
			type |= TYPE_COMPRESSED;
		else if (memcmp(list_element_array(buffer), aes_header, sizeof aes_header) == 0)
			type |= TYPE_ENCRYPTED;
	}
	return type;
}

/* determines type of file and then delegates open function to the appropriate function */
file_details_t file_open(const char* directory)
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

	file_type_t type = file_read_header(current);
	if (type & TYPE_ENCRYPTED)
	{
		list_t next = list_create(sizeof(char));
		if (!aes_open(current, next))
		{
			list_destroy(next);
			list_destroy(current);
			return FAILED_FILE_DETAILS;
		}
		list_destroy(current);
		current = next;
	}

	type |= file_read_header(current);
	if (type & TYPE_COMPRESSED)
	{
		list_t next = list_create(sizeof(char));
		if (!dmc_open(current, next))
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
	bool result = editor_format_raw(current);
	list_push_primitive(current, (void*)'\0');
	result &= editor_add_raw(lines, list_element_array(current), &temp);

	list_destroy(current);
	if (!result)
	{
		editor_destroy_lines(lines);
		return FAILED_FILE_DETAILS;
	}
	return (file_details_t) { .directory = directory, .lines = lines, .type = type };
}

/* saves console's file given user's current settings */
bool file_save(const file_details_t details)
{
	assert(details.directory != NULL);
	FILE* file = fopen(details.directory, "wb");
	if (!file)
		return false;

	list_t current = list_create(sizeof(char));
	if (!editor_copy_all_lines(details.lines, current))
	{
		list_destroy(current);
		fclose(file);
		return false;
	}

	list_pop(current, NULL);

	if (details.type & TYPE_COMPRESSED)
	{
		list_t next = list_create(sizeof(char));
		if (!dmc_save(current, next))
		{
			list_destroy(current);
			fclose(file);
			clear_file(details.directory);
			return false;
		}
		current = next;
	}
	if (details.type & TYPE_ENCRYPTED)
	{
		list_t next = list_create(sizeof(char));
		if (!aes_save(current, next))
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

/*	https://github.com/m3y54m/aes-in-c
	https://en.wikipedia.org/wiki/Rijndael_MixColumns
	https://en.wikipedia.org/wiki/Finite_field_arithmetic#Rijndael's_(AES)_finite_field */

/* KEY_SIZE can be 16, 24, or 32. 16 is the only tested constant. */
#define KEY_SIZE		16
#define EXP_KEY_SIZE	176
#define STATE_SIZE		16 /* state is a 4x4 matrix, row-major */
/* from "The Design of Rjindael:" "For Rijndael versions with a longer key, the number of rounds was raised by one for every additional 32 bits in the cipher key." */
#define ROUND_COUNT		(6 + (KEY_SIZE / 4))
#define AES_FILE_OFFSET	(sizeof aes_header + 16)

static uint8_t aes_sbox[] =
{
	0x63, 0x7C, 0x77, 0x7B, 0xF2, 0x6B, 0x6F, 0xC5, 0x30, 0x01, 0x67, 0x2B, 0xFE, 0xD7, 0xAB, 0x76,
	0xCA, 0x82, 0xC9, 0x7D, 0xFA, 0x59, 0x47, 0xF0, 0xAD, 0xD4, 0xA2, 0xAF, 0x9C, 0xA4, 0x72, 0xC0,
	0xB7, 0xFD, 0x93, 0x26, 0x36, 0x3F, 0xF7, 0xCC, 0x34, 0xA5, 0xE5, 0xF1, 0x71, 0xD8, 0x31, 0x15,
	0x04, 0xC7, 0x23, 0xC3, 0x18, 0x96, 0x05, 0x9A, 0x07, 0x12, 0x80, 0xE2, 0xEB, 0x27, 0xB2, 0x75,
	0x09, 0x83, 0x2C, 0x1A, 0x1B, 0x6E, 0x5A, 0xA0, 0x52, 0x3B, 0xD6, 0xB3, 0x29, 0xE3, 0x2F, 0x84,
	0x53, 0xD1, 0x00, 0xED, 0x20, 0xFC, 0xB1, 0x5B, 0x6A, 0xCB, 0xBE, 0x39, 0x4A, 0x4C, 0x58, 0xCF,
	0xD0, 0xEF, 0xAA, 0xFB, 0x43, 0x4D, 0x33, 0x85, 0x45, 0xF9, 0x02, 0x7F, 0x50, 0x3C, 0x9F, 0xA8,
	0x51, 0xA3, 0x40, 0x8F, 0x92, 0x9D, 0x38, 0xF5, 0xBC, 0xB6, 0xDA, 0x21, 0x10, 0xFF, 0xF3, 0xD2,
	0xCD, 0x0C, 0x13, 0xEC, 0x5F, 0x97, 0x44, 0x17, 0xC4, 0xA7, 0x7E, 0x3D, 0x64, 0x5D, 0x19, 0x73,
	0x60, 0x81, 0x4F, 0xDC, 0x22, 0x2A, 0x90, 0x88, 0x46, 0xEE, 0xB8, 0x14, 0xDE, 0x5E, 0x0B, 0xDB,
	0xE0, 0x32, 0x3A, 0x0A, 0x49, 0x06, 0x24, 0x5C, 0xC2, 0xD3, 0xAC, 0x62, 0x91, 0x95, 0xE4, 0x79,
	0xE7, 0xC8, 0x37, 0x6D, 0x8D, 0xD5, 0x4E, 0xA9, 0x6C, 0x56, 0xF4, 0xEA, 0x65, 0x7A, 0xAE, 0x08,
	0xBA, 0x78, 0x25, 0x2E, 0x1C, 0xA6, 0xB4, 0xC6, 0xE8, 0xDD, 0x74, 0x1F, 0x4B, 0xBD, 0x8B, 0x8A,
	0x70, 0x3E, 0xB5, 0x66, 0x48, 0x03, 0xF6, 0x0E, 0x61, 0x35, 0x57, 0xB9, 0x86, 0xC1, 0x1D, 0x9E,
	0xE1, 0xF8, 0x98, 0x11, 0x69, 0xD9, 0x8E, 0x94, 0x9B, 0x1E, 0x87, 0xE9, 0xCE, 0x55, 0x28, 0xDF,
	0x8C, 0xA1, 0x89, 0x0D, 0xBF, 0xE6, 0x42, 0x68, 0x41, 0x99, 0x2D, 0x0F, 0xB0, 0x54, 0xBB, 0x16
};

static uint8_t aes_sbox_invert[] =
{
	0x52, 0x09, 0x6A, 0xD5, 0x30, 0x36, 0xA5, 0x38, 0xBF, 0x40, 0xA3, 0x9E, 0x81, 0xF3, 0xD7, 0xFB,
	0x7C, 0xE3, 0x39, 0x82, 0x9B, 0x2F, 0xFF, 0x87, 0x34, 0x8E, 0x43, 0x44, 0xC4, 0xDE, 0xE9, 0xCB,
	0x54, 0x7B, 0x94, 0x32, 0xA6, 0xC2, 0x23, 0x3D, 0xEE, 0x4C, 0x95, 0x0B, 0x42, 0xFA, 0xC3, 0x4E,
	0x08, 0x2E, 0xA1, 0x66, 0x28, 0xD9, 0x24, 0xB2, 0x76, 0x5B, 0xA2, 0x49, 0x6D, 0x8B, 0xD1, 0x25,
	0x72, 0xF8, 0xF6, 0x64, 0x86, 0x68, 0x98, 0x16, 0xD4, 0xA4, 0x5C, 0xCC, 0x5D, 0x65, 0xB6, 0x92,
	0x6C, 0x70, 0x48, 0x50, 0xFD, 0xED, 0xB9, 0xDA, 0x5E, 0x15, 0x46, 0x57, 0xA7, 0x8D, 0x9D, 0x84,
	0x90, 0xD8, 0xAB, 0x00, 0x8C, 0xBC, 0xD3, 0x0A, 0xF7, 0xE4, 0x58, 0x05, 0xB8, 0xB3, 0x45, 0x06,
	0xD0, 0x2C, 0x1E, 0x8F, 0xCA, 0x3F, 0x0F, 0x02, 0xC1, 0xAF, 0xBD, 0x03, 0x01, 0x13, 0x8A, 0x6B,
	0x3A, 0x91, 0x11, 0x41, 0x4F, 0x67, 0xDC, 0xEA, 0x97, 0xF2, 0xCF, 0xCE, 0xF0, 0xB4, 0xE6, 0x73,
	0x96, 0xAC, 0x74, 0x22, 0xE7, 0xAD, 0x35, 0x85, 0xE2, 0xF9, 0x37, 0xE8, 0x1C, 0x75, 0xDF, 0x6E,
	0x47, 0xF1, 0x1A, 0x71, 0x1D, 0x29, 0xC5, 0x89, 0x6F, 0xB7, 0x62, 0x0E, 0xAA, 0x18, 0xBE, 0x1B,
	0xFC, 0x56, 0x3E, 0x4B, 0xC6, 0xD2, 0x79, 0x20, 0x9A, 0xDB, 0xC0, 0xFE, 0x78, 0xCD, 0x5A, 0xF4,
	0x1F, 0xDD, 0xA8, 0x33, 0x88, 0x07, 0xC7, 0x31, 0xB1, 0x12, 0x10, 0x59, 0x27, 0x80, 0xEC, 0x5F,
	0x60, 0x51, 0x7F, 0xA9, 0x19, 0xB5, 0x4A, 0x0D, 0x2D, 0xE5, 0x7A, 0x9F, 0x93, 0xC9, 0x9C, 0xEF,
	0xA0, 0xE0, 0x3B, 0x4D, 0xAE, 0x2A, 0xF5, 0xB0, 0xC8, 0xEB, 0xBB, 0x3C, 0x83, 0x53, 0x99, 0x61,
	0x17, 0x2B, 0x04, 0x7E, 0xBA, 0x77, 0xD6, 0x26, 0xE1, 0x69, 0x14, 0x63, 0x55, 0x21, 0x0C, 0x7D
};

static uint8_t aes_rcon[] = 
{ 
	0x8D, 0x01, 0x02, 0x04, 0x08, 0x10, 0x20, 0x40, 0x80, 0x1B, 0x36, 0x6C, 0xD8, 0xAB, 0x4D, 0x9A,
	0x2F, 0x5E, 0xBC, 0x63, 0xC6, 0x97, 0x35, 0x6A, 0xD4, 0xB3, 0x7D, 0xFA, 0xEF, 0xC5, 0x91, 0x39,
	0x72, 0xE4, 0xD3, 0xBD, 0x61, 0xC2, 0x9F, 0x25, 0x4A, 0x94, 0x33, 0x66, 0xCC, 0x83, 0x1D, 0x3A,
	0x74, 0xE8, 0xCB, 0x8D, 0x01, 0x02, 0x04, 0x08, 0x10, 0x20, 0x40, 0x80, 0x1B, 0x36, 0x6C, 0xD8,
	0xAB, 0x4D, 0x9A, 0x2F, 0x5E, 0xBC, 0x63, 0xC6, 0x97, 0x35, 0x6A, 0xD4, 0xB3, 0x7D, 0xFA, 0xEF,
	0xC5, 0x91, 0x39, 0x72, 0xE4, 0xD3, 0xBD, 0x61, 0xC2, 0x9F, 0x25, 0x4A, 0x94, 0x33, 0x66, 0xCC,
	0x83, 0x1D, 0x3A, 0x74, 0xE8, 0xCB, 0x8D, 0x01, 0x02, 0x04, 0x08, 0x10, 0x20, 0x40, 0x80, 0x1B,
	0x36, 0x6C, 0xD8, 0xAB, 0x4D, 0x9A, 0x2F, 0x5E, 0xBC, 0x63, 0xC6, 0x97, 0x35, 0x6A, 0xD4, 0xB3,
	0x7D, 0xFA, 0xEF, 0xC5, 0x91, 0x39, 0x72, 0xE4, 0xD3, 0xBD, 0x61, 0xC2, 0x9F, 0x25, 0x4A, 0x94,
	0x33, 0x66, 0xCC, 0x83, 0x1D, 0x3A, 0x74, 0xE8, 0xCB, 0x8D, 0x01, 0x02, 0x04, 0x08, 0x10, 0x20,
	0x40, 0x80, 0x1B, 0x36, 0x6C, 0xD8, 0xAB, 0x4D, 0x9A, 0x2F, 0x5E, 0xBC, 0x63, 0xC6, 0x97, 0x35,
	0x6A, 0xD4, 0xB3, 0x7D, 0xFA, 0xEF, 0xC5, 0x91, 0x39, 0x72, 0xE4, 0xD3, 0xBD, 0x61, 0xC2, 0x9F,
	0x25, 0x4A, 0x94, 0x33, 0x66, 0xCC, 0x83, 0x1D, 0x3A, 0x74, 0xE8, 0xCB, 0x8D, 0x01, 0x02, 0x04,
	0x08, 0x10, 0x20, 0x40, 0x80, 0x1B, 0x36, 0x6C, 0xD8, 0xAB, 0x4D, 0x9A, 0x2F, 0x5E, 0xBC, 0x63,
	0xC6, 0x97, 0x35, 0x6A, 0xD4, 0xB3, 0x7D, 0xFA, 0xEF, 0xC5, 0x91, 0x39, 0x72, 0xE4, 0xD3, 0xBD,
	0x61, 0xC2, 0x9F, 0x25, 0x4A, 0x94, 0x33, 0x66, 0xCC, 0x83, 0x1D, 0x3A, 0x74, 0xE8, 0xCB
};

static uint8_t aes_mix_vector[4] = { 2, 3, 1, 1 };
static uint8_t aes_mix_inv_vector[4] = { 14, 11, 13, 9 };

static inline void aes_rotate_left(uint8_t word[4])
{
	uint8_t temp = word[0];
	for (int i = 0; i < 3; i++)
		word[i] = word[i + 1];
	word[3] = temp;
}

static inline void aes_rotate_right(uint8_t word[4])
{
	uint8_t temp = word[3];
	for (int i = 3; i > 0; i--)
		word[i] = word[i - 1];
	word[0] = temp;
}

static void aes_expand_key(uint8_t key[KEY_SIZE], uint8_t expanded_key[EXP_KEY_SIZE])
{
	for (int i = 0; i < KEY_SIZE; i++)
		expanded_key[i] = key[i];

	int pos = KEY_SIZE,
		rcon_i = 1;
	while (pos < EXP_KEY_SIZE)
	{
		uint8_t temp[4];
		for (int i = 0; i < 4; i++)
			temp[i] = expanded_key[pos - 4 + i];

		if (pos % KEY_SIZE == 0)
		{
			aes_rotate_left(temp);
			for (int i = 0; i < 4; i++)
				temp[i] = aes_sbox[temp[i]];
			temp[0] ^= aes_rcon[rcon_i++];
		}

		for (int i = 0; i < 4; i++)
		{
			expanded_key[pos] = expanded_key[pos - KEY_SIZE] ^ temp[i];
			pos++;
		}
	}
}

static uint8_t aes_galois_multiply(uint8_t a, uint8_t b)
{
	uint8_t p = 0;
	while (a != 0 && b != 0)
	{
		if (b & 0b00000001)
			p ^= a;
		b >>= 1;
		bool carry = a & 0b10000000;
		a <<= 1;
		if (carry)
			a ^= 0b00011011;
	}
	return p;
}

static inline void aes_substitute_bytes(uint8_t state[STATE_SIZE], uint8_t sbox[sizeof aes_sbox])
{
	for (int i = 0; i < STATE_SIZE; i++)
		state[i] = sbox[state[i]];
}

static inline void aes_shift_rows_left(uint8_t state[STATE_SIZE])
{
	for (int i = 1; i < 4; i++) /* i = 1 because the first row is never shifted */
	{
		for (int j = 0; j < i; j++)
			aes_rotate_left(state + i * 4);
	}
}

static inline void aes_shift_rows_right(uint8_t state[STATE_SIZE])
{
	for (int i = 1; i < 4; i++) /* i = 1 because the first row is never shifted */
	{
		for (int j = 0; j < i; j++)
			aes_rotate_right(state + i * 4);
	}
}

static inline void aes_add_round_key(uint8_t state[STATE_SIZE], uint8_t round_key[STATE_SIZE])
{
	for (int i = 0; i < STATE_SIZE; i++)
		state[i] ^= round_key[i];
}

/* 
	the multiplication matrix for mixing columns is either:
		2 3 1 1
		1 2 3 1
		1 1 2 3
		3 1 1 2
	or the inverse:
		14 11 13 09
		09 14 11 13
		13 09 14 11
		11 13 09 14
	Since each row is the the one before it shifted right, 
	we can just pass a vector in instead of the whole matrix
*/
static inline void aes_mix_columns(uint8_t state[STATE_SIZE], uint8_t vector[4])
{
	for (int i = 0; i < 4; i++)
	{
		uint8_t col_cpy[4];
		for (int j = 0; j < 4; j++)
			col_cpy[j] = state[j * 4 + i];
		
		for (int j = 0; j < 4; j++)
		{
			state[j * 4 + i] =
				aes_galois_multiply(col_cpy[j], vector[0])
				^ aes_galois_multiply(col_cpy[(j + 3) % 4], vector[3])
				^ aes_galois_multiply(col_cpy[(j + 2) % 4], vector[2])
				^ aes_galois_multiply(col_cpy[(j + 1) % 4], vector[1]);
		}
	}
}

static inline void aes_create_round_key(uint8_t exp_key_section[KEY_SIZE], uint8_t out_key[KEY_SIZE])
{
	for (int i = 0; i < 4; i++)
	{
		for (int j = 0; j < 4; j++)
			out_key[i + j * 4] = exp_key_section[i * 4 + j];
	}
}

static void aes_encrypt_chunk(uint8_t input[STATE_SIZE], uint8_t output[STATE_SIZE], uint8_t key[KEY_SIZE])
{
	uint8_t exp_key[EXP_KEY_SIZE];
	aes_expand_key(key, exp_key);

	uint8_t state[STATE_SIZE];
	for (int i = 0; i < 4; i++)
	{
		for (int j = 0; j < 4; j++)
			state[i + j * 4] = input[i * 4 + j];
	}

	uint8_t round_key[STATE_SIZE];
	aes_create_round_key(exp_key, round_key);
	aes_add_round_key(state, round_key);

	for (int i = 1; i < 10; i++)
	{
		aes_create_round_key(exp_key + KEY_SIZE * i, round_key);

		aes_substitute_bytes(state, aes_sbox);
		aes_shift_rows_left(state);
		aes_mix_columns(state, aes_mix_vector);
		aes_add_round_key(state, round_key);
	}

	aes_create_round_key(exp_key + KEY_SIZE * ROUND_COUNT, round_key);

	aes_substitute_bytes(state, aes_sbox);
	aes_shift_rows_left(state);
	aes_add_round_key(state, round_key);

	for (int i = 0; i < 4; i++)
	{
		for (int j = 0; j < 4; j++)
			output[i * 4 + j] = state[i + j * 4];
	}
}

static void aes_decrypt_chunk(uint8_t input[STATE_SIZE], uint8_t output[STATE_SIZE], uint8_t key[KEY_SIZE])
{
	uint8_t exp_key[EXP_KEY_SIZE];
	aes_expand_key(key, exp_key);

	uint8_t state[STATE_SIZE];
	for (int i = 0; i < 4; i++)
	{
		for (int j = 0; j < 4; j++)
			state[i + j * 4] = input[i * 4 + j];
	}

	uint8_t round_key[STATE_SIZE];
	aes_create_round_key(exp_key + ROUND_COUNT * KEY_SIZE, round_key);
	aes_add_round_key(state, round_key);

	for (int i = ROUND_COUNT - 1; i > 0; i--)
	{
		aes_create_round_key(exp_key + KEY_SIZE * i, round_key);

		aes_shift_rows_right(state);
		aes_substitute_bytes(state, aes_sbox_invert);
		aes_add_round_key(state, round_key);
		aes_mix_columns(state, aes_mix_inv_vector);
	}

	aes_create_round_key(exp_key, round_key);

	aes_shift_rows_right(state);
	aes_substitute_bytes(state, aes_sbox_invert);
	aes_add_round_key(state, round_key);

	for (int i = 0; i < 4; i++)
	{
		for (int j = 0; j < 4; j++)
			output[i * 4 + j] = state[i + j * 4];
	}
}

/* TO DO: use PBKDF2-SHA256, the plain text user input seed has awful entropy */

struct isaac_state
{
	uint32_t mm[256], seed[256];
	uint32_t aa, bb, cc;
	int pos;
};

static void isaac_main(struct isaac_state* state)
{
	state->cc++;
	state->bb += state->cc;

	for (int i = 0; i < 256; i++)
	{
		uint32_t x = state->mm[i];
		switch (i % 4)
		{
		case 0: state->aa ^= state->aa << 13; break;
		case 1: state->aa ^= state->aa >> 6; break;
		case 2: state->aa ^= state->aa << 2; break;
		case 3: state->aa ^= state->aa >> 16; break;
		}
		state->aa +=					state->mm[(i + 128) % 256];
		state->mm[i] =					state->mm[(x >> 2) % 256] + state->aa + state->bb;
		state->seed[i] = state->bb =	state->mm[(state->mm[i] >> 10) % 256] + x;
	}
	state->pos = 0;
}

static struct isaac_state* isaac_init(const char* seed)
{
#define ISAAC_MIX(val) do \
{ \
	val[0] ^= val[1] << 11;	val[3] += val[0]; val[1] += val[2]; \
	val[1] ^= val[2] >> 2;	val[4] += val[1]; val[2] += val[3]; \
	val[2] ^= val[3] << 8;	val[5] += val[2]; val[3] += val[4]; \
	val[3] ^= val[4] >> 16;	val[6] += val[3]; val[4] += val[5]; \
	val[4] ^= val[5] << 10;	val[7] += val[4]; val[5] += val[6]; \
	val[5] ^= val[6] >> 4;	val[0] += val[5]; val[6] += val[7]; \
	val[6] ^= val[7] << 8;	val[1] += val[6]; val[7] += val[0]; \
	val[7] ^= val[0] >> 9;	val[2] += val[7]; val[0] += val[1]; \
} while (0)

	struct isaac_state* result = calloc(1, sizeof * result);
	if (!result)
		return NULL;

	for (int i = 0; i < 256 && seed[i]; i++)
		result->seed[i] = seed[i];

	uint32_t val[8];
	for (int i = 0; i < 8; i++)
		val[i] = 0x9E3779B9; /* golden ratio */
	for (int i = 0; i < 4; i++)
		ISAAC_MIX(val);

	for (int i = 0; i < 256; i += 8)
	{
		for (int j = 0; j < 8; j++)
			val[j] += result->seed[i + j];
		ISAAC_MIX(val);
		for (int j = 0; j < 8; j++)
			result->mm[i + j] = val[j];
	}

	for (int i = 0; i < 256; i += 8)
	{
		for (int j = 0; j < 8; j++)
			val[j] += result->mm[i + j];
		ISAAC_MIX(val);
		for (int j = 0; j < 8; j++)
			result->mm[i + j] += val[j];
	}

	isaac_main(result);
	return result;

#undef ISAAC_MIX
}

static bool aes_open(const list_t in, list_t out)
{
	assert(in && list_element_size(in) == sizeof(char) && out && list_element_size(out) == sizeof(char));
	int size = list_count(in);
	uint8_t* buf = list_element_array(in);
	if (size < AES_FILE_OFFSET)
		return false;

	if (memcmp(buf, aes_header, sizeof aes_header) != 0)
		return false;

	uint8_t key[KEY_SIZE];
	struct isaac_state* rng = isaac_init(user_password);
	if (!rng)
		return false;
	for (int i = 0; i < KEY_SIZE; i++)
		key[i] = rng->seed[rng->pos++] % 0x100;
	for (int i = sizeof aes_header; i < AES_FILE_OFFSET; i++)
	{
		if (buf[i] != rng->seed[rng->pos++] % 0x100)
		{
			debug_format("Password is not valid.\n");
			return false;
		}
	}
	free(rng);

	buf += AES_FILE_OFFSET;
	size -= AES_FILE_OFFSET;

	int chunk_count = size / STATE_SIZE;
	for (int i = 0; i < chunk_count * STATE_SIZE; i += STATE_SIZE)
	{
		uint8_t chunk[16] = { 0 }, output[16];
		memcpy(chunk, buf + i, STATE_SIZE);
		aes_decrypt_chunk(chunk, output, key);
		for (int j = 0; j < STATE_SIZE; j++)
			LIST_PUSH(out, output[j]);
	}
#if _DEBUG
	debug_format("Opened file with AES using password \"%s\"\n", user_password);
#endif
	return true;
}

static bool aes_save(const list_t in, list_t out)
{
	assert(in && list_element_size(in) == sizeof(char) && out && list_element_size(out) == sizeof(char));

	LIST_PUSH(out, aes_header[0]);
	LIST_PUSH(out, aes_header[1]);
	LIST_PUSH(out, aes_header[2]);

	uint8_t key[KEY_SIZE];
	struct isaac_state* rng = isaac_init(user_password);
	if (!rng)
		return false;
	for (int i = 0; i < KEY_SIZE; i++)
		key[i] = rng->seed[rng->pos++] % 0x100;
	for (int i = 0; i < 16; i++)
		list_push_primitive(out, (void*)(rng->seed[rng->pos++] % 0x100));
	free(rng);

	int size = list_count(in);
	uint8_t* buf = list_element_array(in);

	int chunk_count = size / STATE_SIZE + 1;
	for (int i = 0; i < chunk_count * STATE_SIZE; i += STATE_SIZE)
	{
		int curr_chunk_size = min(STATE_SIZE, size - i);
		if (curr_chunk_size <= 0)
			break;

		uint8_t chunk[16] = { 0 }, output[16];
		memcpy(chunk, buf + i, curr_chunk_size);
		aes_encrypt_chunk(chunk, output, key);
		for (int j = 0; j < STATE_SIZE; j++)
			LIST_PUSH(out, output[j]);
	}

#if _DEBUG
	debug_format("Saved file using AES with password \"%s\"\n", user_password);
#endif
	return true;
}

#ifdef TEST
#define FILE_TEST
#ifdef FILE_TEST
#include <time.h>

void rand_str(char* buf, size_t len)
{
	for (size_t i = 0; i < len - 1; i++)
		buf[i] = rand() % 26 + 'A';
}

int main()
{
	srand(time(NULL));
	char password[11] = { 0 };
	rand_str(password, sizeof password);
	file_set_password(password);

	file_details_t test = { .directory = "aes_test_start.txt", .lines = list_create(sizeof(line_t)) };
	for (int i = 0; i < 128; i++)
	{
		char buf[32];
		rand_str(buf, sizeof buf);
		line_t line = { .string = list_create_with_array(buf, sizeof(char), sizeof buf - 1) /* exclude NUL terminator */ };
		LIST_PUSH(test.lines, line);
	}

	assert(file_save(test, TYPE_PLAIN));
	test.directory = "aes_test_start.aes";
	assert(file_save(test, TYPE_ENCRYPTED));
	file_details_t read = file_open(test.directory, TYPE_ENCRYPTED);
	assert(!IS_BAD_DETAILS(read));
	for (int i = 0; i < list_count(read.lines); i++)
	{
		list_t str = LIST_GET(read.lines, i, line_t)->string;
		for (int j = 0; j < list_count(str); j++)
			assert(*LIST_GET(str, j, char) == *LIST_GET(LIST_GET(test.lines, i, line_t)->string, j, char));
	}
	read.directory = "aes_test.end.txt";
	assert(file_save(read, TYPE_PLAIN));
}
#else
#include <Windows.h>

/* each completed in ~1 second */

#define ENCRYPT_COUNT 0x20000
#define DECRYPT_COUNT 0x18000

int main()
{
	LARGE_INTEGER freq, start, end;
	QueryPerformanceFrequency(&freq);

	QueryPerformanceCounter(&start);
	srand(start.QuadPart);

	uint8_t input[16], output[16], decrypt[16], key[16];
	for (int i = 0; i < 16; i++)
	{
		input[i] = rand();
		key[i] = rand();
	}

	QueryPerformanceCounter(&start);
	for (int i = 0; i < ENCRYPT_COUNT; i++)
		aes_encrypt_chunk(input, output, key);
	QueryPerformanceCounter(&end);
	printf("Encrypted %i chunks in %fs.\n", ENCRYPT_COUNT, (end.QuadPart - start.QuadPart) / (double)freq.QuadPart);

	QueryPerformanceCounter(&start);
	for (int i = 0; i < DECRYPT_COUNT; i++)
		aes_decrypt_chunk(output, decrypt, key);
	QueryPerformanceCounter(&end);
	bool success = memcmp(input, decrypt, STATE_SIZE) == 0;
	printf("%s %i chunks in %fs.\n", success ? "Successfully decrypted" : "Failed to decrypt", DECRYPT_COUNT, (end.QuadPart - start.QuadPart) / (double)freq.QuadPart);
}
#endif
#endif

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
static void dmc_predictor_init(struct dmc_state* state)
{
	state->clone_buf = journal_malloc(sizeof * state->clone_buf * CLONE_COUNT);
	state->max_cb = state->clone_buf + CLONE_COUNT - 20; /* TO DO: why -20? */
	dmc_predictor_braid(state);
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
	
	struct dmc_state* state = journal_malloc(sizeof * state);
	dmc_predictor_init(state);

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

	LIST_PUSH(out, dmc_header[0]);
	LIST_PUSH(out, dmc_header[1]);
	LIST_PUSH(out, dmc_header[2]);
	
	struct dmc_state* state = journal_malloc(sizeof * state);
	dmc_predictor_init(state);
	
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
				list_push_primitive(out, (void*)(min >> 16));
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
	list_push_primitive(out, (void*)(min >> 16));
	list_push_primitive(out, (void*)((min >> 8) & 0xFF));
	list_push_primitive(out, (void*)(min & 0xFF));;

	free(state);
	debug_format("Compressed file with Dynamic Markov Compression, in: %i, out: %i\n", in_bytes, out_bytes);
	return true;
}