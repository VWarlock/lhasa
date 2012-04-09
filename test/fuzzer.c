/*

Copyright (c) 2011, 2012, Simon Howard

Permission to use, copy, modify, and/or distribute this software
for any purpose with or without fee is hereby granted, provided
that the above copyright notice and this permission notice appear
in all copies.

THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL
WARRANTIES WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED
WARRANTIES OF MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE
AUTHOR BE LIABLE FOR ANY SPECIAL, DIRECT, INDIRECT, OR
CONSEQUENTIAL DAMAGES OR ANY DAMAGES WHATSOEVER RESULTING FROM
LOSS OF USE, DATA OR PROFITS, WHETHER IN AN ACTION OF CONTRACT,
NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF OR IN
CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.

 */

// Fuzz testing system for stress-testing the decompressors.
// This works by repeatedly generating new random streams of
// data and feeding them to the decompressor.

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <assert.h>
#include <time.h>

#include "lha_decoder.h"

// Maximum amount of data to read before stopping.

#define MAX_FUZZ_LEN (2 * 1024 * 1024)

typedef struct {
	uint8_t *data;
	size_t data_len;
	unsigned int read;
} ReadCallbackData;

// Contents of "canary buffer" that is put around allocated blocks to
// check their contents.

static const uint8_t canary_block[] = {
	0xdf, 0xba, 0x18, 0xa0, 0x51, 0x91, 0x3c, 0xd6, 
	0x03, 0xfb, 0x2c, 0xa6, 0xd6, 0x88, 0xa5, 0x75, 
};

// Allocate some memory with canary blocks surrounding it.

static void *canary_malloc(size_t nbytes)
{
	uint8_t *result;

	result = malloc(nbytes + 2 * sizeof(canary_block) + sizeof(size_t));
	assert(result != NULL);

	memcpy(result, &nbytes, sizeof(size_t));
	memcpy(result + sizeof(size_t), canary_block, sizeof(canary_block));
	memset(result + sizeof(size_t) + sizeof(canary_block), 0, nbytes);
	memcpy(result + sizeof(size_t) + sizeof(canary_block) + nbytes,
	       canary_block, sizeof(canary_block));

	return result + sizeof(size_t) + sizeof(canary_block);
}

// Free memory allocated with canary_malloc().

static void canary_free(void *data)
{
	if (data != NULL) {
		free((uint8_t *) data - sizeof(size_t) - sizeof(canary_block));
	}
}

// Check the canary blocks surrounding memory allocated with canary_malloc().

static void canary_check(void *_data)
{
	uint8_t *data = _data;
	size_t nbytes;

	memcpy(&nbytes, data - sizeof(size_t) - sizeof(canary_block),
	       sizeof(size_t));

	assert(!memcmp(data - sizeof(canary_block), canary_block,
	               sizeof(canary_block)));
	assert(!memcmp(data + nbytes, canary_block, sizeof(canary_block)));
}

// Fill in the specified block with random data.

static void fuzz_block(uint8_t *data, unsigned int data_len)
{
	unsigned int i;

	for (i = 0; i < data_len; ++i) {
		data[i] = rand() & 0xff;
	}
}

// Callback function used to read more data from the signature being
// processed.

static size_t read_more_data(void *buf, size_t buf_len, void *user_data)
{
	ReadCallbackData *cb_data = user_data;

	// Return end of file when we reach the end of the data.

	if (cb_data->read >= cb_data->data_len) {
		return 0;
	}

	// Only copy a single byte at a time. This allows us to
	// accurately track how much of the signature is valid.

	memcpy(buf, cb_data->data + cb_data->read, 1);
	++cb_data->read;

	return 1;
}

// Decode data from the specified signature block, using a decoder
// of the specified type.

static unsigned int run_fuzz_test(LHADecoderType *dtype,
                                  uint8_t *data,
                                  size_t data_len)
{
	ReadCallbackData cb_data;
	uint8_t *read_buf;
	size_t result;
	void *handle;

	// Init decoder.

	cb_data.data = data;
	cb_data.data_len = data_len;
	cb_data.read = 0;

	handle = canary_malloc(dtype->extra_size);
	assert(dtype->init(handle, read_more_data, &cb_data));

	// Create a buffer into which to decompress data.

	read_buf = canary_malloc(dtype->max_read);
	assert(read_buf != NULL);

	for (;;) {
		memset(read_buf, 0, dtype->max_read);
		result = dtype->read(handle, read_buf);
		canary_check(read_buf);

		//printf("read: %i\n", result);
		if (result == 0) {
			break;
		}
	}

	// Destroy the decoder and free buffers.

	if (dtype->free != NULL) {
		dtype->free(handle);
	}

	canary_check(handle);
	canary_free(handle);
	canary_free(read_buf);

	//printf("Fuzz test complete, %i bytes read\n", cb_data.read);

	return cb_data.read;
}

static void fuzz_test(LHADecoderType *dtype, size_t data_len)
{
	unsigned int count;
	void *data;

	// Generate a block of random data.

	data = malloc(data_len);
	assert(data != NULL);
	fuzz_block(data, data_len);

	// Run the decoder with the data as input.

	count = run_fuzz_test(dtype, data, data_len);

	if (count >= data_len) {
		printf("\tTest complete (end of file)\n");
	} else {
		printf("\tTest complete (read %i bytes)\n", count);
	}

	free(data);
}

int main(int argc, char *argv[])
{
	LHADecoderType *dtype;
	unsigned int i;

	if (argc < 2) {
		printf("Usage: %s <decoder-type>\n", argv[0]);
		exit(-1);
	}

	dtype = lha_decoder_for_name(argv[1]);

	if (dtype == NULL) {
		fprintf(stderr, "Unknown decoder type '%s'\n", argv[1]);
		exit(-1);
	}

	srand(time(NULL));

	for (i = 0; ; ++i) {
		printf("Iteration %i:\n", i);
		fuzz_test(dtype, MAX_FUZZ_LEN);
	}

	return 0;
}

