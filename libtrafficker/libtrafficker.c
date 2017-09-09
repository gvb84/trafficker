/* libtrafficker.c */

#include <stdlib.h>
#include <string.h>
#include <time.h>

#include <nids.h>

#include "libtrafficker.h"
#include "libtrafficker-int.h"
#include "ssl.h"
#include "buffer.h"
#include "hash.h"

extern struct pcap_pkthdr * nids_last_pcap_header;

static int
set_filter(pcap_t * pcap, const char * filter)
{
	struct bpf_program pf;
	int ret;

        ret = pcap_compile(pcap, &pf, filter, 1, PCAP_NETMASK_UNKNOWN);
	if (ret < 0) return -1;
	
	ret = pcap_setfilter(pcap, &pf);
	if (ret < 0) {
		pcap_freecode(&pf);
		return -1;
	}

	pcap_freecode(&pf);
	return 0;
}

static struct trafficker *
trafficker_open(pcap_t * pcap, const char * filter)
{
	struct trafficker * t;
	int ret;

	if (filter) {
		ret = set_filter(pcap, filter);
		if (ret < 0) return NULL;
	}

	t = malloc(sizeof(struct trafficker));
	if (!t) return NULL;

	memset(t, 0, sizeof(struct trafficker));

	t->pcap = pcap;

	init_hash();
	return t;
}

struct trafficker *
trafficker_open_offline(const char * fname, const char * filter)
{
	struct trafficker * t;
	char errbuf[PCAP_ERRBUF_SIZE];
	pcap_t * pcap;

	if (!fname) return NULL;

	pcap = pcap_open_offline(fname, errbuf);
	if (!pcap) return NULL;

	t = trafficker_open(pcap, filter);
	if (!t) {
		pcap_close(pcap);
		return NULL;
	}
	t->live_cap = 0;

	return t;
}

struct trafficker *
trafficker_open_online(const char * device, const char * filter)
{
	char errbuf[PCAP_ERRBUF_SIZE];
	pcap_t * pcap;
	struct trafficker * t;

	if (!device) return NULL;

	pcap = pcap_open_live(device, 65535, 1, 0, errbuf);
	if (!pcap) {
		return NULL;
	}

	t = trafficker_open(pcap, filter);
	if (!t) {
		pcap_close(pcap);
		return NULL;
	}
	t->live_cap = 1;

	return t;
}

static void
nids_tcp_callback(struct tcp_stream * t, void ** param)
{
	struct trafficker * tr = current;
	struct half_stream hs;
	struct burst burst;
	struct ssl_parse sslret;
	struct tr_session * session;
	struct buffer * buf;
	size_t datalen;
	int incomplete = 0, ret;
	char * p;
	uint32_t hash;

	hash = mkhash(t->addr.saddr, t->addr.source,
		t->addr.daddr, t->addr.dest);

	switch (t->nids_state) {
		case NIDS_JUST_EST:
			t->client.collect++;
			t->server.collect++;
			session = malloc(sizeof(struct tr_session));
			if (!session) return;
			session->sbuf = buffer_new();
			session->cbuf = buffer_new();
			session->first_burst = 1;
			memset(&(session->last_burst), 0, sizeof(struct burst));
			t->user = session;
			break;
		case NIDS_DATA:
			session = (struct tr_session *)(t->user);
			burst.hash = hash;
			if (t->server.count_new) {
				hs = t->server;
				buf = session->sbuf;
				burst.client = 1;
			}
			else if (t->client.count_new) {
				hs = t->client;
				buf = session->cbuf;
				burst.client = 0;
			}
			buffer_append(buf, hs.data, hs.count_new);

			p = buf->data;
			datalen = buf->len;
			burst.len = 0;
			burst.tr = tr;
			burst.chost = ntohl(t->addr.saddr);
			burst.dhost = ntohl(t->addr.daddr);
			burst.cport = ntohs(t->addr.source);
			burst.dport = ntohs(t->addr.dest);
			burst.incomplete = 0;
			burst.ts = (tr->live_cap ? time(NULL) :
				nids_last_pcap_header->ts.tv_sec);

			if (tr->burst_join && !session->first_burst) {
				if (session->last_burst.client ^ burst.client) {
					if (session->last_burst.len > 0)
						tr->cb(&(session->last_burst));
					memcpy(&(session->last_burst), &burst,
						sizeof(struct burst));
				}
				else burst.len = session->last_burst.len;
			}

			/* XXX: we want to discard this complete session
			   if it just doesn't look like SSL data at all
			   (so check this if it's the first data we got
			   and if it looks like valid SSL).
			*/
			while (datalen) {
				ret = ssl_parse(p, datalen, &sslret);
				if (ret < 0) break;
				p += sslret.total_read;
				datalen -= sslret.total_read;
				if (sslret.record_type == 23) {
					burst.len += sslret.data_len;
				}
			}

			/* All buffered SSL data successfully parsed */
			if (!datalen) {

				burst.ts = (tr->live_cap ? time(NULL) :
					nids_last_pcap_header->ts.tv_sec);

				/* Send burst if there's data in the burst and
				   the burst join option is not set. If the
				   option is set update the last burst info. */
				if (burst.len > 0) {
					if (!(tr->burst_join)) {
						tr->cb(&burst);
					}
				}
				if (tr->burst_join) {
					session->last_burst.len =
							burst.len;
					session->first_burst = 0;
				}

				buffer_reset(buf);
			}
			break;
		case NIDS_EXITING:
		case NIDS_RESET:
		case NIDS_TIMED_OUT:
			incomplete = 1;
		case NIDS_CLOSE:
			session = (struct tr_session *)(t->user);
	
			if (!session || !tr) break;

			/* Send the last buffered burst which might be 
			   incomplete. */
			if (tr->burst_join && session->last_burst.len > 0) {
				memcpy(&burst, &(session->last_burst),
					sizeof(struct burst));
				burst.incomplete = incomplete;
				tr->cb(&burst);
			}

			buffer_free(session->sbuf);
			buffer_free(session->cbuf);	
			free(session);
			break;
	}
}

int
trafficker_loop(struct trafficker * t, void (*callback)(const struct burst *))
{
	if (!t || !callback) return -1;

	t->cb = callback;
	t->loop = 1;
	current = t;

	nids_params.scan_num_hosts = 0;
	nids_params.pcap_desc = t->pcap;
	nids_init();
	nids_register_tcp(nids_tcp_callback);
	pcap_loop(t->pcap, -1, (pcap_handler)nids_pcap_handler, NULL); 
	nids_exit();

	return 0;
}

int
trafficker_breakloop(struct trafficker * t)
{
	if (!t || !(t->loop)) return -1;

	t->loop = 0;
	pcap_breakloop(t->pcap);

	return 0;
}

void
trafficker_close(struct trafficker * t)
{
	if (!t) return;

	pcap_close(t->pcap);
	current = NULL;
	free(t);
}

int
trafficker_set_burstjoin(struct trafficker * t, int join)
{
	if (!t || (join != 0 && join != 1)) return -1;

	t->burst_join = join;

	return 0;
}

int
trafficker_get_burstjoin(struct trafficker * t, int * join)
{
	if (!t || !join) return -1;

	*join = t->burst_join;

	return 0;
}

/* EOF */
