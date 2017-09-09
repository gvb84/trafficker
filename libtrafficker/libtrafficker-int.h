/* libtrafficker-int.h */

#ifndef LIBTRAFFICKER_INT_H
  #define LIBTRAFFICKER_INT_H

#include <pcap.h>

#ifndef PCAP_NETMASK_UNKNOWN
  /* older versions of libpcap don't seem to define this */
  #define PCAP_NETMASK_UNKNOWN 0xffffffff
#endif

struct trafficker {
	size_t max_mem;
	size_t max_fork;
	int live_cap;
	int loop;
	int burst_join;
	pcap_t * pcap;
	void (*cb)(const struct burst *);
};

static struct trafficker * current;

struct tr_session {
	int first_burst;
	struct burst last_burst;
	struct buffer * sbuf;
	struct buffer * cbuf;
};

#endif

/* EOF */
