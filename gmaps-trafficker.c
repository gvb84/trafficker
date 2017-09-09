/* gmaps-trafficker.c */

#include <errno.h>
#include <netdb.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <time.h>
#include <unistd.h>
#include <sys/select.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include "libtrafficker.h"
#include "gmaps.h"

struct trafficker * tr = NULL;
struct map * profilemap = NULL;
struct map * sessionmap = NULL;
struct map * tsmap = NULL;
static int child_died = 0;
static int int_received = 0;
static int verbose_level = 0;
static int live_mode = 0;
static int capture_fd = 0;
static int analyze_fd = 0;
static int colorize_output = 0;

struct matches {
	size_t off;
	size_t max;
	struct map * xseen;
	struct map *** xmaps;
};

struct retangle {
	uint8_t z;
	struct coord c1;
	struct coord c2;
	struct coord c3;
	struct coord c4;
	double lat;
	double lng;
};

static void
verbose(int level, const char * fmt, ...)
{
	va_list ap;

	if (level > verbose_level) return;
	if (colorize_output && level > 1) printf("\x1b[1;30m[+] "); 
	else printf("[+] ");
	va_start(ap, fmt);
	vprintf(fmt, ap);
	va_end(ap);
	if (colorize_output && level > 1) printf("\x1b[0;37m");
	fflush(stdout);
}

static void
warning(const char * fmt, ...)
{
	va_list ap;

	printf("%s", (colorize_output ? "\x1b[1;33m[!]\x1b[0;37m " : "[!] "));
	va_start(ap, fmt);
	vprintf(fmt, ap);
	va_end(ap);
	fflush(stdout);
}

static struct matches *
matches_new()
{
	struct matches * m;
	uint32_t i, j;

	m = xmalloc(sizeof(struct matches));
	m->off = 0;
	m->max = 50;

	m->xmaps = xmalloc(sizeof(struct map **) * MAX_Z);
	for (i=0;i<MAX_Z;i++) {
		m->xmaps[i] = xmalloc(sizeof(struct map *) * m->max);
		for (j=0;j<m->max;j++) {
			m->xmaps[i][j] = map_new(1009);
			if (!(m->xmaps[i][j])) fatal("Out of memory.");
		}
	}

	m->xseen = map_new(1009);
	if (!(m->xseen)) fatal("Out of memory.");

	return m;	
}

static void
matches_free(struct matches * m)
{
	uint32_t i,j;

	for(i=0;i<MAX_Z;i++) {
		for(j=0;j<m->max;j++) {
			map_free(m->xmaps[i][j], _list_free);
		}
		free(m->xmaps[i]);
	}
	map_free(m->xseen, NULL);
	free(m->xmaps);
	free(m);
}

static void
matches_add(struct matches * matches, struct http_entry * hte)
{
	struct map * map;
	struct list * pflist, * ylist;
	struct profile_entry pe;
	uint32_t i, j, c;
	size_t reslen, minreslen, maxreslen;	

	reslen = hte->reslen;
	if (reslen < MIN_TILE_LEN || reslen > MAX_TILE_LEN) {
		verbose(3, "Ignoring entry because not in tile range!: %lu\n",
			reslen);
		return;
	}

	if (matches->off == matches->max) {
		warning("Cannot add new matches, match limit is reached!\n");
		return;
	}

	/* establish the lower and upper bounds for the match search */
	minreslen = reslen - TILE_LEN_RANGE;
	if (minreslen < MIN_TILE_LEN || minreslen > reslen)
		minreslen = MIN_TILE_LEN;
	maxreslen = reslen + TILE_LEN_RANGE;
	if (maxreslen > MAX_TILE_LEN || maxreslen < reslen)
		maxreslen = MAX_TILE_LEN;

	/* build the list of all matches for the input HTTP request */
	for (i=minreslen;i<=maxreslen;i++) {

		pflist = map_get(profilemap, i);
		if (!pflist) continue;

		c = list_count(pflist);
		for (j=0;j<c;j++) {
			if (list_get(pflist, j, &pe) < 0) {
				fatal("Unexpected error in list_get");
			}

			/* quick sanity check */
			if (pe.z < 0 || pe.z >= MAX_Z ||
				pe.x < 0 || pe.x >= MAX_X ||
				pe.y < 0 || pe.y >= MAX_Y)
				continue;

			map = matches->xmaps[pe.z][matches->off];
			ylist = map_get(map, pe.x);
			if (!ylist) {
				ylist = list_new(sizeof(uint32_t));
				if (!ylist) fatal("Out of memory.");
				if (map_set(map, pe.x, ylist) < 0)
					fatal("Out of memory.");
			}

			if (list_append(ylist, &(pe.y)) < 0)
				fatal("Out of memory.");

			map_set(matches->xseen, pe.x, (void *)1);
		}
	}

	matches->off++;
	return;
}

