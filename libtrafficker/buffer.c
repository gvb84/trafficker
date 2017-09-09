/* buffer.c */

#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <string.h>

#include "buffer.h"

struct buffer *
buffer_new()
{
	struct buffer * b;

	b = malloc(sizeof(struct buffer));
	if (!b) {
		fprintf(stderr, "Cannot allocate memory.\n");
		exit(EXIT_FAILURE);
	}

	b->len = 0;
	b->alloc = 1024;
	b->data = malloc(b->alloc);
	if (!(b->data)) {
		fprintf(stderr, "Cannot allocate memory.\n");
		exit(EXIT_FAILURE);
		free(b);
	}

	memset(b->data, 0, b->alloc);
	return b;
}

void
buffer_append(struct buffer * b, const char * data, size_t len)
{
	if (!b || !data || !len) {
		fprintf(stderr, "Wrong arguments.\n");
		exit(EXIT_FAILURE);
	}

	if ((b->len + len) > b->alloc) {
		b->alloc = b->len + len;
		b->data = realloc(b->data, b->alloc);
	}
	memcpy(b->data + b->len, data, len);
	b->len += len;
}

void
buffer_reset(struct buffer * b)
{
	if (!b) {
		fprintf(stderr, "Wrong arguments.\n");
		exit(EXIT_FAILURE);
	}

	b->len = 0;
	b->data = realloc(b->data, 1024);
	b->alloc = 1024;
}

void
buffer_free(struct buffer * b)
{
	if (!b) {
		fprintf(stderr, "Wrong arguments.\n");
		exit(EXIT_FAILURE);
	}
	free(b->data);
	free(b);
}

/* EOF */
