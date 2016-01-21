/*
   +----------------------------------------------------------------------+
   | PHP MsgSrv Extension v1.2                                            |
   +----------------------------------------------------------------------+
   | Copyright (c) 2015 ChinaRoad Co., Ltd.  All rights reserved.         |
   +----------------------------------------------------------------------+
   | Author: Deng Tao <dengt@660pp.com>                                   |
   +----------------------------------------------------------------------+
*/

#ifndef MSGSRV_COMMON_H
#define MSGSRV_COMMON_H

#ifndef TRUE
#define TRUE 1
#endif

#ifndef ALIVE
#define ALIVE 1
#endif

#ifndef DEAD
#define DEAD 0
#endif

#ifndef FALSE
#define FALSE 0
#endif

#ifndef YES
#define YES 1
#endif

#ifndef NO
#define NO 0
#endif

//#define MSGSRV_DEBUG 1
#ifdef MSGSRV_DEBUG
    #define MSGSRV_DEBUG_SOCKET 1
    #define MSGSRV_DEBUG_PHP 1
#endif

// #define MSGSRV_VIRTUAL_PERSISTENT 1

#define MSGSRV_SOCKET_STATUS_FAILED 0
#define MSGSRV_SOCKET_STATUS_FINISH 9   // link is not reusable, should close it, and send [. _Bye]
#define MSGSRV_SOCKET_STATUS_DISCONNECTED 1
#define MSGSRV_SOCKET_STATUS_UNKNOWN 2
#define MSGSRV_SOCKET_STATUS_CONNECTED 3
#define MSGSRV_SOCKET_STATUS_GRANTED 4
#define MSGSRV_SOCKET_STATUS_DENY -1

#define MSGSRV_BUFFER_SIZE   1024

#define MSGSRV_PLAYLOAD_ERROR   -1
#define MSGSRV_PLAYLOAD_MINI    2
#define MSGSRV_PLAYLOAD_DEFAULT 3

#define MSGSRV_ERROR_NONE 0
#define MSGSRV_ERROR_READ_TIMEOUT 3112
#define MSGSRV_ERROR_READ_ERROR 3111
#define MSGSRV_ERROR_WRITE_FAILED 3113
#define MSGSRV_ERROR_DISCONNECT 3102
#define MSGSRV_ERROR_APPLICATION 3404
#define MSGSRV_ERROR_UNKOWN 3501
#define MSGSRV_ERROR_PLAYLOAD 3002

#define MSGSRV_STATUS_OK 1
#define MSGSRV_STATUS_TIMEOUT 0
#define MSGSRV_STATUS_DISCONNECT -1
#define MSGSRV_STATUS_READ_ERROR -2
#define MSGSRV_STATUS_ERROR_PLAYLOAD -3
#define MSGSRV_STATUS_ERROR_APPLICATION -4
#define MSGSRV_STATUS_UNKOWN -9

#define _NL "\n"
#define _NL_LENGTH 1
#define _NL_FIX '\r'
#define _DELIM " "

#define _DELIM_LENGTH 1

#define _LOGGER_OPEN    "[MsgSrv][O]"
#define _LOGGER_CLOSE   "[MsgSrv][C]"
#define _LOGGER_AUTH    "[MsgSrv][A]"
#define _LOGGER_FUNS    "[MsgSrv][F]"
#define _LOGGER_READ    "[MsgSrv][R]"
#define _LOGGER_WRITE   "[MsgSrv][W]"
#define _LOGGER_ERROR   "[MsgSrv][X]"
#define _LOGGER_SYS     "[MsgSrv][S]"
#define _LOGGER_PING    "[MsgSrv][P]"

#define LOCAL_MSGSRV_APPNAME "."

#define MSGSRV_CMD_MSGSRV "HELO"
#define MSGSRV_CMD_APPNAME "AppName"
#define MSGSRV_CMD_AUTH "AUTH"
#define MSGSRV_CMD_USER "User"
#define MSGSRV_CMD_ACCESS "ACCESS"
#define MSGSRV_CMD_ERROR "ERROR"
#define MSGSRV_CMD_CLOSE "_Bye"

#define MSGSRV_PLAYLOAD_FROM "from"
#define MSGSRV_PLAYLOAD_CMD "cmd"
#define MSGSRV_PLAYLOAD_BODY "body"

#define MSGSRV_ACCESS_GRANTED "GRANTED"
#define MSGSRV_ACCESS_DENY "DENY"

#define MSGSRV_ERROR_FLAG_APPLICATION "APPLICATION"

/* {{{ struct msgsrv_socket */
typedef struct _msgsrv_socket {
    long           idx;
    char           *host;
    unsigned short port;

    char           *app;
    char           *user;
    char           *pass;

    // for persistent
    long           pool_index;
    int            persistent;
    char           *persistent_id;
    int            persistent_id_len;
    char           *hash_key;
    int            hash_key_len;
    int            watching;
    int            busy;
    long           refcount;

    long           timeout;
    long           read_timeout;

    int            status;
    php_stream     *stream;

    time_t         recent_use_time; // in seconds

    char           in_buffer[MSGSRV_BUFFER_SIZE];   // read buffer

    char           *full_app;
} MsgSrvSocket;
/* }}} */

typedef struct _php_msgsrv_conn {
    MsgSrvSocket *socket;
    long           error;
    zval         *res;
    ulong        id;  /* index in page_links */
} php_msgsrv_conn;

#endif /* MSGSRV_COMMON_H */