static void
add_segment_variations(struct map * results, uint32_t x, 
	uint32_t first_y, uint32_t last_y)
{
	struct list * list;
	uint32_t i, j;
	uint32_t tmp[2];

	for (i=first_y;i<last_y+1;i++) {
		for (j=i;j<last_y+1;j++) {
			list = map_get(results, x);
			if (!list) {
				list = list_new(sizeof(tmp));
				if (map_set(results, x, list) < 0)
					fatal("Out of memory.");
			}
			tmp[0] = i;
			tmp[1] = j;

			if (!list_contains(list, &tmp)) {
				if (list_append(list, &tmp) < 0) {
					fatal("Out of memory.");
				}
				verbose(3,
				"Found line segment from (%i,%i) - (%i,%i)\n",
				x,i,x,j);
			}
		}
	}	
}


static struct map *
get_line_segments(struct list * xlist, struct map ** maps, size_t mapcount)
{
	uint32_t * intptr;
	uint32_t xcount, ycount, listcount, i, x, y, c, k;
	uint32_t first_y, last_y, tmp_y;
	struct map * results, * xmap, * ymap;
	struct list * list, * ysortedlist;

	results = map_new(1009);
	ymap = map_new(1009);	
	if (!results || !ymap) fatal("Out of memory.");

	xcount = list_count(xlist);
	for (i=0;i<xcount;i++) {

		if (list_get(xlist, i, &x) < 0) 
			fatal("Unexpected error in list_get");

		if (!ymap) {
			ymap = map_new(1009);
			if (!ymap) fatal("Out of memory.");
		}

		for (c=0;c<mapcount;c++) {
			xmap = maps[c];	
			if (!xmap) continue;
			list = map_get(xmap, x);	
			if (!list) continue;

			listcount = list_count(list);
			for (k=0;k<listcount;k++) {
				if (list_get(list, k, &y) < 0) 
					fatal("Unexpected error in list_get");
				intptr = map_get(ymap, y);	
				if (!intptr) intptr = (void *)1;
				else intptr++;
				map_set(ymap, y, intptr);
			}
		}

		ycount = map_count(ymap);
		if (!ycount) continue;

		ysortedlist = map_getkeys(ymap, 1);
		list_get(ysortedlist, 0, &first_y);
		last_y = first_y;
		tmp_y = 0;

		for (c=1;c<ycount;c++) {
			list_get(ysortedlist, c, &tmp_y);
			if (tmp_y == last_y + 1) {
				last_y = tmp_y;
			}		
			else if (first_y != last_y) {
				add_segment_variations(results, x,
					first_y, last_y);
			}
			else {
				first_y = last_y = tmp_y;
			}
		}

		list_free(ysortedlist);
		map_free(ymap, NULL);
		ymap = NULL;
	}

	if (ymap) map_free(ymap, NULL);

	return results;
}

