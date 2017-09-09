/* gmaps-profile.c */

/* This is a modified and translated version of the Python code in gmapcatcher
   to resolve image paths in the gmapcatcher cache for a specified input range
   of latitude, longitude and zoomlevels. */

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>

#include "gmaps.h"

#define MAX_CACHE_PATH		2048

static char * gmap_catcher_cache_path;

static struct map * profile_map;

static unsigned int tiles_missing = 0;
static unsigned int tiles_found = 0;
static int verbose = 0;

inline static const char *
coord_to_path(int z, int x, int y)
{
	static char buf[MAX_CACHE_PATH * 2];

	memset(buf, 0, sizeof(buf)-1);

	sprintf(buf, "%s/sat_tiles/%i/%i/%i/%i/%i.png",
		gmap_catcher_cache_path,
		z, x/1024, x % 1024, y / 1024, y % 1024);
	return buf;
}

static void
query_region(int xmin, int xmax, int ymin, int ymax, int zoom)
{
	struct list * list;
	struct profile_entry pe;
	off_t file_size;
	struct stat st;
	int world_tiles, i, j, x, y, ret;

	world_tiles = tiles_on_level(zoom);
	if (xmax - xmin >= world_tiles) {
		xmin = 0;
		xmax = world_tiles - 1;
	}
	if (ymax - ymin >= world_tiles) {
		ymin = 0;
		ymax = world_tiles - 1;
	}

	for (i=0;i<(xmax-xmin+world_tiles)%world_tiles+1;i++) {
		x = (xmin+i) % world_tiles;
		for (j=0;j<(ymax-ymin+world_tiles)%world_tiles+1;j++) {
			y = (ymin+j) % world_tiles;

			ret = stat(coord_to_path(zoom, x, y), &st);
			if (!ret) {
				file_size = st.st_size;

				pe.x = x;
				pe.y = y;
				pe.z = zoom;

				list = map_get(profile_map, file_size);
				if (!list) {
					list = list_new(
						sizeof(struct profile_entry));
					if (!list) fatal("Out of memory.");
					if (map_set(profile_map, file_size,
							list) < 0)
						fatal("Out of memory.");
				}
				list_append(list, &pe);

				if (verbose) {
					printf("Found: (%i,%i,%i), size: %lu\n",
						x,y,zoom,st.st_size);
				}
				tiles_found++;
			}
			else {
				tiles_missing++;
				if (verbose) {
					printf("Missing: (%i,%i,%i)\n",
						x,y,zoom);
				}
			}
		}
	}
}

static void
query_region_around_location(double lat0, double lon0, double dlat, double dlon,
	int zoom)
{
	struct coord coord1, coord2, coord3, coord4;

	if (dlat > 170.0) {
		lat0 = 0.0;
		dlat = 170.0;
	}
	if (dlon > 358.0) {
		lon0 = 0.0;
		dlon = 358.0;
	}

	coord_to_tile(lat0+(dlat/2.0), lon0-(dlon/2.0), zoom, &coord1, &coord2);
	coord_to_tile(lat0-(dlat/2.0), lon0+(dlon/2.0), zoom,
		&coord3, &coord4);

	query_region(coord1.x, coord3.x, coord1.y, coord3.y, zoom);
}

void
usage(const char * argv0)
{
	fprintf(stderr, "%s [options]\n", argv0);
	fprintf(stderr, "Builds a gmaps-trafficker profile from a");
	fprintf(stderr, " gmapcatcher cache directory.\n\n");
	fprintf(stderr, "Example: '%s -a 48.856667 -o 2.350833' will", argv0);
	fprintf(stderr, " create the\nprofile for Paris, France.\n\n");
	fprintf(stderr, "-a <latitude>  - latitude\n");
	fprintf(stderr, "-o <longitude> - longitude\n");
	fprintf(stderr, "-m             - merge new profile data with");
	fprintf(stderr, " the existing data in the file\n");
	fprintf(stderr, "-d <cachedir>  - gmapcatcher cache directory\n");
	fprintf(stderr, "                 (default: $HOME/.googlemaps/)\n");
	fprintf(stderr, "-f <filename>  - write data to this file\n");
	fprintf(stderr, "                 (default: ./%s)\n", DEFAULT_FN);
	fprintf(stderr, "-v             - be verbose\n");
	fprintf(stderr, "-h             - usage information\n");
	exit(EXIT_FAILURE);
}

