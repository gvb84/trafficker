/* utils.c */

#include <arpa/inet.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <unistd.h>
#include <pwd.h>

#include "utils.h"

void *
xmalloc(size_t len)
{
	void * p;

	p = malloc(len);
	if (!p) {
		fprintf(stderr, "Out of memory.\n");
		exit(EXIT_FAILURE);
	}
	memset(p, 0, len);
	return p;
}

uint32_t
read_uint32(FILE * f)
{
	size_t ret;
	uint32_t t;

	ret = fread(&t, 4, 1, f);
	if (ret != 1) {
		fprintf(stderr, "Error while reading from file.\n");
		exit(EXIT_FAILURE);
	}

	t = ntohl(t);
	return t;
}

uint16_t
read_uint16(FILE * f)
{
	size_t ret;
	uint16_t t;

	ret = fread(&t, 2, 1, f);
	if (ret != 1) {
		fprintf(stderr, "Error while reading from file.\n");
		exit(EXIT_FAILURE);
	}

	t = ntohs(t);
	return t;
}

uint8_t
read_uint8(FILE * f)
{
	size_t ret;
	uint8_t t;

	ret = fread(&t, 1, 1, f);
	if (ret != 1) {
		fprintf(stderr, "Error while reading from file.\n");
		exit(EXIT_FAILURE);
	}
	return t;
}

void
write_uint32(FILE * f, uint32_t t)
{
	size_t ret;
	t = htonl(t);
	ret = fwrite(&t, 4, 1, f);
	if (ret != 1) {
		fprintf(stderr, "Error while writing to file.\n");
		exit(EXIT_FAILURE);
	}
}

void
write_uint16(FILE * f, uint16_t t)
{
	size_t ret;
	t = htons(t);
	ret = fwrite(&t, 2, 1, f);
	if (ret != 1) {
		fprintf(stderr, "Error while writing to file.\n");
		exit(EXIT_FAILURE);
	}
}

void
write_uint8(FILE * f, uint8_t t)
{
	size_t ret;
	ret = fwrite(&t, 1, 1, f);
	if (ret != 1) {
		fprintf(stderr, "Error while writing to file.\n");
		exit(EXIT_FAILURE);
	}
}

inline static void
passwd_clear(struct passwd * pwd)
{
	if (!pwd) fatal("passwd_clear called with null");

	/* check for existance of each pointer and clear the string */
	if (pwd->pw_name)
		memset(pwd->pw_name, 0, strlen(pwd->pw_name));
	if (pwd->pw_passwd)
		memset(pwd->pw_passwd, 0, strlen(pwd->pw_passwd));
	if (pwd->pw_gecos)
		memset(pwd->pw_gecos, 0, strlen(pwd->pw_gecos));
	if (pwd->pw_dir)
		memset(pwd->pw_dir, 0, strlen(pwd->pw_dir));
	if (pwd->pw_shell)
		memset(pwd->pw_shell, 0, strlen(pwd->pw_shell));

	/* clear the passwd struct itself */
	memset(pwd, 0, sizeof(struct passwd));
}

void
fatal(const char * fmt)
{
	fprintf(stderr, "%s\n", fmt);
	exit(EXIT_FAILURE);
}

void
privdrop(const char * username)
{
	uid_t uid;
	gid_t gid, saved_egid, saved_rgid;
	struct passwd * pwd;
	int ret;

	saved_egid = getegid();
	saved_rgid = getgid();

	pwd = getpwnam(username);
	if (!pwd) fatal("getpwnam failed so cannot privdrop");

	uid = pwd->pw_uid;
	gid = pwd->pw_gid;

	ret = setregid(gid, gid);
	if (ret < 0) goto err;

	ret = setreuid(uid, uid);
	if (ret < 0) goto err;

	passwd_clear(pwd);
	return;
err:
	passwd_clear(pwd);
	fatal("privdrop failed");
}

/* EOF */