inline static void
find_retangles_for_zoomlevel(struct list * retangles,
	struct matches * matches, struct list * xvals,
	uint32_t z)
{
	struct coord coord;
	struct retangle retangle;
	struct list * keylist, * ylist, * ylist2;
	struct map * xsegs;
	uint32_t keycount, i, j, k, l, ycount, ycount2, new_x, found;
	uint32_t x, x2, y[2], y2[2];
	uint32_t dim, mindim, maxdim;

	xsegs = get_line_segments(xvals, matches->xmaps[z], matches->off);	
	mindim = 3;
	maxdim = 18;
	
	keylist = map_getkeys(xsegs, 1);
	keycount = list_count(keylist);
	for (i=0;i<keycount;i++) {
		list_get(keylist, i, &x);
		ylist = map_get(xsegs, x);
		ycount = list_count(ylist);	

		for (j=0;j<ycount;j++) {
			list_get(ylist, j, &y);
			new_x = x;

			for(k=i+1;k<keycount;k++) {
				found = 0;

				list_get(keylist, k, &x2);
				ylist2 = map_get(xsegs, x2);
				ycount2 = list_count(ylist2);
				
				for(l=0;l<ycount2;l++) {
					list_get(ylist2, l, &y2);
					if (y[0]==y2[0] && y[1]==y2[1]) {
						found = 1;
						new_x = x2;
						break;
					}
				}
				if (!found)
					break;
			}
			if (new_x != x) {
				dim = ((y[1]-y[0]+1) * (x2-x+1));
				if (dim >= mindim && dim <= maxdim) {

					retangle.z = z;
					retangle.c1.x = x;
					retangle.c1.y = y[0];
					retangle.c2.x = x;
					retangle.c2.y = y[1];	
					retangle.c3.x = new_x;
					retangle.c3.y = y[0];
					retangle.c4.x = new_x;
					retangle.c4.y = y[1];

					coord.x = x + ((new_x - x + 1)/2);
					coord.y = y[0] + ((y[1] - y[0] + 1)/2);

					tile_to_coord(z, &coord, 0, 0,
						&(retangle.lat), &(retangle.lng)
					);

					if (list_append(retangles,
						&retangle) < 0) {
						fatal("Out of memory.");
					}

					verbose(3, "Retangle with z:%u, dim:%i"
						" ,[(%u,%u),(%u,%u),"
						"(%u,%u),(%u,%u)] at"
						" %lf,%lf\n",
						z, dim,
						x, y[0], x, y[1],
						new_x, y[0], new_x, y[1],
						retangle.lat, retangle.lng);
				}
			}
		}
	}

	list_free(keylist);
	map_free(xsegs, _list_free);

	return;
}

static struct list *
find_retangles(struct matches * matches)
{
	struct list * xvals, * retangles;
	uint32_t z;

	verbose(2, "Looking for retangles\n");

	retangles = list_new(sizeof(struct retangle));

	xvals = map_getkeys(matches->xseen, 0);
	if (!xvals) fatal("Out of memory.");

	for (z=0;z<MAX_Z;z++) {
		find_retangles_for_zoomlevel(retangles, matches, xvals, z);
	}
	list_free(xvals);

	return retangles;
}