int
main(int argc, char ** argv, char ** envp)
{
	FILE * f;
	struct list * list;
	struct stat st;
	struct profile_entry pe;
	char * filename = DEFAULT_FN, * tmp, * arg0;
	char buf[4096];
	size_t off;
	double lng, lat, lat_range, lng_range;
	double latitude = -1.0, longitude = -1.0;
	unsigned int i, j;
	int max_zl = 17, min_zl = 2, merge = 0;
	int c, zl;

	arg0 = (argc > 0 ? argv[0] : "(unknown)");

	while ((c = getopt(argc, argv, "d:f:hmva:o:")) != -1) {
		switch (c) {
			case 'a':
				sscanf(optarg, "%lf", &latitude);
				break;
			case 'o':
				sscanf(optarg, "%lf", &longitude);
				break;
			case 'd':
				gmap_catcher_cache_path = optarg;
				break;
			case 'f':
				filename = optarg;
				break;
			case 'h':
				usage(arg0);
				break; /* not reached */
			case 'm':
				merge = 1;
				break;
			case 'v':
				verbose = 1;
				break;
		}
	}

	if (latitude == -1.0 || longitude == -1.0) {
		fprintf(stderr, "Both latitude and longitude need to be set. ");
		fprintf(stderr, "See -h for info.\n");
		exit(EXIT_FAILURE);
	}
	else {
		lng = longitude;
		lat = latitude;
		lat_range = 0.05;
		lng_range = 0.05;
	}

	/* check for existance of gmapcatcher.conf and assume it's a
	   gmapcatcher cache directory then. */
	memset(buf, 0, sizeof(buf));
	if (!gmap_catcher_cache_path) {
		tmp = getenv("HOME");
		if (!tmp || strlen(tmp) > 2048) {
			fprintf(stderr, "Cannot find gmapcatcher directory.");
			fprintf(stderr, " Use -h for info.\n");
			exit(EXIT_FAILURE);
		}
		strcat(buf, tmp);
		strcat(buf, "/.googlemaps/");
		gmap_catcher_cache_path = strdup(buf);
		if (!gmap_catcher_cache_path) {
			fprintf(stderr, "Out of memory.\n");
			exit(EXIT_FAILURE);
		}
	}
	else {
		strcat(buf, gmap_catcher_cache_path);
	}
	strcat(buf, "/gmapcatcher.conf");
	if (stat(buf, &st) < 0) {
		fprintf(stderr, "Cannot find gmapcatcher directory.");
		fprintf(stderr, " Use -h for info.\n");
		exit(EXIT_FAILURE);
	}

	if (merge) {
		profile_map = profile_load(filename);
		if (!profile_map) fatal("Error while opening profile!");
	}
	else {
		profile_map = map_new(PROFILEMAP_HASHSIZE);
		if (!profile_map) fatal("Cannot create profile!");
	}


	/* Search the cache and add found entries to the profile table */
	for (zl=max_zl; zl > (min_zl - 1); zl--) {
		query_region_around_location(lat, lng,
			lat_range * 2.0, lng_range * 2.0, zl);
	}

	printf("Total tiles: %d (found: %d, missing: %d).\n",
		tiles_found + tiles_missing, tiles_found, tiles_missing);

	printf("Writing out %sprofile.\n", (merge ? "merged " : ""));

	f = fopen(filename, "w+");
	for (i=0;i<PROFILEMAP_HASHSIZE;i++) {

		write_uint16(f, i & 0xFFFF);

		list = map_get(profile_map, i);
		if (!list) {
			write_uint32(f, 0);
			continue;
		}

		off = list_count(list);
		write_uint32(f, off & 0xFFFF);

		for (j=0;j<off;j++) {
			if (list_get(list, j, &pe) < 0)
				fatal("Error while retrieving data.");
			write_uint32(f, pe.x);
			write_uint32(f, pe.y);
			write_uint8(f, pe.z);
		}
	}

	printf("Done.\n");

	profile_unload(profile_map);

	exit(EXIT_SUCCESS);
}

/* EOF */
