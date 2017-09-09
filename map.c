/* map.c */

#include <stdlib.h>
#include <string.h>

#include "map.h"
#include "list.h"

struct map_entry {
	struct map_entry * next;
	uint32_t key;
	void * data;
	
};

struct map {
	uint32_t hash_size;
	struct map_entry ** entries;
	uint32_t count;
};

struct map *
map_new(uint32_t hash_size)
{
	struct map * map;

	map = malloc(sizeof(struct map));
	if (!map) return NULL;

	map->entries = malloc(sizeof(struct map_entry *) * hash_size);
	if (!map->entries) {
		free(map);
		return NULL;
	}
	memset(map->entries, 0, sizeof(struct map_entry *) * hash_size);

	map->hash_size = hash_size;
	map->count = 0;
	return map;
}

int
map_set(struct map * map, uint32_t key, void * data)
{
	struct map_entry * entry, * last;

	if (!map) return -1;

	last = NULL;
	entry = map->entries[key % map->hash_size];
	while (entry != NULL) {
		if (entry->key == key) {
			/* key already in the hashmap, just overwrite
			   the datapointer, it's the callers responsibility
			   to check if a key already exists and free the
			   data for that key before resetting it. */
			entry->data = data;
			return 0;
		}
		last = entry;
		entry = entry->next;
	}

	entry = malloc(sizeof(struct map_entry));
	if (!entry) return -1;

	if (!last) map->entries[key % map->hash_size] = entry;
	else last->next = entry;

	entry->key = key;
	entry->data = data;
	entry->next = NULL;
	map->count++;
	
	return 0;
}

void *
map_get(struct map * map, uint32_t key)
{
	struct map_entry * entry;

	if (!map) return NULL;

	entry = map->entries[key % map->hash_size];
	while (entry != NULL) {
		if (entry->key == key)
			return entry->data;
		entry = entry->next;
	}

	return NULL;
}

void
map_free(struct map * map, void (*data_free)(void *))
{
	struct map_entry * entry, * next;
	uint32_t i;

	for (i=0;i<map->hash_size;i++) {
		entry = map->entries[i];
		while (entry) {
			next = entry->next;
			if (data_free)
				data_free(entry->data);
			free(entry);
			entry = next;
		}
	}

	free(map);
}

uint32_t
map_count(struct map * map)
{
	if (!map) return 0;
	return map->count;
}

static int
uint32_tcompare(const void * a, const void * b)
{
	uint32_t arg1, arg2;

	arg1 = *(uint32_t *)a;
	arg2 = *(uint32_t *)b;
	if( arg1 < arg2 ) return -1;
	else if( arg1 == arg2 ) return 0;
	else return 1;
}

struct list *
map_getkeys(struct map * map, int sort)
{
	uint32_t * sorted;
	struct list * list;
	struct map_entry * entry;
	uint32_t i, c;

	if (!map) return NULL;

	list = list_new(sizeof(uint32_t));
	if (!list) return NULL;

	if (sort) {
		sorted = malloc(sizeof(uint32_t) * map->count);
		memset(sorted, 0, sizeof(uint32_t) * map->count);
	}

	c = 0;
	for (i=0;i<map->hash_size;i++) {
		entry = map->entries[i];
		while (entry) {
			if (sort) sorted[c] = entry->key;
			else list_append(list, &(entry->key));
			entry = entry->next;
			c++;
		}
	}

	if (sort) {
		qsort(sorted, c, sizeof(uint32_t), uint32_tcompare);
		for (i=0;i<c;i++) {
			list_append(list, &(sorted[i]));
		}
		free(sorted);
	}

	return list;
}

/* EOF */
