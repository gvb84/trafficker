/* ssl.c */

#include <stdlib.h>
#include <unistd.h>

#include "ssl.h"

/* Returns -1 if the parsing failed, 0 if the parsing succeeded
   and only in the case of success will the ssl_parse structure
   be filled. */
int
ssl_parse(char * ibuf, size_t len, struct ssl_parse * ret)
{
	char * data = NULL;
	size_t msg_len, total_read = 0, record_type;
	size_t data_len = 0;
	unsigned char * buf;

	if (len < 5) return -1;

	buf = (unsigned char *)(ibuf);
	if (buf[0] & 0x80) {
		msg_len = ((buf[0] & 0x7f) << 8) | buf[1];
		if (buf[2] != 1 || buf[3] != 3)
			return -1;

		total_read = msg_len + 2;	
		record_type = SSL_MSG_HANDSHAKE;
	}	
	else if ((buf[0] == SSL_MSG_HANDSHAKE  ||
			buf[0] == SSL_MSG_ALERT ||
			buf[0] == SSL_MSG_APPLICATION_DATA ||
			buf[0] == SSL_MSG_CHANGE_CIPHER_SPEC)
			&& buf[1] == 3) {
		msg_len = (buf[3] << 8) |  buf[4];
		record_type = buf[0];
		buf += 5;
		len -= 5;
		if (len < msg_len) {
			return -1;
		}
		total_read = msg_len + 5;
	}
	else return -1;

	switch (record_type) {
		case SSL_MSG_APPLICATION_DATA:
			data = (char *)buf;
			data_len = msg_len;
			break;
	}

	ret->total_read = total_read;
	ret->record_type = record_type;
	ret->data = data;
	ret->data_len = data_len;

	return 0;
}

/* EOF */
