/* ssl.h */

#define SSL_MSG_CHANGE_CIPHER_SPEC     20
#define SSL_MSG_ALERT                  21
#define SSL_MSG_HANDSHAKE              22
#define SSL_MSG_APPLICATION_DATA       23

struct ssl_parse {
	size_t total_read;
	int record_type;	
	char * data;
	size_t data_len;
};

int ssl_parse(char *, size_t, struct ssl_parse *);

/* EOF */
