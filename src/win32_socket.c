#include <winsock2.h>
#include <windows.h>
#define IS_INVALID_SOCKET(a) (a == INVALID_SOCKET)

int socket_open(const char * host, long port) {

    int sockfd = -1;

    /*协商套接字版本*/
    WORD wVersionRequested;
    WSADATA wsaData;
    SOCKADDR_IN server;
    int err;
    wVersionRequested = MAKEWORD(2, 2);
    err = WSAStartup(wVersionRequested, &wsaData);
    if (err != 0) {
        return err;
    }
    if (LOBYTE(wsaData.wVersion) != 2 || HIBYTE(wsaData.wVersion) != 2) {
        WSACleanup();
        return err;
    }
    if ((sockfd = socket(AF_INET, SOCK_STREAM, 0)) == -1) {
        return -1;
    }

    memset(&server, 0, sizeof(server));
    server.sin_family = AF_INET;
    server.sin_port = htons(port);
    server.sin_addr.s_addr = inet_addr(host);

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
    fd_set fdset;
    int nw = 0;

    if (IS_INVALID_SOCKET(sockfd)) {
        return -1;
    }
    FD_ZERO(&fdset);
    FD_SET(sockfd, &fdset);

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
    char *tmp_buf = NULL;
    if ((nw = socket_writable(sockfd, select_timeout)) != 1) {
        return nw;
    }
    tmp_buf = (char *) malloc((buf_len + 2) * sizeof(char));
    if (tmp_buf == NULL) {
        return ALLOCATE_FAILED;

    }
    strcpy(tmp_buf, buf);
    tmp_buf[buf_len] = '\n';
    tmp_buf[buf_len + 1] = '\0';
    wl = strlen(tmp_buf);

    buf_len += 1;

    for (;;) {
        if ((wc = send(sockfd, tmp_buf + wc, wl - wc, 0)) == -1) {
            return SOCKET_WRITE_ERROR;
        }
        if (wc > 0) {
            twc += wc;
            if (twc >= buf_len) {
                free(tmp_buf);
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
    fd_set fdset;
    int nr = 0;
    if (IS_INVALID_SOCKET(sockfd)) {
        return SOCKET_INVALID;
    }
    FD_ZERO(&fdset);
    FD_SET(sockfd, &fdset);

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
 * -2 read buffer malloc failed!
 * -4 socket read error!
 * -5 max read data length reached!
 * 0 no data was read.
 * >0 length of data read from socket.
 */
int socket_readline(const int sockfd, char **buffer, long read_buf_size,
        int max_len, long select_timeout) {
    int rc = 0, tc = 0, check_len = -1, nr = 0;
    long max_size = -1;
    char tmp_ch = NULL;
    char *tmp_buf = NULL, *old_buf = NULL;
    if ((nr = socket_readable(sockfd, select_timeout)) != 1) {
        return nr;
    }

    if (read_buf_size <= 0) {
        read_buf_size = DEFAULT_READ_BUFFER_SIZE;
    }

    max_size = read_buf_size + 1;

    if ((tmp_buf = (char *) malloc(max_size * sizeof(char))) == NULL) {
        return ALLOCATE_FAILED;
    }
    check_len = (max_len > 0) ? 1 : 0;

    if (max_len > 0 && read_buf_size > max_len) {
        max_len = read_buf_size;
    }

    for (;;) {
        rc = recv(sockfd, &tmp_ch, 1, 0);
        if (rc == -1) { // error
            free(tmp_buf);
            return SOCKET_READ_ERROR;
        } else if (rc == 1) {
            if (tmp_ch == '\n') { // end of line
                tmp_buf[tc] = '\0';
                *buffer = tmp_buf;
                tmp_buf = NULL;
                break;
            }
            tmp_buf[tc++] = tmp_ch;
            //printf("%s\n", tmp_buf);
            if (max_size <= tc) {
                if (check_len) {
                    if (max_size >= (max_len - 1)) {
                        free(tmp_buf);
                        return -4;
                    }
                    // save memory, but more calculates
                    /*并非严格的增长1.5呗*/
                    max_size =
                            (max_size % 2 == 0) ?
                                    max_size / 2 * 3 : (max_size + 1) / 2 * 3;
                    // lese calculates, but more memory
                    // max_size = max_size * 2 - 1;

                    max_size =
                            max_size > (max_len - 1) ? max_len + 1 : max_size;
                    old_buf = tmp_buf;
                    if ((tmp_buf = (char *) realloc(tmp_buf,
                            max_size * sizeof(char))) == NULL) {
                        free(old_buf);
                        return ALLOCATE_FAILED;
                    }
                } else {
                    // save memory, but more calculates
                    max_size =
                            (max_size % 2 == 0) ?
                                    max_size / 2 * 3 : (max_size + 1) / 2 * 3;
                    // lese calculates, but more memory
                    // max_size = max_size * 2 - 1;

                    old_buf = tmp_buf;
                    if ((tmp_buf = (char *) realloc(tmp_buf,
                            max_size * sizeof(char))) == NULL) {
                        free(old_buf);
                        return ALLOCATE_FAILED;
                    }
                }
            }
        } else { // no data
            if (tc == 0)
                break;
        }
    }
    return tc;
}
