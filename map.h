/* map.h */

#ifndef MAP_H
  #define MAP_H

#include <stdint.h>
#include "list.h"

struct map * map_new(uint32_t);
int map_set(struct map *, uint32_t, void *);
void * map_get(struct map *, uint32_t);
void map_free(struct map *, void (*)(void *));
uint32_t map_count(struct map *);
struct list * map_getkeys(struct map *, int);

#endif

/* EOF */
