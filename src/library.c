/*
   +----------------------------------------------------------------------+
   | PHP MsgSrv Extension v1.2                                            |
   +----------------------------------------------------------------------+
   | Copyright (c) 2015 ChinaRoad Co., Ltd.  All rights reserved.         |
   +----------------------------------------------------------------------+
   | Author: Deng Tao <dengt@660pp.com>                                   |
   +----------------------------------------------------------------------+
*/

#include "php.h"

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "syslog.h"
#include "php_network.h"
#include <sys/types.h>
#include <ext/standard/php_rand.h>

#ifndef _MSC_VER
#include <netinet/tcp.h>  /* TCP_NODELAY */
#include <sys/socket.h>
#endif

#include "common.h"
#include "library.h"
#include "md5.h"

#ifdef PHP_WIN32
# if PHP_MAJOR_VERSION == 5 && PHP_MINOR_VERSION <= 4
/* This proto is available from 5.5 on only */
PHPAPI int usleep(unsigned int useconds);
# endif
#endif

#ifdef _MSC_VER
#define atoll _atoi64
#define random rand
#define usleep Sleep
#endif

long rand_range(long min, long max TSRMLS_DC) {
    return php_rand(TSRMLS_CC) % (max - min + 1) + min;;
}

char *md5_str(char *plain) {
    md5_state_t state;
    md5_byte_t digest[16];
    static char hex_output[33];
    int di;

    md5_init(&state);
    md5_append(&state, (const md5_byte_t *) plain, strlen(plain));
    md5_finish(&state, digest);

    for (di = 0; di < 16; ++di) {
        sprintf(hex_output + di * 2, "%02x", digest[di]);
    }
    return hex_output;
}

int str_split(char *str, char *delim, const int delim_len, char*** ret, unsigned int limit) {
    char *p1, *p2, *end;
    int  str_len = strlen(str);
    int  count = 0;
    char **resp;

    resp = (char **) emalloc(limit * sizeof(char *));
    if (resp == NULL) return -1;
    if (limit == 0 || limit == 1) {   // handle limit 0 or 1
        resp[0] = str;
        *ret = resp;
        return 1;
    }
    end = str + str_len;
    p2 = php_memnstr(str, delim, delim_len, end);
    
    if (p2 == NULL) {   // delim not found!     
        resp[0] = str;
        *ret = resp;
        return 1;
    }
    p1 = str;
    do {
        p2[0] = '\0';
        resp[count ++] = p1;
        p1 = p2 + delim_len;
    } while ((p2 = php_memnstr(p1, delim, delim_len, end)) != NULL && --limit > 1);

    if (p1 <= end) {
        resp[count ++] = p1;
    }
    *ret = resp;
    return count;
}

MsgSrvSocket *msgsrv_socket_create(
                        char *host, 
                        unsigned short port,
                        char *app,
                        char *user,
                        char *pass,
                        long timeout, 
                        long read_timeout,
                        int persistent, long max_pool_size TSRMLS_DC) {

    MsgSrvSocket *msgsrv_socket;
    char *persistent_id, *hash_key;

    msgsrv_socket         = (MsgSrvSocket *) pemalloc(sizeof(MsgSrvSocket), persistent);
    if (msgsrv_socket == NULL) return NULL;

    msgsrv_socket->host   = pestrdup(host, persistent);
    // app info
    msgsrv_socket->app    = pestrdup(app, persistent);
    msgsrv_socket->user   = pestrdup(user, persistent);
    msgsrv_socket->pass   = pestrdup(pass, persistent);
    // inject data~~
    msgsrv_socket->app  = pestrdup(app, persistent);
    msgsrv_socket->user = pestrdup(user, persistent);
    msgsrv_socket->pass = pestrdup(pass, persistent);
    msgsrv_socket->port   = port;
    msgsrv_socket->busy  = NO;
    msgsrv_socket->refcount  = 0;
    
    // host info
    if (persistent) {
        msgsrv_socket->pool_index = rand_range(1, max_pool_size TSRMLS_CC);
        msgsrv_socket->persistent_id_len = spprintf(&persistent_id, 0, "%s@tcp://%s:%d/%s#%ld", msgsrv_socket->user, msgsrv_socket->host, msgsrv_socket->port, msgsrv_socket->app, msgsrv_socket->pool_index);
        msgsrv_socket->persistent_id = pestrdup(persistent_id, persistent);
        msgsrv_socket->hash_key_len = spprintf(&hash_key, 0, "%s@msgsrv://%s:%d/%s#%ld", msgsrv_socket->user, msgsrv_socket->host, msgsrv_socket->port, msgsrv_socket->app, msgsrv_socket->pool_index);
        msgsrv_socket->hash_key = pestrdup(hash_key, persistent);
    } else {
        msgsrv_socket->pool_index = 0;
        msgsrv_socket->persistent_id = NULL;
        msgsrv_socket->persistent_id_len = -1;
        msgsrv_socket->hash_key = NULL;
        msgsrv_socket->hash_key_len = -1;
    }
    
    msgsrv_socket->idx    = php_rand(TSRMLS_CC);
    msgsrv_socket->timeout = timeout;
    msgsrv_socket->read_timeout = read_timeout;
    msgsrv_socket->persistent = persistent;

    memset(msgsrv_socket->in_buffer, 0, MSGSRV_BUFFER_SIZE);

    msgsrv_socket->stream = NULL;
    msgsrv_socket->status = MSGSRV_SOCKET_STATUS_DISCONNECTED;
    msgsrv_socket->watching = NO;
    msgsrv_socket->full_app = NULL;
    msgsrv_socket->recent_use_time = -1;

    return msgsrv_socket;
}