static void
analyze(time_t first_ts, time_t last_ts)
{
	struct retangle r;
	struct http_entry hte;
	struct matches * matches;
	struct map * latmap, * lngmap;
	struct list * htelist, * retangles, * keys;
	uint32_t c, i, j, rcount, ilat, ilng, * intptr;
	double dlat, dlng, dc, scale;

	verbose(2, "Analyzing time frame of %us\n", last_ts - first_ts);

	matches = matches_new();
	
	/* Retrieve the HTTP response sizes and add them to the matches 
	   if the corresponding HTTP request size fits with the average 
	   HTTP response size for satellite tiles (as computed above by
	   means of the histogram). */
	for (i=first_ts;i<=last_ts;i++) {
		htelist = map_get(tsmap, i);
		if (!htelist) continue;
		
		c = list_count(htelist);	
		for(j=0;j<c;j++) {
			if (list_get(htelist, j, &hte) < 0) {
				fatal("Unexpected error in list_get");
			}
			matches_add(matches, &hte);
		}
	}

	retangles = find_retangles(matches);
	rcount = list_count(retangles);
	if (!rcount) verbose(2, "No retangles found\n");
	else verbose(2, "Found %u retangle%s\n", rcount, (rcount == 1?"":"s"));

	/* The retangles have been found and their lat/lng values have been
	   calcuated. Add all the retangles to a histogram and cluster the
	   information on their lat/lng values. Based on that infer the actual
	   locations the user is looking at. We cheat again and just use the
	   map entry pointer as the counter. */
	latmap = map_new(1009);
	lngmap = map_new(1009);
	scale = 10000.0;
	for(i=0;i<rcount;i++) {
		if (list_get(retangles, i, &r) < 0)
			fatal("Unexpected error in list_get");
		ilat = (uint32_t)(r.lat * scale);
		ilng = (uint32_t)(r.lng * scale);
		if (map_get(latmap, ilat) < 0) map_set(latmap, ilat, (void *)1);
		else map_set(latmap, ilat, map_get(latmap, ilat) + 1);
		if (map_get(lngmap, ilng) < 0) map_set(lngmap, ilng, (void *)1);
		else map_set(lngmap, ilng, map_get(lngmap, ilng) + 1);
	}

	rcount = map_count(latmap);
	keys = map_getkeys(latmap, 1);
	dlat = 0;
	dc = 0.0;
	for (i=0;i<rcount;i++) {
		list_get(keys, i, &c);
		intptr = map_get(latmap, c);
		dlat += ((double)(((int)c)/scale) * *(uint32_t *)&intptr);
		dc+=(1.0 * *(uint32_t *)&intptr);
	}
	dlat = dlat/dc;
	list_free(keys);

	rcount = map_count(lngmap);
	keys = map_getkeys(lngmap, 1);
	dlng = 0;
	dc = 0.0;
	for (i=0;i<rcount;i++) {
		list_get(keys, i, &c);
		intptr = map_get(lngmap, c);
		dlng += ((double)(((int)c)/scale) * *(uint32_t *)&intptr);
		dc+=(1.0 * *(uint32_t *)&intptr);
	}
	dlng = dlng/dc;
	list_free(keys);

	verbose(0, "Lat: %lf, Lng: %lf\n", dlat, dlng);

	map_free(latmap, NULL);
	map_free(lngmap, NULL);
	list_free(retangles);
	matches_free(matches);
}

