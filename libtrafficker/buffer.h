/* buffer.h */

#ifndef BUFFER_H
  #define BUFFER_H

struct buffer {
	size_t len;
	size_t alloc;
	char * data;
};

struct buffer * buffer_new();
void buffer_free(struct buffer *);
void buffer_append(struct buffer *, const char *, size_t);
void buffer_reset(struct buffer *);

#endif