int msgsrv_socket_open(MsgSrvSocket *msgsrv_socket, int force_connect, int authorize, int trace_mode TSRMLS_DC) {
    int res = -1;

    switch (msgsrv_socket->status) {
        case MSGSRV_SOCKET_STATUS_DISCONNECTED:
            return msgsrv_socket_connect(msgsrv_socket, authorize, trace_mode TSRMLS_CC);
        case MSGSRV_SOCKET_STATUS_CONNECTED:
            res = 0;
        break;
        case MSGSRV_SOCKET_STATUS_UNKNOWN:
            if (force_connect == YES && msgsrv_socket_connect(msgsrv_socket, authorize, trace_mode TSRMLS_CC) < 0) {
                res = -1;
            } else {
                res = 0;
                msgsrv_socket->status = MSGSRV_SOCKET_STATUS_CONNECTED;
            }
        break;
    }

    return res;
}

int msgsrv_socket_connect(MsgSrvSocket *msgsrv_socket, int authorize, int trace_mode TSRMLS_DC) {
    struct timeval tv, read_tv, *tv_ptr = NULL;
    char *host = NULL, *errstr = NULL;
    int host_len, err = 0;
    php_netstream_data_t *sock;
    int tcp_flag = 1;
    int keep_alive = 1; // enable keep alive
    int keep_idle = 60;
    int keep_intvl = 5;
    int keep_count = 3;

    char *playload = NULL, *_from = NULL, *_cmd = NULL, *_body = NULL, *reply;
    char *random = NULL, *plain = NULL;
    int ret = -1;
    char **_params = NULL;
    int  full_app_len = 0;

    /* do close current connection */
    if (msgsrv_socket->stream != NULL) {
        msgsrv_socket_disconnect(msgsrv_socket, trace_mode TSRMLS_CC);
    }
    tv.tv_sec  = (time_t)msgsrv_socket->timeout;
    tv.tv_usec = (int)((msgsrv_socket->timeout - tv.tv_sec) * 1000000);
    if(tv.tv_sec != 0 || tv.tv_usec != 0) {
        tv_ptr = &tv;
    }
    read_tv.tv_sec  = (time_t)msgsrv_socket->read_timeout;
    read_tv.tv_usec = (int)((msgsrv_socket->read_timeout - read_tv.tv_sec) * 1000000);
    host_len = spprintf(&host, 0, "tcp://%s:%d", msgsrv_socket->host, msgsrv_socket->port);
    
    if (trace_mode) {
        syslog(LOG_INFO, _LOGGER_OPEN "[%ld] - Connecting to %s...", msgsrv_socket->idx, host);
    }
    #ifdef MSGSRV_DEBUG_SOCKET
    php_printf(_LOGGER_OPEN "[%ld] - Connecting to %s...\n", msgsrv_socket->idx, host);
    #endif

    /* do Socket Connect */
    msgsrv_socket->stream = php_stream_xport_create(host, host_len, ENFORCE_SAFE_MODE,
                                STREAM_XPORT_CLIENT | STREAM_XPORT_CONNECT,
                                msgsrv_socket->persistent_id, tv_ptr, NULL, &errstr, &err
                            );

    if (!msgsrv_socket->stream) {
        if (trace_mode) {
            syslog(LOG_ERR, _LOGGER_OPEN "[%ld] - Unable to connect %s(%s)!", msgsrv_socket->idx, host, errstr);
        }
        #ifdef MSGSRV_DEBUG_SOCKET
        php_printf(_LOGGER_OPEN "[%ld] - Unable to connect %s(%s)!\n", msgsrv_socket->idx, host, errstr);
        #endif

        efree(host);
        efree(errstr);
        return -1;
    }
    efree(host);
    if (trace_mode) {
        syslog(LOG_INFO, _LOGGER_OPEN "[%ld] - Connection established!", msgsrv_socket->idx);
    }
    #ifdef MSGSRV_DEBUG_SOCKET
    php_printf(_LOGGER_OPEN "[%ld] - Connection established!\n", msgsrv_socket->idx);
    #endif

    /* set TCP_NODELAY */
    sock = (php_netstream_data_t*)msgsrv_socket->stream->abstract;
    setsockopt(sock->socket, IPPROTO_TCP, TCP_NODELAY, (char *) &tcp_flag, sizeof(int));
    setsockopt(sock->socket, SOL_SOCKET, SO_KEEPALIVE, (void *) &keep_alive, sizeof(int));
    setsockopt(sock->socket, SOL_TCP, TCP_KEEPIDLE, (void *) &keep_idle, sizeof(int));
    setsockopt(sock->socket, SOL_TCP, TCP_KEEPINTVL, (void *) &keep_intvl, sizeof(int));
    setsockopt(sock->socket, SOL_TCP, TCP_KEEPCNT, (void *) &keep_count, sizeof(int));

    php_stream_auto_cleanup(msgsrv_socket->stream);

    if(tv.tv_sec != 0 || tv.tv_usec != 0) {
        php_stream_set_option(msgsrv_socket->stream, PHP_STREAM_OPTION_READ_TIMEOUT,
                              0, &read_tv);
    }
    php_stream_set_option(msgsrv_socket->stream,
                          PHP_STREAM_OPTION_WRITE_BUFFER,
                          PHP_STREAM_BUFFER_NONE, NULL);

    msgsrv_socket->status = MSGSRV_SOCKET_STATUS_CONNECTED;

    if (authorize == NO) return 0;

    /* login process */
    if (trace_mode) {
        syslog(LOG_INFO, _LOGGER_AUTH "[%ld] - Authorize as %s@%s ...", msgsrv_socket->idx, msgsrv_socket->user, msgsrv_socket->app);
    }
    #ifdef MSGSRV_DEBUG_SOCKET
    php_printf(_LOGGER_AUTH "[%ld] - Authorize as %s@%s ...\n", msgsrv_socket->idx, msgsrv_socket->user, msgsrv_socket->app);
    #endif
    while (1) {
        // read a playload from stream
        ret = msgsrv_socket_read(msgsrv_socket, &playload, msgsrv_socket->timeout, trace_mode TSRMLS_CC);
        switch (ret) {
            case 0: // link closed!
                if (trace_mode) {
                    syslog(LOG_ERR, _LOGGER_AUTH "[%ld] - Authorize failed(connection closed)!", msgsrv_socket->idx);
                }
                #ifdef MSGSRV_DEBUG_SOCKET
                php_printf(_LOGGER_AUTH "[%ld] - Authorize failed(connection closed)!\n", msgsrv_socket->idx);
                #endif

                return -2;

            case -1:
            case -9:    // timeout
            case -2:    // read error close link!
                if (trace_mode) {
                    syslog(LOG_ERR, _LOGGER_AUTH "[%ld] - Authorize failed(read error)!", msgsrv_socket->idx);
                }
                #ifdef MSGSRV_DEBUG_SOCKET
                php_printf(_LOGGER_AUTH "[%ld] - Authorize failed(read error)!\n", msgsrv_socket->idx);
                #endif

                msgsrv_stream_close(msgsrv_socket TSRMLS_CC);
                msgsrv_socket->stream = NULL;
                msgsrv_socket->status = MSGSRV_SOCKET_STATUS_FAILED;
                msgsrv_socket->watching = NO;
                return -2;

            default:
                if (ret < 1) {  // unkown error!
                    if (trace_mode) {
                        syslog(LOG_ERR, _LOGGER_AUTH "[%ld] - Authorize failed(read unkown error:%d)!", msgsrv_socket->idx, ret);
                    }
                    #ifdef MSGSRV_DEBUG_SOCKET
                    php_printf(_LOGGER_AUTH "[%ld] - Authorize failed(read unkown error:%d)!\n", msgsrv_socket->idx, ret);
                    #endif

                    msgsrv_stream_close(msgsrv_socket TSRMLS_CC);
                    msgsrv_socket->stream = NULL;
                    msgsrv_socket->status = MSGSRV_SOCKET_STATUS_FAILED;
                    msgsrv_socket->watching = NO;
                    return -2;
                }
                break;
        }
        // parser playload
        ret = msgsrv_playload_parser(playload, &_from, &_cmd, &_body);
        switch(ret) {
            case MSGSRV_PLAYLOAD_MINI:
            case MSGSRV_PLAYLOAD_DEFAULT:
                break;
            case MSGSRV_PLAYLOAD_ERROR:   // error playload
            default:    // unkown error
                if (trace_mode) {
                    syslog(LOG_ERR, _LOGGER_AUTH "[%ld] - Authorize failed(error playload read:%d)!", msgsrv_socket->idx, ret);
                }                
                #ifdef MSGSRV_DEBUG_SOCKET
                php_printf(_LOGGER_AUTH "[%ld] - Authorize failed(error playload read:%d)!\n", msgsrv_socket->idx, ret);
                #endif

                efree(playload);
                msgsrv_stream_close(msgsrv_socket TSRMLS_CC);
                msgsrv_socket->stream = NULL;
                msgsrv_socket->status = MSGSRV_SOCKET_STATUS_FAILED;
                msgsrv_socket->watching = NO;
                return -2;
        }
        // detect playload app
        if (strcmp(LOCAL_MSGSRV_APPNAME, _from) != 0) {  // error app
            if (trace_mode) {
                syslog(LOG_ERR, _LOGGER_AUTH "[%ld] - Authorize failed(illegale app:%s)!", msgsrv_socket->idx, _from);
            }
            #ifdef MSGSRV_DEBUG_SOCKET
            php_printf(_LOGGER_AUTH "[%ld] - Authorize failed(illegale app:%s)!\n", msgsrv_socket->idx, _from);
            #endif

            efree(playload);
            msgsrv_stream_close(msgsrv_socket TSRMLS_CC);
            msgsrv_socket->stream = NULL;
            msgsrv_socket->status = MSGSRV_SOCKET_STATUS_FAILED;
            msgsrv_socket->watching = NO;
            return -2;
        }
        // dispatch playload command
        if (strcmp(MSGSRV_CMD_MSGSRV, _cmd) == 0) {   // . MSGSRV
            efree(playload);
            msgsrv_playload_builder(&reply, LOCAL_MSGSRV_APPNAME, MSGSRV_CMD_APPNAME, msgsrv_socket->app);
            if (msgsrv_socket_write(msgsrv_socket, reply, trace_mode TSRMLS_CC) != 0) {
                if (trace_mode) {
                    syslog(LOG_ERR, _LOGGER_AUTH "[%ld] - Authorize failed(send app name error)!", msgsrv_socket->idx);
                }
                #ifdef MSGSRV_DEBUG_SOCKET
                php_printf(_LOGGER_AUTH "[%ld] - Authorize failed(send app name error)!\n", msgsrv_socket->idx);
                #endif

                efree(reply);
                msgsrv_stream_close(msgsrv_socket TSRMLS_CC);
                msgsrv_socket->stream = NULL;
                msgsrv_socket->status = MSGSRV_SOCKET_STATUS_FAILED;
                msgsrv_socket->watching = NO;
                return -2;
            }
            efree(reply);
        } else if (strcmp(MSGSRV_CMD_AUTH, _cmd) == 0) { // . AUTH
            random = _body;
            spprintf(&plain, 0, "%s%s", msgsrv_socket->pass, random);
            efree(playload);
            char *key = md5_str(plain);
            efree(plain);

            spprintf(&_body, 0, "%s" _DELIM "%s", msgsrv_socket->user, key);

            msgsrv_playload_builder(&reply, LOCAL_MSGSRV_APPNAME, MSGSRV_CMD_USER, _body);
            efree(_body);
            if (msgsrv_socket_write(msgsrv_socket, reply, trace_mode TSRMLS_CC) != 0) {
                if (trace_mode) {
                    syslog(LOG_ERR, _LOGGER_AUTH "[%ld] - Authorize failed(send user identity error)!", msgsrv_socket->idx);
                }
                #ifdef MSGSRV_DEBUG_SOCKET
                php_printf(_LOGGER_AUTH "[%ld] - Authorize failed(send user identity error)!\n", msgsrv_socket->idx);
                #endif

                efree(reply);
                msgsrv_stream_close(msgsrv_socket TSRMLS_CC);
                msgsrv_socket->stream = NULL;
                msgsrv_socket->status = MSGSRV_SOCKET_STATUS_FAILED;
                msgsrv_socket->watching = NO;
                return -2;
            }
            efree(reply);
        } else if (strcmp(MSGSRV_CMD_ACCESS, _cmd) == 0) {
            if (str_split(_body, _DELIM, _DELIM_LENGTH, &_params, 2) < 1) {
                if (trace_mode) {
                    syslog(LOG_ERR, _LOGGER_AUTH "[%ld] - Authorize failed(access result invalid)!", msgsrv_socket->idx);
                }
                #ifdef MSGSRV_DEBUG_SOCKET
                php_printf(_LOGGER_AUTH "[%ld] - Authorize failed(access result invalid)!\n", msgsrv_socket->idx);
                #endif

                efree(playload);
                msgsrv_stream_close(msgsrv_socket TSRMLS_CC);
                msgsrv_socket->stream = NULL;
                msgsrv_socket->status = MSGSRV_SOCKET_STATUS_FAILED;
                msgsrv_socket->watching = NO;
                return -2;
            }

            if (strcmp(MSGSRV_ACCESS_GRANTED, _params[0]) == 0) {   // Login Success
                if (trace_mode) {
                    syslog(LOG_INFO, _LOGGER_AUTH "[%ld] - Authorize Granted(%s)!", msgsrv_socket->idx, _params[1]);
                }
                #ifdef MSGSRV_DEBUG_SOCKET
                php_printf(_LOGGER_AUTH "[%ld] - Authorize Granted(%s)!\n", msgsrv_socket->idx, _params[1]);
                #endif

                msgsrv_socket->status = MSGSRV_SOCKET_STATUS_GRANTED;
                msgsrv_socket->watching = YES;
                if (msgsrv_socket->persistent) {
                    full_app_len = strlen(_params[1]);
                    msgsrv_socket->full_app = pemalloc(full_app_len + 1, msgsrv_socket->persistent);
                    if (msgsrv_socket->full_app == NULL) {
                        if (trace_mode) {
                            syslog(LOG_INFO, _LOGGER_AUTH "[%ld] - Authorize Granted(%s), But memory alloc failed! disconnect...", msgsrv_socket->idx, _params[1]);
                        }
                        #ifdef MSGSRV_DEBUG_SOCKET
                        php_printf(_LOGGER_AUTH "[%ld] - Authorize Granted(%s)!, But memory alloc failed! disconnect...\n", msgsrv_socket->idx, _params[1]);
                        #endif

                        efree(playload);
                        msgsrv_stream_close(msgsrv_socket TSRMLS_CC);
                        msgsrv_socket->stream = NULL;
                        msgsrv_socket->status = MSGSRV_SOCKET_STATUS_FAILED;
                        msgsrv_socket->watching = NO;
                    }
                    memcpy(msgsrv_socket->full_app, _params[1], full_app_len + 1);
                } else {
                    msgsrv_socket->full_app = estrndup(_params[1], strlen(_params[1])); // Set FullAppName
                }               

                efree(playload);
                return 0;
            } else {    // Login Failed!
                if (trace_mode) {
                    syslog(LOG_ERR, _LOGGER_AUTH "[%ld] - Authorize failed(access deny)!", msgsrv_socket->idx);
                }
                #ifdef MSGSRV_DEBUG_SOCKET
                php_printf(_LOGGER_AUTH "[%ld] - Authorize failed(access deny)!\n", msgsrv_socket->idx);
                #endif

                efree(playload);
                msgsrv_stream_close(msgsrv_socket TSRMLS_CC);
                msgsrv_socket->stream = NULL;
                msgsrv_socket->status = MSGSRV_SOCKET_STATUS_DENY;
                msgsrv_socket->watching = NO;
                return -2;
            }
        } else if (strcmp(MSGSRV_CMD_ERROR, _cmd) == 0) {   // . ERROR
            if (trace_mode) {
                syslog(LOG_ERR, _LOGGER_AUTH "[%ld] - Authorize failed(error occur:%s)!", msgsrv_socket->idx, _body);
            }
            #ifdef MSGSRV_DEBUG_SOCKET
            php_printf(_LOGGER_AUTH "[%ld] - Authorize failed(error occur:%s)!\n", msgsrv_socket->idx, _body);
            #endif

            efree(playload);
            msgsrv_stream_close(msgsrv_socket TSRMLS_CC);
            msgsrv_socket->stream = NULL;
            msgsrv_socket->status = MSGSRV_SOCKET_STATUS_FAILED;
            msgsrv_socket->watching = NO;
            return -2;
        }
    }
}