static void
run_analyzer()
{
	char cmd;
	time_t first_ts, last_ts;
	struct list * list;
	struct http_entry hte;
	struct timeval tv;
	int ret, analyze_do, status, stop_after_analyze;
	fd_set rfds;

	tsmap = map_new(TSMAP_HASHSIZE);
	first_ts = last_ts = stop_after_analyze = 0;

	while (1) {

		analyze_do = 0;
		FD_ZERO(&rfds);
		FD_SET(capture_fd, &rfds);

		/* In live analysis mode we set a timeout, in the offline one
                   we really don't care and will just analyze after each passed
		   timeframe based on the timestamps of the received bursts. */
		if (live_mode) {
			tv.tv_sec = 1;
			tv.tv_usec = 0;
			if (!first_ts) first_ts = time(NULL);
		}
		else {
			tv.tv_sec = 0;
			tv.tv_usec = 0;
		}

		do {
			ret = select(capture_fd + 1, &rfds, NULL, NULL, &tv);
		}
		while (ret < 0 && errno == EINTR);

		if (child_died) {
			/* child is done reading packets from PCAP file */
			if (!live_mode) {
				last_ts = first_ts + 2;
				analyze_do = 1;
				stop_after_analyze = 1;
			}
			else {
				warning("Child died unexpectedly. Exitting.\n");
				break;
			}
		}
		else if (int_received) {
			warning("SIGINT received. Exitting.\n");
			break;
		}
		else if (FD_ISSET(capture_fd, &rfds)) {
			/* read new HTTP req/res pairs into the timestamp map */

			read(capture_fd, &cmd, 1);
			if (cmd == 'E') {
				memset(&hte, 0, sizeof(struct http_entry));
				ret = read(capture_fd, &hte,
					sizeof(struct http_entry));
			}	
			else fatal("Invalid message from capture process");

			verbose(3, "read new HTTP req/res pair: %u,%u\n",
				hte.reqlen, hte.reslen);

			list = map_get(tsmap, hte.ts);
			if (!list) {
				list = list_new(sizeof(struct http_entry));
				if (!list) fatal("Out of memory.");

				ret = map_set(tsmap, hte.ts, list);
				if (ret < 0) fatal("Out of memory.");
			}
			ret = list_append(list, &hte);
			if (ret < 0) fatal("Out of memory");

			if (!live_mode) {
				if (!first_ts) first_ts = hte.ts;
				last_ts = hte.ts;
			}
		}

		/* Determine if the timestamp interval exceeded the
		   the limit and start analysis of the collection of
		   requests in that interval. */
		if (live_mode) {
			last_ts = time(NULL);
			if (last_ts - first_ts >= 2) {
				analyze_do = 1;	
			}
		}
		else if (last_ts - first_ts >= 2) {
			analyze_do = 1;
		}
		if (analyze_do) {
			analyze(first_ts, last_ts);
			last_ts = first_ts = 0;
			analyze_do = 0;
			if (stop_after_analyze) break;
		}
	}

	wait(&status);
	map_free(tsmap, _list_free);
}

static void
capture_callback(const struct burst * b)
{ 
	struct http_entry hte;
	struct list * list;
	struct burst bcmp;
	uint32_t i, lc;
	char cmd;

	verbose(3,
		"burst: %s, len: %lu, incp: %i, "
		"(%i.%i.%i.%i:%i) - (%i.%i.%i.%i:%i)\n",
		(b->client?"c->s":"s->c"), b->len, b->incomplete,
		(b->chost >> 24) & 0xff,
		(b->chost >> 16) & 0xff,
		(b->chost >> 8) & 0xff,
		(b->chost & 0xff),
		htons(b->cport),
		(b->dhost >> 24) & 0xff,
		(b->dhost >> 16) & 0xff,
		(b->dhost >> 8) & 0xff,
		(b->dhost & 0xff),
		htons(b->dport));

	if (!sessionmap) {
		sessionmap = map_new(SESSIONMAP_HASHSIZE);
		if (!sessionmap) {
			fatal("Out of memory.");
		}
	}

	list = map_get(sessionmap, b->hash);
	if (!list) {
		list = list_new(sizeof(struct burst));
		if (!list) {
			fatal("Out of memory");
		}
		if (map_set(sessionmap, b->hash, list) < 0) {
			fatal("Out of memory");
		}
	}
	memcpy(&bcmp, b, sizeof(struct burst));
	if (list_append(list, &bcmp) < 0) {
		fatal("Out of memory");
	}

	/* Check the lookup table if it is a server response and try to find
	   the matching client request and pass these on. */
	if (!b->client) {
		lc = list_count(list);
		for(i=lc-1;lc >= 1 && i>0;i--) {
			if (list_get(list, i-1, &bcmp) < 0) {
				fatal("Unexpected error in list_get");
			}
			if (bcmp.chost == b->chost &&
				bcmp.dhost == b->dhost &&
				bcmp.cport == b->cport &&
				bcmp.dport == b->dport &&
				bcmp.client && bcmp.hash == b->hash) {

				if (live_mode) hte.ts = time(NULL);

				hte.reqlen = bcmp.len;
				hte.reslen = b->len;
				hte.ts = b->ts;

				cmd = 'E';
				write(analyze_fd, &cmd, 1);
				write(analyze_fd, &hte,
					sizeof(struct http_entry));

				/* XXX: remove the entries from the list */
				break;
			}
		}
	}
}

