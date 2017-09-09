/* list.c */

#include <stdlib.h>
#include <string.h>

#include "list.h"

struct list {
	uint32_t alloc;
	uint32_t off;
	size_t entry_size;
	void * entries;
};

struct list *
list_new(size_t entry_size)
{
	size_t sz;
	struct list * list;

	list = malloc(sizeof(struct list));
	if (!list) return NULL;

	sz = 256 * entry_size;
	if (sz < 256 || sz < entry_size)
		/* int overflow */
		return NULL;

	list->entries = malloc(sz);
	if (!list->entries) {
		free(list);
		return NULL;
	}

	list->entry_size = entry_size;
	list->alloc = 256;
	list->off = 0;

	return list;
}

int
list_append(struct list * list, void * p)
{
	size_t sz;
	void * entries;

	if (!list || !p) return -1;

	if (list->alloc == list->off) {
		sz = list->alloc * 2 * list->entry_size;
		if (sz < list->alloc || sz < list->entry_size)
			/* int overflow */
			return -1;
		entries = realloc(list->entries, sz);
		if (!entries) return -1;
		list->entries = entries;
		list->alloc *= 2;
	}

	memcpy(list->entries + (list->off * list->entry_size),
		p, list->entry_size);

	list->off++;
	return 0;
}

uint32_t
list_count(struct list * list)
{
	if (!list) return 0;
	return list->off;
}

int
list_get(struct list * list, uint32_t idx, void * res)
{
	if (!list || !res || idx >= list->off) return -1;
	
	memcpy(res, list->entries + (idx * list->entry_size), list->entry_size);
	return 0;
}

void
list_free(struct list * list)
{
	if (!list) return;

	free(list->entries);
	free(list);
}

int
list_contains(struct list * list, void * p)
{
	uint32_t i;

	if (!list || !p) return 0;

	for(i=0;i<list->off;i++) {
		if (!memcmp(list->entries + (i * list->entry_size), p, 
			list->entry_size))
			return 1;
	}

	return 0;
}

/* EOF */