int msgsrv_check_eof(MsgSrvSocket *msgsrv_socket, int trace_mode TSRMLS_DC) {

    if (!msgsrv_socket->stream) {
        return -1;
    }

    if (php_stream_eof(msgsrv_socket->stream)) {
        msgsrv_stream_close(msgsrv_socket TSRMLS_CC);
        msgsrv_socket->stream = NULL;
        msgsrv_socket->status = MSGSRV_SOCKET_STATUS_FAILED;
        msgsrv_socket->watching = NO;
        return TRUE;
    } else {
        return FALSE;
    }
}

int msgsrv_socket_read(MsgSrvSocket *msgsrv_socket, char **playload, long timeout, int trace_mode TSRMLS_DC) {
    struct timeval now, start;
    size_t  in_buffer_len = 0;
    int  in_buffer_offset = 0;
    int  keep_read = TRUE;
    long consuming = 0;

    char *resp = NULL;
    int  resp_index = 0;    // copy data from this index.
    int  resp_len = MSGSRV_BUFFER_SIZE;

    if(msgsrv_socket && msgsrv_socket->status == MSGSRV_SOCKET_STATUS_DISCONNECTED) {
        return 0;
    }

    *playload = NULL;
    resp = (char *) emalloc(MSGSRV_BUFFER_SIZE * sizeof(char));
    if (resp == NULL) {
        if (trace_mode) {
            syslog(LOG_ERR, _LOGGER_READ "[%ld] - Read Error(emalloc buffer[%d] failed)!", msgsrv_socket->idx, MSGSRV_BUFFER_SIZE);
        }
        #ifdef MSGSRV_DEBUG_SOCKET
        php_printf(_LOGGER_READ "[%ld] - Read Error(emalloc buffer[%d] failed)!\n", msgsrv_socket->idx, MSGSRV_BUFFER_SIZE);
        #endif  
        return -2;
    }

    if (timeout > 0) {
        gettimeofday(&start, NULL);
    }

    while (keep_read) { 
        memset(msgsrv_socket->in_buffer, 0, MSGSRV_BUFFER_SIZE);
        if (timeout > 0) {
            // keep read until timeout or max buffer size or reach the end of line '\n'.
            gettimeofday(&now, NULL);
            if ((now.tv_sec - start.tv_sec) >= timeout) {
                if (trace_mode) {
                    syslog(LOG_ERR, _LOGGER_READ "[%ld] - Read Timeout(Start: %lds, Now: %lds, Timeout: %lds)!", msgsrv_socket->idx, start.tv_sec, now.tv_sec, timeout);
                }
                #ifdef MSGSRV_DEBUG_SOCKET
                php_printf(_LOGGER_READ "[%ld] - Read Timeout(Start: %lds, Now: %lds, Timeout: %lds)!\n", msgsrv_socket->idx, start.tv_sec, now.tv_sec, timeout);
                #endif

                if (resp_index > 0) {
                    msgsrv_socket->status = MSGSRV_SOCKET_STATUS_FINISH;

                    if (trace_mode) {
                        syslog(LOG_ERR, _LOGGER_READ "[%ld] - Read Timeout(Start: %lds, Now: %lds, Timeout: %ldsm, Read: %d), Finish Connection!", msgsrv_socket->idx, start.tv_sec, now.tv_sec, timeout, resp_index);
                    }
                    #ifdef MSGSRV_DEBUG_SOCKET
                    php_printf(_LOGGER_READ "[%ld] - Read Timeout(Start: %lds, Now: %lds, Timeout: %lds, Read: %d), Finish Connection!\n", msgsrv_socket->idx, start.tv_sec, now.tv_sec, timeout, resp_index);
                    #endif
                }
                return -9;
            }
        }
        /**
         * reads up to count-1 bytes of data from stream and copies them into the buffer buf. 
         * Reading stops after an EOF or a newline. 
         * If a newline is read, it is stored in buf as part of the returned data. 
         * A NUL terminating character is stored as the last character in the buffer.
         *
         * MARK BY D.T.:
         * if MSGSRV_BUFFER_SIZE reached, in_buffer[MSGSRV_BUFFER_SIZE - 1] will be set to '\0'.
         */
        if (php_stream_get_line(msgsrv_socket->stream, msgsrv_socket->in_buffer, MSGSRV_BUFFER_SIZE, &in_buffer_len) == NULL) {
            // Read Nothing...
            if (msgsrv_check_eof(msgsrv_socket, trace_mode TSRMLS_CC) == TRUE) {
                if (trace_mode) {
                    syslog(LOG_ERR, _LOGGER_READ "[%ld] - Read Error(current link eof)!", msgsrv_socket->idx);
                }
                #ifdef MSGSRV_DEBUG_SOCKET
                php_printf(_LOGGER_READ "[%ld] - Read Error(current link eof)!\n", msgsrv_socket->idx);
                #endif
                return -1;
            }
            continue;
        }
        // fix msgsrv \n\r
        if (resp_index == 0 && msgsrv_socket->in_buffer[0] == _NL_FIX) {
            in_buffer_offset = 1;
        } else {
            in_buffer_offset = 0;
        }
        switch (msgsrv_socket->in_buffer[in_buffer_len - 1]) {                
            case '\n': // reach the end of line '\n'.
                msgsrv_socket->in_buffer[in_buffer_len - 1] = '\0';
                keep_read = FALSE;
                *playload = resp;

            default: // 
                if ((resp_len - resp_index) <= in_buffer_len) {
                    // reach current buffer size. we should realloc resp buffer by add a block:MSGSRV_BUFFER_SIZE.
                    resp = (char *) erealloc(resp, resp_len + MSGSRV_BUFFER_SIZE);
                    if (resp == NULL) {
                        if (trace_mode) {
                            syslog(LOG_ERR, _LOGGER_READ "[%ld] - Read Error(realloc buffer[%d] failed)!", msgsrv_socket->idx, resp_len + MSGSRV_BUFFER_SIZE);
                        }
                        #ifdef MSGSRV_DEBUG_SOCKET
                        php_printf(_LOGGER_READ "[%ld] - Read Error(realloc buffer[%d] failed)!\n", msgsrv_socket->idx, resp_len + MSGSRV_BUFFER_SIZE);
                        #endif
                        *playload = NULL;
                        return -2;
                    }
                    *playload = resp;
                    resp_len += MSGSRV_BUFFER_SIZE;
                }
                // append buffer we just read into our playload buffer.
                memcpy(resp + resp_index, msgsrv_socket->in_buffer + in_buffer_offset, in_buffer_len - in_buffer_offset);
                resp_index = resp_index + (in_buffer_len - in_buffer_offset);

                break;
        }
    }

    if (trace_mode) {
        syslog(LOG_DEBUG, _LOGGER_READ "[%ld] - [%s]", msgsrv_socket->idx, resp);
    }
    #ifdef MSGSRV_DEBUG_SOCKET
    php_printf(_LOGGER_READ "[%ld] - [%s]\n", msgsrv_socket->idx, resp);
    #endif
    
    return resp_index - 1;
}

