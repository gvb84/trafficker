/* list.h */

#ifndef LIST_H
  #define LIST_H

#include <stdint.h>
#include <sys/types.h>

struct list * list_new(size_t);
int list_append(struct list *, void *);
int list_get(struct list *, uint32_t, void *);
void list_free(struct list *);
uint32_t list_count(struct list *);
int list_contains(struct list *, void *);

#endif

/* EOF */