static void
signal_child(int sig)
{
	if (int_received) return;
	child_died = 1;
}

static void
signal_int(int sig)
{
	int_received = 1;
}

static void
signal_pipe(int sig)
{
	trafficker_breakloop(tr);
}

static int
run_capture_child(struct trafficker * tr)
{
	int pipefd[2], ret;
	pid_t pid;

	if (!tr) return -1;

	ret = pipe(pipefd);
	if (ret < 0) return -1;

	pid = fork();
	if (pid == -1) return -1;
	if (pid) {
		return pipefd[0];
	}

	analyze_fd = pipefd[1];
	signal(SIGPIPE, signal_pipe);
	trafficker_loop(tr, capture_callback);
	trafficker_close(tr);
	map_free(sessionmap, _list_free);
	profile_unload(profilemap);
	exit(EXIT_SUCCESS);
}

static void
resolve_host(struct map * ipmap, const char * host)
{
	struct in_addr in;
	struct addrinfo * result, * ai;
	struct addrinfo hints;
	uint32_t ip;
	int ret;

	verbose(2, "Trying to resolve %s\n", host);

	memset(&hints, 0, sizeof(struct addrinfo));
	hints.ai_family = AF_INET; /* libnids only supports IPv4 anyway */
	hints.ai_socktype = SOCK_STREAM;
	ret = getaddrinfo(host, NULL, &hints, &result);
	if (ret < 0) fatal("Cannot resolve host");

	for (ai=result; ai != NULL; ai=ai->ai_next) {
		if (ai->ai_family != AF_INET || 
				ai->ai_addrlen != sizeof(struct sockaddr_in))
			continue;
		ip = ((struct sockaddr_in *)ai->ai_addr)->sin_addr.s_addr;
		in.s_addr = ip;
		verbose(3, "Host %s resolves to %s\n", host, inet_ntoa(in));
	}

	freeaddrinfo(result);
}

static void
usage(const char * arg0)
{
	fprintf(stderr, "%s -L/-O <arg> [options]\n", arg0);
	fprintf(stderr, "Do gmaps traffic analysis on a");
	fprintf(stderr, " live pcap session or an offline session.\n\n");
	fprintf(stderr, "-L <device>    - live device to capture on\n");
	fprintf(stderr, "-O <filename>  - offline pcap file to read from\n\n");
	fprintf(stderr, "-f <profile>   - profile datafile");
	fprintf(stderr, " (default: ./%s)\n", DEFAULT_FN);
	fprintf(stderr, "-i <iplist>    - text file with IPv4 addresses of");
	fprintf(stderr, " the gmap servers.\n");
	fprintf(stderr, "-u <user>      - privdrop to this user\n");
	fprintf(stderr, "-c             - colorize output\n");
	fprintf(stderr, "-v             - be verbose (use multiple times");
	fprintf(stderr, " for greater effect)\n");
	fprintf(stderr, "-h             - usage information\n");
}