int msgsrv_socket_write(MsgSrvSocket *msgsrv_socket, char *playload, int trace_mode TSRMLS_DC) {
    int len, wrote = 0;
    if(msgsrv_socket && msgsrv_socket->status == MSGSRV_SOCKET_STATUS_DISCONNECTED) {
        return -1;
    }
    if (playload == NULL) return -1;
    len = strlen(playload);
    if (len == 0) return 0;

    if (len > 0 && (wrote = php_stream_write(msgsrv_socket->stream, playload, len)) == len && php_stream_write(msgsrv_socket->stream, _NL, _NL_LENGTH)) {
        if (trace_mode) {
            syslog(LOG_DEBUG, _LOGGER_WRITE "[%ld] - [%s]", msgsrv_socket->idx, playload);
        }
        #ifdef MSGSRV_DEBUG_SOCKET
        php_printf(_LOGGER_WRITE "[%ld] - [%s]\n", msgsrv_socket->idx, playload);
        #endif

        return 0;
    } else {
        if (trace_mode) {
            if (wrote == len) {
                syslog(LOG_ERR, _LOGGER_ERROR "[%ld] - [%s][Total:%d, Wrote:%d]", msgsrv_socket->idx, playload, len, wrote);    
            } else {
                syslog(LOG_ERR, _LOGGER_ERROR "[%ld] - [%s][Total:%d, Wrote:%d, Packet Error!]", msgsrv_socket->idx, playload, len, wrote);    
            }
        }
        #ifdef MSGSRV_DEBUG_SOCKET
        if (wrote == len) {
            php_printf(_LOGGER_ERROR "[%ld] - [%s][Total:%d, Wrote:%d]\n", msgsrv_socket->idx, playload, len, wrote);
        } else {
            php_printf(_LOGGER_ERROR "[%ld] - [%s][Total:%d, Wrote:%d, Packet Error!]\n", msgsrv_socket->idx, playload, len, wrote);
        }
        #endif

        return -1;
    }
}

