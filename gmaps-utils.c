/* gmaps-utils.c */

#include <stdlib.h>
#include <stdio.h>
#include <math.h>

#include "gmaps.h"

struct map *
profile_load(const char * fn)
{
	uint8_t z;
	uint16_t sz;
	uint32_t i, j, x, y, nr_entries;
	struct profile_entry pe;
	struct map * map;
	struct list * list;
	FILE * f;

	if (!fn) return NULL;

	f = fopen(fn, "r");
	if (!f) return NULL;

	map = map_new(PROFILEMAP_HASHSIZE);
	if (!map) goto err;

	for (i=0;i<PROFILEMAP_HASHSIZE;i++) {
		sz = read_uint16(f);
		nr_entries = read_uint32(f);
		for (j=0;j<nr_entries;j++) {
			x = read_uint32(f);
			y = read_uint32(f);
			z = read_uint8(f);
			if (x >= MAX_X || y >= MAX_Y || z >= MAX_Z)
				continue;
			pe.x = x;
			pe.y = y;
			pe.z = z;
			list = map_get(map, sz);
			if (!list) {
				list = list_new(sizeof(struct profile_entry));
				if (map_set(map, sz, list) < 0)
					goto err;
			}

			if (list_append(list, &pe) < 0)
				goto err;
		}
	}

	fclose(f);
	return map;
err:
	if (map) profile_unload(map);
	fclose(f);
	return NULL;
}

void
_list_free(void * l)
{
	list_free((struct list *)l);	
}

void
profile_unload(struct map * map)
{
	map_free(map, _list_free);
}

/* The functions below this comment are slightly modified but essentially
   ported versions of the Python code in gmapcatcher to resolve tiles to
   coordinates and vice versa. */

int
tiles_on_level(int zoom_level)
{
	return (1<<(17-zoom_level));
}

void
tile_to_coord(uint8_t zoom, struct coord * c, int x2, int y2,
	double * dlat, double * dlon)
{
	double x, y, e, lat, lon, x1, y1;
	double world_tiles = tiles_on_level(zoom);
	x1 = c->x;
	y1 = c->y;
	x = ((x1 + (1.0 * ((double)x2)/256.0)) / (world_tiles/2.0)) - 1.0;
	y = ((y1 + (1.0 * ((double)y2)/256.0)) / (world_tiles/2.0)) - 1.0;
	lon = x * 180.0;
	y = exp(-y * 2 * PI);
	e = (y-1.0)/(y+1.0);
	lat = 180.0/PI * asin(e);
	*dlat = lat;
	*dlon = lon;
}

void
coord_to_tile(double lat, double lon, int zoom,
	struct coord * c1, struct coord * c2)
{
	double x, e, y, tiles_per_radian;
	int world_tiles, offsetx, offsety;

	world_tiles = tiles_on_level(zoom);
	x = (double)(world_tiles / 360.0 * (lon + 180.0));
	tiles_per_radian = world_tiles / (2.0 * PI);
	e = sin(lat * (1.0/180.0* PI));
	y = ((double)world_tiles/2.0) + 0.5*log((1+e)/(1-e)) *
		(tiles_per_radian * -1.0);
	offsetx = (int)((x - (int)x) * 256.0);
	offsety = (int)((y - (int)y) * 256.0);
	c1->x = ((int)(x) % world_tiles);
	c1->y = ((int)(y) % world_tiles);
	c2->x = offsetx;
	c2->y = offsety;
}

/* EOF */
