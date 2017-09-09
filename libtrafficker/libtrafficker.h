/* libtrafficker.h */

#ifndef LIBTRAFFICKER_H
  #define LIBTRAFFICKER_H

#include <stdint.h>

struct trafficker;

struct burst {
	struct trafficker * tr;
	uint32_t hash;
	uint32_t id;
	size_t len;
	int client;
	int incomplete;
	uint32_t chost;
	uint16_t cport;
	uint32_t dhost;
	uint16_t dport;
	time_t ts;
};

typedef void (*traffick_handler)(const struct burst *);

struct trafficker * trafficker_open_offline(
	const char * fname, const char * filter);
struct trafficker * trafficker_open_online(
	const char * device, const char * filter);
void trafficker_close(struct trafficker * t);
int trafficker_loop(struct trafficker * t, traffick_handler);
int trafficker_breakloop(struct trafficker * t);
int trafficker_set_burstjoin(struct trafficker * t, int);
int trafficker_get_burstjoin(struct trafficker * t, int *);

#endif

/* EOF */