int msgsrv_socket_ping(MsgSrvSocket *msgsrv_socket, int trace_mode TSRMLS_DC) {
    if (msgsrv_socket == NULL) return FAILURE;
    if (msgsrv_socket->stream == NULL) return FAILURE;
    if (msgsrv_socket->status != MSGSRV_SOCKET_STATUS_GRANTED) return FAILURE;

    if (trace_mode) {
        syslog(LOG_INFO, _LOGGER_PING "[%ld] - Try to ping %s@msgsrv://%s:%d#%s...", msgsrv_socket->idx, msgsrv_socket->full_app, msgsrv_socket->host, msgsrv_socket->port, msgsrv_socket->app);
    }
    #ifdef MSGSRV_DEBUG_SOCKET
    php_printf(_LOGGER_PING "[%ld] - Try to ping %s@msgsrv://%s:%d#%s...\n", msgsrv_socket->idx, msgsrv_socket->full_app, msgsrv_socket->host, msgsrv_socket->port, msgsrv_socket->app);
    #endif

    if (msgsrv_check_eof(msgsrv_socket, trace_mode TSRMLS_CC) == TRUE) {
        if (trace_mode) {
            syslog(LOG_ERR, _LOGGER_PING "[%ld] - Ping %s@msgsrv://%s:%d#%s Failed!", msgsrv_socket->idx, msgsrv_socket->full_app, msgsrv_socket->host, msgsrv_socket->port, msgsrv_socket->app);
        }
        #ifdef MSGSRV_DEBUG_SOCKET
        php_printf(_LOGGER_PING "[%ld] - Ping %s@msgsrv://%s:%d#%s Failed!\n", msgsrv_socket->idx, msgsrv_socket->full_app, msgsrv_socket->host, msgsrv_socket->port, msgsrv_socket->app);
        #endif

        return FAILURE;
    } else {
        if (trace_mode) {
            syslog(LOG_INFO, _LOGGER_PING "[%ld] - Ping %s@msgsrv://%s:%d#%s Successfully!", msgsrv_socket->idx, msgsrv_socket->full_app, msgsrv_socket->host, msgsrv_socket->port, msgsrv_socket->app);
        }
        #ifdef MSGSRV_DEBUG_SOCKET
        php_printf(_LOGGER_PING "[%ld] - Ping %s@msgsrv://%s:%d#%s Successfully!\n", msgsrv_socket->idx, msgsrv_socket->full_app, msgsrv_socket->host, msgsrv_socket->port, msgsrv_socket->app);
        #endif

        return SUCCESS;
    }
}

