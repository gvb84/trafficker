/* trafficker */

#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <netpacket/packet.h>
#include <net/if.h>
#include <net/ethernet.h>
#include <linux/if_ether.h>
#include <arpa/inet.h>

static int fd = -1;
static int from_file;
static int linktype;
static int pcap_swap;
static unsigned char * pktbuf = NULL;
static uint32_t pktbuflen = 0;

int
sniff_open_live(const char * iface, int raw)
{
	int ret;
	struct ifreq ifr;
	struct sockaddr_ll sl;

	if (fd != -1) return -1;
	fd = socket(PF_PACKET, raw ? SOCK_RAW : SOCK_DGRAM, ETH_P_ALL);
	if (fd == -1) return -1;

	if (iface) {
		sl.sll_family = AF_PACKET;
		sl.sll_protocol = htons(ETH_P_ALL);
		sl.sll_ifindex = if_nametoindex(iface);
	
		ret = bind(fd, (struct sockaddr *)&sl, sizeof(sl));
		if (ret == -1) {
			close(fd);
			fd = -1;
			return -1;
		}	
		strncpy(ifr.ifr_name, iface, IFNAMSIZ);
	}
	else {
		if_indextoname(1, ifr.ifr_name);
	}

	ret = ioctl(fd, SIOCGIFHWADDR, &ifr);
	if (ret == -1) {
		close(fd);
		fd = -1;
		return -1;
	}	

	from_file = 0;
	return ifr.ifr_hwaddr.sa_family;
}

int
sniff_open_file(const char * filename)
{
	struct {
		uint32_t magic;
		uint16_t version_major, version_minor;
		uint32_t thiszone;
		uint32_t sigfigs;
		uint32_t snaplen;
		uint32_t linktype;
	} pcap_header;
	int ret;

	if (fd != -1) return -1;

	fd = open(filename, O_RDONLY);
	if (fd == -1) return -1;	

	ret = read(fd, &pcap_header, sizeof(pcap_header));
	if (ret != sizeof(pcap_header) || pcap_header.magic != 0xa1b2c3d4) {
		close(fd);
		fd = -1;
		return -1;
	}

	linktype = pcap_header.linktype;
	pcap_swap = (pcap_header.version_major < 2 ||
		(pcap_header.version_major == 2 &&
		pcap_header.version_minor <= 3));
	from_file = 1;
	return linktype;
}

static inline int
sniff_packet_live(unsigned char ** buf, struct sockaddr_ll * from,
	struct timeval * tv)
{
	int len, ret;
	socklen_t fromlen;

	fromlen = sizeof(*from);

	if (!pktbuf) {
		pktbuf = malloc(16 * 1024);
		if (!pktbuf) return -1;
		pktbuflen = 16 * 1024;
	}

	len = recvfrom(fd, pktbuf, pktbuflen, 0,
		(struct sockaddr *)from, &fromlen);
	if (len < 0) return -1;
	ret = ioctl(fd, SIOCGSTAMP, tv);
	if (ret < 0) return -1;
	*buf = pktbuf;
	
	return len;
}

static inline int
sniff_packet_file(unsigned char ** buf, struct sockaddr_ll * from,
	struct timeval * tv)
{
	struct pcap_timeval {
		int32_t tv_sec;
		int32_t tv_usec;
	};
	struct pcap_sf_pkthdr {
		struct pcap_timeval ts;
		uint32_t caplen;
		uint32_t len;
	} p;
	unsigned char * c;
	int ret;

	/* read pcap packet header */
	ret = read(fd, (char *)&p, sizeof(p));
	if (ret != sizeof(p)) return (ret == 0 ? 0 : -1);

	/* make sure there's enough space for the entire packet */
	if (p.caplen > pktbuflen) {
		c = realloc(pktbuf, p.caplen);
		if (!c) return -1;
		pktbuf = c;	
		pktbuflen = p.caplen;
	}

	/* read packet */
	ret = read(fd, pktbuf, p.caplen);
	if (ret != p.caplen) return -1;

	*buf = pktbuf;
	tv->tv_sec = p.ts.tv_sec;
	tv->tv_usec = p.ts.tv_usec;

	if (linktype == 113) {
		/* Fix "Linux cooked" mode */
		c = *buf;
		memset(c, 12, 0);
		memcpy(c + 12, c + 14, 2);
		memmove(c + 14, c + 16, p.len - 14);
		return p.len - 2;
	}

	return p.len;
}

int
sniff_packet(unsigned char ** buf, struct sockaddr_ll * from,
	struct timeval * tv)
{
	if (fd == -1) return -1;

	return (from_file ? sniff_packet_file(buf, from, tv) :
		sniff_packet_live(buf, from, tv));
}

void
sniff_close()
{
	if (fd != -1) close(fd);
	if (pktbuf) free(pktbuf);
	fd = -1;
	pktbuf = NULL;
	pktbuflen = 0;
}

int
main(int argc, char ** argv, char ** envp)
{
	unsigned char * buf;
	struct timeval tv;
	struct sockaddr_ll from;
	int ret;

	ret = sniff_open_file("wpa-induction.pcap");
	//ret = sniff_open_live("lo", 1);
	printf ("!%x\n", ret);
	
	while ((ret = sniff_packet(&buf, &from, &tv)) != -1) {
		if (ret == 0) break;
		printf("?%x\n", ret);
	}

	sniff_close();

	return 0;
}

/* EOF */
