/**
 * check string start with
 */
int start_with(char *str, char *expect) {
    return (strcspn(str, expect) == 0);
}

/**
 * connect to remote server
 * return
 *-2 memory allocate failed.
 * 0 nothing to do with line.
 * >0 result count.
 */
int split(char *line, char ***result, int limit, char *delim) {
    if (line == NULL)
        return 0;
    int len = strlen(line);
    if (len == 0)
        return 0;
    if (limit < 0)
        return 0;

    char *tmp = (char *) emalloc((len + 1) * sizeof(char));
    if (tmp == NULL)
        return ALLOCATE_FAILED;

    strcpy(tmp, line);

    char *token;
    char **rs_tmp = (*result = (char **) emalloc(limit * sizeof(char *)));
    if (rs_tmp == NULL)
        return ALLOCATE_FAILED;
    int rc = 0, i = 0;
    while (token = strsep(&tmp, delim)) {
        if (token[0] != '\0') {
            rs_tmp[i++] = token;
            rc++;
            if (tmp == NULL || tmp[0] == '\0')
                break;
            if ((i + 1) == limit) {
                rs_tmp[i++] = tmp;
                rc++;
                // php_printf("RS:%s\n", result[i - 1]);
                break;
            }
        }
        if (tmp == NULL || tmp[0] == '\0')
            break;
    }
    return rc;
}

/**
 * fech line to appname
 */
int trim_appname(char *line, char **appname) {
    if (start_with(line, LOCAL_MSGSRV_APPNAME)) {
        *appname = line;
        return 1;
    } else {
        char **result = NULL;
        int count = 0;
        //php_printf("T1:%s\n", line);
        if ((count = split(line, &result, 2, ".")) < 1) {
            efree(result);
            return count;
        }
        //php_printf("T2:%s\n", result[0]);
        *appname = result[0];
        efree(result);
        return 1;
    }
}

/**
 * split for msgsrv
 * return
 * -2 memory allocate failed.
 * 0 nothing to do with line.
 * >0 result count.
 */
int msgsrv_split(char *line, php_msgsrv_msg **message) {
    int tc = 0, os = 0, oe = 0, ss = 0, rs = 0, len = strlen(line);
    char **tmp_rs = NULL;

    if ((rs = split(line, &tmp_rs, MSGSRV_MSG_LIMIT, MSGSRV_DELIMITER)) < 1) {
        efree(tmp_rs);
        return rs;
    }
    php_msgsrv_msg *tmp = (*message = (php_msgsrv_msg *) emalloc(
            sizeof(php_msgsrv_msg)));
    if (tmp == NULL) {
        efree(tmp_rs);
        return ALLOCATE_FAILED;
    }
    tmp->appname = tmp_rs[0];
    if (rs == 2) {
        tmp->command = tmp_rs[1];
    } else {
        tmp->command = tmp_rs[1];
        tmp->content = tmp_rs[2];
    }
    efree(tmp_rs);
    return rs;
}

/**
 * readline for msgsrv
 * return
 * -2 memory allocate failed.
 * >0 result count.
 */
int msgsrv_readline(const int sockfd, char **buf, int read_buffer_size,
        long select_timeout, long timeout) {

    char *tmp_buf = NULL;
    int tmp_buf_len = 0, i = 0, tc = 0;

    struct timeval now, start;
    if (timeout > 0) {
        tc = 1;
        gettimeofday(&start, NULL);
    }

    for (;;) {
        char *tmp = NULL;
        int rs = socket_readline(sockfd, &tmp, read_buffer_size, -1,
                select_timeout);
        if (rs < 0) { // error
            if (tmp != NULL)
                efree(tmp);
            if (tmp_buf != NULL)
                efree(tmp_buf);
            return rs;
        } else if (rs == 0) { // no data
            if (tc) {
                gettimeofday(&now, NULL);
                long timeuse = 1000 * (now.tv_sec - start.tv_sec) + (now.tv_usec - start.tv_usec) / 1000;
                if (timeuse >= timeout) {
                    if (tmp != NULL)
                        efree(tmp);
                    if (tmp_buf != NULL)
                        efree(tmp_buf);
                    return SOCKET_READ_TIMEOUT;
                }
            }
            continue;
        } else {
            if (tmp_buf == NULL) {
                tmp_buf_len = strlen(tmp);
                tmp_buf = (char *) emalloc((tmp_buf_len + 1) * sizeof(char));
                if (tmp_buf == NULL) {
                    efree(tmp);
                    return ALLOCATE_FAILED;
                }
                strcpy(tmp_buf, tmp);
                if (i > 2 && tmp_buf[i - 1] == '\\' && tmp_buf[i - 2] != '\\') {
                    tmp_buf[i - 1] = '\n';
                } else {
                    efree(tmp);
                    *buf = tmp_buf;
                    return tmp_buf_len;
                }
            } else {
                i = tmp_buf_len - 1;
                tmp_buf_len += strlen(tmp);
                char *old_tmp = tmp_buf;
                tmp_buf = (char *) realloc(tmp_buf,
                        (tmp_buf_len + 1) * sizeof(char));
                if (tmp_buf == NULL) {
                    efree(old_tmp);
                    efree(tmp);
                    return ALLOCATE_FAILED;
                }
                strcat(tmp_buf, tmp);

                if (tmp_buf_len > 2 && tmp_buf[tmp_buf_len - 1] == '\\'
                        && tmp_buf[tmp_buf_len - 2] != '\\') {
                    tmp_buf[i - 1] = '\n';
                } else {
                    efree(tmp);
                    *buf = tmp_buf;
                    return tmp_buf_len;
                }
            }
        }
    }
}

/**
 * writeline for msgsrv
 * return
 * 1 write successfuly
 */
int msgsrv_writeline(const int sockfd, char *data, long select_timeout) {
    if (data == NULL)
        return SOCKET_WRITE_NULL;
    return socket_writeline(sockfd, data, strlen(data), select_timeout);
}