int msgsrv_socket_disconnect(MsgSrvSocket *msgsrv_socket, int trace_mode TSRMLS_DC) {
    char *playload = NULL;

    if (msgsrv_socket == NULL) {
        return 1;
    }

    if (msgsrv_socket->stream != NULL) {
        if (trace_mode) {
            syslog(LOG_INFO, _LOGGER_CLOSE "[%ld] - Disconnect...", msgsrv_socket->idx);
        }
        #ifdef MSGSRV_DEBUG_SOCKET
        php_printf(_LOGGER_CLOSE "[%ld] - Disconnect...\n", msgsrv_socket->idx);
        #endif

        if (msgsrv_socket->status == MSGSRV_SOCKET_STATUS_GRANTED) {
            msgsrv_playload_builder(&playload, LOCAL_MSGSRV_APPNAME, MSGSRV_CMD_CLOSE, NULL);
            msgsrv_socket_write(msgsrv_socket, playload, trace_mode TSRMLS_CC);
            efree(playload);
        }
        msgsrv_socket->status = MSGSRV_SOCKET_STATUS_DISCONNECTED;
        msgsrv_socket->watching = NO;

        if(msgsrv_socket->stream) { /* still valid after the write? */
            msgsrv_stream_close(msgsrv_socket);
        }

        if (trace_mode) {
            syslog(LOG_INFO, _LOGGER_CLOSE "[%ld] - Disconnected!", msgsrv_socket->idx);
        }
        #ifdef MSGSRV_DEBUG_SOCKET
        php_printf(_LOGGER_CLOSE "[%ld] - Disconnected!\n", msgsrv_socket->idx);
        #endif

        msgsrv_socket->stream = NULL;
        return 1;
    }
    return 0;
}

