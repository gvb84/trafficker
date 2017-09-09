/* gmaps.h */

#ifndef GMAPS_H
  #define GMAPS_H

#include <stdint.h>
#include <stdlib.h>

#include "map.h"
#include "list.h"
#include "utils.h"

/* hash sizes for tables must be prime */
#define SESSIONMAP_HASHSIZE		1009
#define TSMAP_HASHSIZE			1009

/* assume there are no tiles with size >= 30kB, so choose lowest
   prime bigger than that. */
#define PROFILEMAP_HASHSIZE		30727

/* minimum and maximum lenght of tiles */
#define MIN_TILE_LEN			(2 * 1024)
#define MAX_TILE_LEN			(30 * 1024)

/* defines range to retrieve coordinates for a tile from the database */
#define TILE_LEN_RANGE			1024	

/* maximum entries to use for calculating the histogram */
#define MAX_HISTOGRAM			100

/* default profile name */
#define DEFAULT_FN			"gmaps_profile.dat"

/* maximum supported coordinate values */
#define MAX_X				200000
#define MAX_Y				200000
#define MAX_Z				20

#define PI 			3.14159265358979323846

/* entry in the profile database */
struct profile_entry {
	uint32_t x;
	uint32_t y;
	uint8_t  z;
};

/* one http request/response pair */
struct http_entry {
	time_t ts;
	size_t reslen;
	size_t reqlen;
};

/* coordinate in World Coordinate System */
struct coord {
	uint32_t x;
	uint32_t y;
};

struct map * profile_load(const char *);
void profile_unload(struct map *);
void _list_free(void *);
int tiles_on_level(int);
void tile_to_coord(uint8_t, struct coord *, int, int, double *, double *);
void coord_to_tile(double, double, int,	struct coord *, struct coord *);

#endif

/* EOF */
