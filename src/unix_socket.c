#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <netdb.h>

#define IS_INVALID_SOCKET(a) (a < 0)

/**
 * connect to remote server
 * return
 * -1 socket create failed!
 * -2 server connect failed!
 * >0 connected to server successfully!
 */
int socket_open(const char * host, long port) {
    int sockfd = -1, flag;
    if ((sockfd = socket(AF_INET, SOCK_STREAM, 0)) == -1) {
        return -1;
    }
    flag = 1;
    if (setsockopt(sockfd, IPPROTO_TCP, TCP_NODELAY, (char *) &flag,
            sizeof(flag)) == -1) {
        close(sockfd);
        return -2;
    }

    struct sockaddr_in server;
    struct hostent *host_info;
    if ((host_info = gethostbyname(host)) == NULL) {
        close(sockfd);
        return -2;
    }

    bzero(&server, sizeof(server));
    server.sin_family = AF_INET;
    server.sin_addr = *(struct in_addr *) *host_info->h_addr_list;
    server.sin_port = htons(port);

    if (connect(sockfd, (struct sockaddr *) &server, sizeof(server)) == -1) {
        close(sockfd);
        return -2;
    }
    return sockfd;
}

/**
 * check is socket writable.
 * -3 socket select error.
 * 1 socket is readable.
 * 0 socket is not readable.
 */
int socket_writable(const int sockfd, long select_timeout) {
    if (IS_INVALID_SOCKET(sockfd))
        return SOCKET_INVALID;
    fd_set fdset;
    FD_ZERO(&fdset);
    FD_SET(sockfd, &fdset);
    int nw = 0;
    if (select_timeout < 0) {
        if ((nw = select(sockfd + 1, NULL, &fdset, &fdset, NULL)) == -1) {
            return SOCKET_SELECT_ERROR;
        }
        return nw;
    } else {
        struct timeval tv;
        tv.tv_sec = 0;
        tv.tv_usec = select_timeout;
        if ((nw = select(sockfd + 1, NULL, &fdset, &fdset, &tv)) == -1) {
            return SOCKET_SELECT_ERROR;
        }
        return nw;
    }
}

/**
 * socket write until buf_len.
 * return SOCKET_WRITE_ERROR or total write count.
 */
int socket_writeline(const int sockfd, char *buf, int buf_len,
        long select_timeout) {
    int nw = 0, wc = 0, twc = 0, wl = 0;
    if ((nw = socket_writable(sockfd, select_timeout)) != 1) {
        return nw;
    }

    char *tmp_buf = (char *) emalloc((buf_len + 2) * sizeof(char));
    if (tmp_buf == NULL)
        return ALLOCATE_FAILED;
    strcpy(tmp_buf, buf);
    tmp_buf[buf_len] = '\n';
    tmp_buf[buf_len + 1] = '\0';
    wl = strlen(tmp_buf);

    buf_len += 1;

    for (;;) {
        if ((wc = write(sockfd, tmp_buf + wc, wl - wc)) == -1) {
            efree(tmp_buf);
            return SOCKET_WRITE_ERROR;
        }
        if (wc > 0) {
            twc += wc;
            if (twc >= buf_len) {
                efree(tmp_buf);
                return 1;
            }
        }
    }
}

/**
 * check is socket readable.
 * -3 socket select error.
 * 1 socket is readable.
 * 0 socket is not readable.
 */
int socket_readable(const int sockfd, long select_timeout) {
    if (IS_INVALID_SOCKET(sockfd))
        return SOCKET_INVALID;

    fd_set fdset;
    FD_ZERO(&fdset);
    FD_SET(sockfd, &fdset);
    int nr = 0;

    if (select_timeout < 0) {
        if ((nr = select(sockfd + 1, &fdset, NULL, &fdset, NULL)) == -1) {
            return SOCKET_SELECT_ERROR;
        }
        return nr;
    } else {
        struct timeval tv;
        tv.tv_sec = 0;
        tv.tv_usec = select_timeout;
        if ((nr = select(sockfd + 1, &fdset, NULL, &fdset, &tv)) == -1) {
            return SOCKET_SELECT_ERROR;
        }
        return nr;
    }
}

/**
 * wrapper around read() so that it only reads to a \n.
 * return
 * -1 socketfd is invalid!
 * -2 read buffer emalloc failed!
 * -4 socket read error!
 * -5 max read data length reached!
 * 0 no data was read.
 * >0 length of data read from socket.
 */
int socket_readline(const int sockfd, char **buffer, long read_buf_size,
        int max_len, long select_timeout) {
    int nr = 0;
    if ((nr = socket_readable(sockfd, select_timeout)) != 1) {
        return nr;
    }

    if (read_buf_size < 0)
        read_buf_size = DEFAULT_READ_BUFFER_SIZE;
    long max_size = read_buf_size + 1;

    char *tmp_buf = NULL;
    if ((tmp_buf = (char *) emalloc((max_size) * sizeof(char))) == NULL) {
        return ALLOCATE_FAILED;
    }
    int rc = 0, tc = 0;
    char tmp_ch;
    int check_len = (max_len > 0) ? 1 : 0;
    if (max_len > 0 && read_buf_size > max_len)
        max_len = read_buf_size;

    for (;;) {
        if (rc == 1 && tc == 0)
            return 0;
        rc = read(sockfd, &tmp_ch, 1);
        if (rc == -1) { // error
            efree(tmp_buf);
            return SOCKET_READ_ERROR;
        } else if (rc == 1) {
            if (tc == 0 && tmp_ch == '\r')
                continue;
            if (tmp_ch == '\n') { // end of line
                tmp_buf[tc] = '\0';
                *buffer = tmp_buf;
                tmp_buf = NULL;
                break;
            }
            tmp_buf[tc++] = tmp_ch;
            if (max_size <= tc) {
                if (check_len) {
                    if (max_size >= (max_len - 1)) {
                        efree(tmp_buf);
                        return SOCKET_BEYONE_LIMIT;
                    }
                    // save memory, but more calculates
                    max_size = (max_size % 2 == 0) ? max_size / 2 * 3 : (max_size + 1) / 2 * 3;
                    // lese calculates, but more memory
                    // max_size = max_size * 2 - 1;

                    max_size =
                            max_size > (max_len - 1) ? max_len + 1 : max_size;
                    char *old_buf = tmp_buf;
                    if ((tmp_buf = (char *) erealloc(tmp_buf,
                            max_size * sizeof(char))) == NULL) {
                        efree(old_buf);
                        return ALLOCATE_FAILED;
                    }
                } else {
                    // save memory, but more calculates
                    max_size = (max_size % 2 == 0) ? max_size / 2 * 3 : (max_size + 1) / 2 * 3;
                    // lese calculates, but more memory
                    // max_size = max_size * 2 - 1;

                    char *old_buf = tmp_buf;
                    if ((tmp_buf = (char *) erealloc(tmp_buf, max_size * sizeof(char))) == NULL) {
                        efree(old_buf);
                        return ALLOCATE_FAILED;
                    }
                }
            }
        } else { // no data
            // php_printf("waite...\n");
            if (tc == 0) break;
        }
    }
    return tc;
}