void msgsrv_stream_close(MsgSrvSocket *msgsrv_socket TSRMLS_DC) {
    if (!msgsrv_socket->persistent) {
        php_stream_close(msgsrv_socket->stream);
    } else {
        php_stream_pclose(msgsrv_socket->stream);
    }
}

void msgsrv_free_socket(MsgSrvSocket *msgsrv_socket) {
    if (msgsrv_socket == NULL) return;
    
    if(msgsrv_socket->persistent_id) {
        pefree(msgsrv_socket->persistent_id, msgsrv_socket->persistent);
    }
    if(msgsrv_socket->hash_key) {
        pefree(msgsrv_socket->hash_key, msgsrv_socket->persistent);
    }
    if(msgsrv_socket->host) {
        pefree(msgsrv_socket->host, msgsrv_socket->persistent);
    }
    if(msgsrv_socket->app) {
        pefree(msgsrv_socket->app, msgsrv_socket->persistent);
    }
    if(msgsrv_socket->user) {
        pefree(msgsrv_socket->user, msgsrv_socket->persistent);
    }
    if(msgsrv_socket->pass) {
        pefree(msgsrv_socket->pass, msgsrv_socket->persistent);
    }
    if(msgsrv_socket->full_app) {
        pefree(msgsrv_socket->full_app, msgsrv_socket->persistent);  
    }
    pefree(msgsrv_socket, msgsrv_socket->persistent);
}

int msgsrv_playload_builder(char **ret, const char *target, const char *cmd, const char *body) {
    if (body == NULL) {
        return spprintf(ret, 0, "%s" _DELIM "%s", target, cmd);
    } else {
        return spprintf(ret, 0, "%s" _DELIM "%s" _DELIM "%s", target, cmd, body);
    }    
}

int msgsrv_playload_parser(char *playload, char **from, char **cmd, char **body) {
    char **ret = NULL;
    int c = 0;

    *from = NULL;
    *cmd = NULL;
    *body = NULL;

    c = str_split(playload, _DELIM, _DELIM_LENGTH, &ret, 3);
    if (c < 2) {
        return MSGSRV_PLAYLOAD_ERROR;
    } else if (c == 2) {    // eg: . ERROR
        *from = ret[0];
        *cmd = ret[1];
        efree(ret);
        return MSGSRV_PLAYLOAD_MINI;
    } else {    // eg: . CMD BODY
        *from = ret[0];
        *cmd = ret[1];
        *body = ret[2];
        efree(ret);
        return MSGSRV_PLAYLOAD_DEFAULT;
    }
}