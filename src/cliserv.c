#include "config.h"
#include "cliserv.h"

#include <netdb.h>
#include "netdb-compat.h"

#include <sys/socket.h>
#include <sys/types.h>

#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <syslog.h>
#include <unistd.h>

const uint64_t cliserv_magic = 0x00420281861253LL;
const uint64_t opts_magic = 0x49484156454F5054LL;
const uint64_t rep_magic = 0x3e889045565a9LL;

/**
 * Set a socket to blocking or non-blocking
 *
 * @param fd The socket's FD
 * @param nb non-zero to set to non-blocking, else 0 to set to blocking
 * @return 0 - OK, -1 failed
 */
int set_nonblocking(int fd, int nb) {
	int sf = fcntl (fd, F_GETFL, 0);
	if (sf == -1)
		return -1;
	return fcntl (fd, F_SETFL, nb ? (sf | O_NONBLOCK) : (sf & ~O_NONBLOCK));
}

void err_nonfatal(const char *s) {
	char s1[150], *s2;

	strncpy(s1, s, sizeof(s1));
	if ((s2 = strstr(s, "%m"))) {
		strcpy(s1 + (s2 - s), strerror(errno));
		s2 += 2;
		strcpy(s1 + strlen(s1), s2);
	}
#ifndef	sun
	/* Solaris doesn't have %h in syslog */
	else if ((s2 = strstr(s, "%h"))) {
		strcpy(s1 + (s2 - s), hstrerror(h_errno));
		s2 += 2;
		strcpy(s1 + strlen(s1), s2);
	}
#endif

	s1[sizeof(s1)-1] = '\0';
#ifdef ISSERVER
	syslog(LOG_ERR, "%s", s1);
	syslog(LOG_ERR, "Exiting.");
#endif
	fprintf(stderr, "Error: %s\n", s1);
}

void err(const char *s) {
	err_nonfatal(s);
	fprintf(stderr, "Exiting.\n");
	exit(EXIT_FAILURE);
}

void logging(const char* name) {
#ifdef ISSERVER
	openlog(name, LOG_PID, LOG_DAEMON);
#endif
	setvbuf(stdout, NULL, _IONBF, 0);
	setvbuf(stderr, NULL, _IONBF, 0);
}

#ifndef ntohll
#ifdef WORDS_BIGENDIAN
uint64_t ntohll(uint64_t a) {
	return a;
}
#else
uint64_t ntohll(uint64_t a) {
	uint32_t lo = a & 0xffffffff;
	uint32_t hi = a >> 32U;
	lo = ntohl(lo);
	hi = ntohl(hi);
	return ((uint64_t) lo) << 32U | hi;
}
#endif
#endif

/**
 * Read data from a file descriptor into a buffer
 *
 * @param f a file descriptor
 * @param buf a buffer
 * @param len the number of bytes to be read
 **/
void readit(int f, void *buf, size_t len) {
	ssize_t res;
	while (len > 0) {
		res = read(f, buf, len);
		if (res > 0) {
			len -= res;
			buf += res;
		} else if (res < 0) {
			if(errno != EAGAIN) {
				err("Read failed: %m");
			}
		} else {
			err("Read failed: End of file");
		}
	}
}

/**
 * Write data from a buffer into a filedescriptor
 *
 * @param f a file descriptor
 * @param buf a buffer containing data
 * @param len the number of bytes to be written
 * @return 0 on success, or -1 if the socket was closed
 **/
int writeit(int f, void *buf, size_t len) {
    ssize_t res;
    while (len > 0) {
        if ((res = write(f, buf, len)) <= 0) {
            switch(errno) {
                case EAGAIN:
                    break;
                default:
                    err_nonfatal("Send failed: %m");
                    return -1;
            }
        }
        len -= res;
        buf += res;
    }
    return 0;
}

