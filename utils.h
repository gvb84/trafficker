/* utils.h */

#ifndef UTILS_H
  #define UTILS_H

#include <stdio.h>
#include <stdint.h>

void * xmalloc(size_t);
uint32_t read_uint32(FILE *);
uint16_t read_uint16(FILE *);
uint8_t read_uint8(FILE *);
void write_uint32(FILE *, uint32_t);
void write_uint16(FILE *, uint16_t);
void write_uint8(FILE *, uint8_t);
void fatal(const char *);
void privdrop(const char *);

#endif

/* EOF */