int
main(int argc, char ** argv, char ** envp)
{
	FILE * f;
	char buf[128];
	struct map * ipmap;
	struct list * keys;
	struct in_addr in;
	uint32_t keycount, i;
	const char * arg0 = NULL, * iplistfn = NULL;
	char * live = NULL, * offline = NULL, * user = NULL, * filter;
	char * profile = DEFAULT_FN;
	int c, ret;

	arg0 = (argc > 0 ? argv[0] : "(unknown)");
	while ((c = getopt(argc, argv, "hL:O:f:u:vi:c")) != -1) {
		switch (c) {
			case 'c':
				colorize_output = 1;
				break;
			case 'h':
				usage(arg0);
				exit(EXIT_FAILURE);
				break; /* not reached */
			case 'v':
				verbose_level++;
				break;
			case 'L':
				live = optarg;
				live_mode = 1;
				break; 
			case 'O':
				offline = optarg;
				live_mode = 0;
				break;
			case 'i':
				iplistfn = optarg;
				break;
			case 'f':
				profile = optarg;
				break;
			case 'u':
				user = optarg;
				break;
		}
	}

	if (live && offline) {
		fprintf(stderr, "Cannot use -L and -O at the same time.");
		fprintf(stderr, " Use -h for info.\n");
		exit(EXIT_FAILURE);
	}
	else if (!live && !offline) {
		fprintf(stderr, "No analysis mode selection.");
		fprintf(stderr, " Use -h for info.\n");
		exit(EXIT_FAILURE);
	}

	profilemap = profile_load(profile);
	if (!profilemap) {
		fprintf(stderr, "Cannot load profile.\n");
		exit(EXIT_FAILURE);
	}

	/* Construct the PCAP filter: first get the list of available IP's
	   to filter on either by getting the specified list of IP's or by
	   doing resolving DNS entries for the Google Maps servers. Then built
	   the filter based on this map. */

	ipmap = map_new(1009);
	if (!iplistfn) {
		verbose(2, "No IPv4 list specified, start DNS resolving\n");
		resolve_host(ipmap, "khms0.google.com");
		resolve_host(ipmap, "khms1.google.com");
		resolve_host(ipmap, "khms2.google.com");
		resolve_host(ipmap, "khms3.google.com");
	}
	else {
		verbose(2, "Parsing the supplied list of IPv4 addresses\n");
		f = fopen(iplistfn, "r");
		if (!f) fatal("Cannot open file with IPv4 addresses");
		do {
			memset(buf, 0, sizeof(buf));
			fgets(buf, sizeof(buf)-1, f);
			if (!strlen(buf)) break;
			ret = inet_aton(buf, &in);
			map_set(ipmap, in.s_addr, NULL);
		} while (ret > 0 && !feof(f));
		fclose(f);
		if (!ret)
			fatal("Error while parsing file. Only supportes one "
			"IP address per line");
	}
	keys = map_getkeys(ipmap, 0);
	keycount = list_count(keys);
	if (!keycount) fatal("No hosts specified to use for the filter");

	filter = xmalloc((15 + strlen(" or host ")) * keycount +
		strlen(") and port 443"));

	list_get(keys, 0, &(in.s_addr));
	strcat(filter, "(host ");
	strcat(filter, inet_ntoa(in));
	for(i=1;i<keycount;i++) {
		list_get(keys, i, &(in.s_addr));
		strcat(filter, " or host ");
		strcat(filter, inet_ntoa(in));
	}
	strcat(filter, ") and port 443");
	list_free(keys);	
	map_free(ipmap, NULL);

	verbose(3, "Using PCAP filter of '%s'\n", filter);

	/* open the libtrafficker session with the built up filter */
	if (live) tr = trafficker_open_online(live, filter);
	else tr = trafficker_open_offline(offline, filter);
	if (!tr) {
		fprintf(stderr, "Error while opening pcap file/stream!\n");
		exit(EXIT_FAILURE);
	}
	free(filter);

	/* privdrop if requested */
	if (user) privdrop(user);

	/* join individual bursts until a data direction switch occurs. */
	trafficker_set_burstjoin(tr, 1);

	/* run the capturing child process */
	capture_fd = run_capture_child(tr);
	if (capture_fd < 0) {
		trafficker_close(tr);
		profile_unload(profilemap);
		fprintf(stderr, "Cannot open capture child.\n");
		exit(EXIT_FAILURE);
	}

	signal(SIGCHLD, signal_child);
	signal(SIGINT, signal_int);

	run_analyzer();

	/* cleanup */
	trafficker_close(tr); /* XXX: move tr ref only to capture */
	profile_unload(profilemap);

	exit(EXIT_SUCCESS);
}

/* EOF */
