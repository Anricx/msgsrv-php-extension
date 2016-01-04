/*
   +----------------------------------------------------------------------+
   | PHP MsgSrv Extension v1.2                                            |
   +----------------------------------------------------------------------+
   | Copyright (c) 2015 ChinaRoad Co., Ltd.  All rights reserved.         |
   +----------------------------------------------------------------------+
   | Author: Deng Tao <dengt@660pp.com>                                   |
   +----------------------------------------------------------------------+
*/

#ifndef MSGSRV_LIBRARY_H
#define MSGSRV_LIBRARY_H

#include "common.h"

/**
 * get rand by range.
 */
long rand_range(long min, long max TSRMLS_DC);

/**
 * caculate the md5 hash of str.
 * return the md5 hash of str in hex.
 */
char *md5_str(char *str);

/**
 * split str into array by delim limit with limit count.
 * return the count of ret array.
 */
extern int str_split(char *str, char *delim, const int delim_len, char*** ret, unsigned int limit);

/**
 * create MsgSrvSocket, auto init context by persistent.
 */
extern MsgSrvSocket *msgsrv_socket_create( 
                        char *host, 
                        unsigned short port,
                        char *app,
                        char *user,
                        char *pass,
                        long timeout, 
                        long read_timeout,
                        int persistent, long max_pool_size TSRMLS_DC);

/**
 * try to open the connection of msgsrv_socket.
 * 0 for open successfully.
 * -1 while some error.
 */
extern int msgsrv_socket_open(MsgSrvSocket *msgsrv_socket, int force_connect, int authorize, int trace_mode TSRMLS_DC);

/**
 * do msgsrv real connect
 * return:
 * -1 for connect failed!
 * -2 for login error!
 */
int msgsrv_socket_connect(MsgSrvSocket *msgsrv_socket, int authorize, int trace_mode TSRMLS_DC);

/**
 * check weather this link has disconnect.
 * TRUE for disconnect.
 * FALSE for not.
 */
int msgsrv_check_eof(MsgSrvSocket *msgsrv_socket, int trace_mode TSRMLS_DC);

/**
 * do msgsrv read.
 * return 
 * 0 link is disconnected.
 * -1 link error.
 * -2 memery alloc failed, close link!
 * -9 timeout
 * > 0 for playload length.
 */
extern int msgsrv_socket_read(MsgSrvSocket *msgsrv_socket, char **packet, long timeout, int trace_mode TSRMLS_DC);

/**
 * writes count bytes of data from buf into stream.
 * returns the number of bytes that were read successfully. 
 * If there was an error, the number of bytes written will be less than count.
 * 0 when success.
 * -1 for error.
 */
extern int msgsrv_socket_write(MsgSrvSocket *msgsrv_socket, char *packet, int trace_mode TSRMLS_DC);

/**
 * do msgsrv ping, to test connection.
 * SUCCESS for ok.
 * FAILURE for link error
 */
extern int msgsrv_socket_ping(MsgSrvSocket *msgsrv_socket, int trace_mode TSRMLS_DC);

/**
 * do msgsrv socket disconnect.
 * auto send '. Bye\n'
 * -1 for NULL msgsrv_socket.
 * 1 for close success.
 * 0 for NULL msgsrv_socket->stream
 */
extern int msgsrv_socket_disconnect(MsgSrvSocket *msgsrv_socket, int trace_mode TSRMLS_DC);

/**
 * close the php_stream for msgsrv_socket.
 */
void msgsrv_stream_close(MsgSrvSocket *msgsrv_socket TSRMLS_DC);

/**
 * free msgsrv_socket.
 */
extern void msgsrv_free_socket(MsgSrvSocket *msgsrv_socket TSRMLS_DC);

/**
 * build a msgsrv playload.
 * return the length of ret playload.
 */
extern int msgsrv_playload_builder(char **ret, const char *target, const char *cmd, const char *body);

/**
 * parse msgsrv playload to from cmd and body
 * return parse parameter's count.
 * MSGSRV_PLAYLOAD_MINI for eg: . ERROR
 * MSGSRV_PLAYLOAD_DEFAULT for eg: . Auth 127324
 * MSGSRV_PLAYLOAD_ERROR for invalid playload.
 */
extern int msgsrv_playload_parser(char *playload, char **from, char **cmd, char **body);

#endif /* MSGSRV_LIBRARY_H */
