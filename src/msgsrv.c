/*
   +----------------------------------------------------------------------+
   | PHP MsgSrv Extension v1.2                                            |
   +----------------------------------------------------------------------+
   | Copyright (c) 2015 ChinaRoad Co., Ltd.  All rights reserved.         |
   +----------------------------------------------------------------------+
   | Author: Deng Tao <dengt@660pp.com>                                   |
   +----------------------------------------------------------------------+
*/

/* $ Id: $ */ 

#include "php_msgsrv.h"

#if HAVE_MSGSRV

#include "syslog.h"

#include "common.h"
#include "library.h"

/* True globals, no need for thread safety */
static int msgsrv_link, msgsrv_plink;

#define PHP_MSGSRV_PAGE_LINKS_INIT_SIZE 32
#define UNKOWN_LINK_IDX (long) -1
#define PHP_MSGSRV_PAGE_PLINKS_INIT_SIZE 32
#define PHP_MSGSRV_DESCRIPTOR_RES_NAME "msgsrv link"
#define PHP_MSGSRV_DESCRIPTOR_PERSISTENT_RES_NAME "msgsrv persistent link"

#define SAFE_ZVAL_LONG(z, v) {          \
        MAKE_STD_ZVAL(z);                   \
        ZVAL_LONG(z, v);                  \
    }

#define SAFE_ZVAL_STRING(z, v) {          \
        MAKE_STD_ZVAL(z);                   \
        if (v == NULL) {                    \
            ZVAL_NULL(z);                   \
        } else {                            \
            ZVAL_STRING(z, v, TRUE);        \
        }                                   \
    }

#define SAFE_ZVAL_RESOURCE(z, v) {          \
        MAKE_STD_ZVAL(z);                   \
        if (v == NULL) {                    \
            ZVAL_NULL(z);                   \
        } else {                            \
            ZVAL_RESOURCE(z, Z_LVAL_P(v));     \
        }                                   \
    }

/* {{{ ZEND_INIT_MODULE_GLOBALS
 */
static void php_msgsrv_minit_globals(zend_msgsrv_globals *msgsrv_globals) {
    msgsrv_globals->trace_mode = 0;
    msgsrv_globals->allow_persistent = 1;
    msgsrv_globals->max_persistent = -1;
    msgsrv_globals->max_pool_size = 10;
    msgsrv_globals->max_links = -1;
    msgsrv_globals->connect_timeout = 10;
    msgsrv_globals->request_timeout = 10;
    msgsrv_globals->read_timeout = 6;

    msgsrv_globals->num_links = 0;
    msgsrv_globals->num_persistent = 0;
}
static void php_msgsrv_mshutdown_globals(zend_msgsrv_globals *msgsrv_globals) {
}
/* }}} */


/* {{{ php_msgsrv_do_connect
 */
static int php_msgsrv_do_connect(INTERNAL_FUNCTION_PARAMETERS, int persistent TSRMLS_DC)
{
    char *host=NULL, *app=NULL, *user=NULL, *pass=NULL;
    int  host_len, app_len, user_len, pass_len;
    long port = 0;
    list_entry  *le, new_le;
    long timeout = MSGSRV_SG(connect_timeout);   // in seconds
    long read_timeout = MSGSRV_SG(read_timeout); // in seconds
    php_msgsrv_conn *conn = NULL, *conn_dest = NULL;
    MsgSrvSocket *msgsrv_socket;
    ulong conn_id;
    long nil;   // this parameter only for msgsrv_open
    // only for persistent
    php_msgsrv_conn *pconn = NULL;

#ifdef ZTS
    /* not sure how in threaded mode this works so disabled persistents at first */
    persistent = FALSE;
#endif

    if (zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC, "slsss|l", &host, &host_len, &port, &app, &app_len, &user, &user_len, &pass, &pass_len, &nil) == FAILURE) {
        return FAILURE;
    }

    if (MSGSRV_SG(max_links) != -1 && MSGSRV_SG(num_links) >= MSGSRV_SG(max_links)) {
        if (MSGSRV_SG(trace_mode)) {
            syslog(LOG_ERR, _LOGGER_FUNS "[%ld] - [Connect]Too many open links (%ld).", UNKOWN_LINK_IDX, MSGSRV_SG(num_links));
        }
        #ifdef MSGSRV_DEBUG_PHP
        php_printf(_LOGGER_FUNS "[%ld] - [Connect]Too many open links (%ld).\n", UNKOWN_LINK_IDX, MSGSRV_SG(num_links));
        #endif

        php_error(E_WARNING, "Too many open links (%ld)", MSGSRV_SG(num_links));
        return FAILURE;
    }

    if (timeout < 0L || timeout > INT_MAX) {
        return FAILURE;
    }

    if (!MSGSRV_SG(allow_persistent)) {
        persistent = FALSE;
    }

#ifdef MSGSRV_VIRTUAL_PERSISTENT
    if (persistent == TRUE) {
        if (MSGSRV_SG(trace_mode)) {
            syslog(LOG_ERR, _LOGGER_FUNS "[%ld] - [Connect]Real persistent not support right now, switch to non persistent.", UNKOWN_LINK_IDX);
        }
        #ifdef MSGSRV_DEBUG_PHP
        php_printf(_LOGGER_FUNS "[%ld] - [Connect]Real persistent not support right now, switch to non persistent.\n", UNKOWN_LINK_IDX);
        #endif
        persistent = FALSE;
    }
#endif

reconnect:

    if (persistent) { /* persistent */
pconnect:
        
        if (MSGSRV_SG(max_persistent) != -1 && MSGSRV_SG(num_persistent) >= MSGSRV_SG(max_persistent)) {
            if (MSGSRV_SG(trace_mode)) {
                syslog(LOG_ERR, _LOGGER_FUNS "[%ld] - [Connect]Too many open links (%ld), switch to non persistent..", UNKOWN_LINK_IDX, MSGSRV_SG(num_links));
            }
            #ifdef MSGSRV_DEBUG_PHP
            php_printf(_LOGGER_FUNS "[%ld] - [Connect]Too many open links (%ld), switch to non persistent..\n", UNKOWN_LINK_IDX, MSGSRV_SG(num_links));
            #endif

            php_error(E_WARNING, "Too many open links (%ld), switch to non persistent.", MSGSRV_SG(num_links));

            persistent = FALSE;
            goto connect;
        }

        msgsrv_socket = msgsrv_socket_create(
                        host, port, 
                        app, 
                        user, 
                        pass,
                        timeout, 
                        read_timeout,
                        TRUE, MSGSRV_SG(max_pool_size) TSRMLS_CC);
    

        conn = (php_msgsrv_conn *) emalloc(sizeof(php_msgsrv_conn));    /* create the link */
        if (!conn) {
            if (MSGSRV_SG(trace_mode)) {
                syslog(LOG_ERR, _LOGGER_FUNS "[%ld] - [Connect]Out of memory while allocating memory for a link.", msgsrv_socket->idx);
            }
            #ifdef MSGSRV_DEBUG_PHP
            php_printf(_LOGGER_FUNS "[%ld] - [Connect]Out of memory while allocating memory for a link.\n", msgsrv_socket->idx);
            #endif

            msgsrv_free_socket(msgsrv_socket);
            php_error(E_ERROR, "Out of memory while allocating memory for a link");
            return FAILURE;
        }

# ifdef ZTS
        tsrm_mutex_lock( locale_mutex );
# endif

        /* try to find if we already have this link in our persistent list */
        if (zend_hash_find(&EG(persistent_list), msgsrv_socket->hash_key, msgsrv_socket->hash_key_len + 1, (void **) &le) == SUCCESS) {  /* The link is in our list of persistent connections */
            msgsrv_free_socket(msgsrv_socket);
            if (Z_TYPE_P(le) != msgsrv_plink) {
# ifdef ZTS
        tsrm_mutex_unlock( locale_mutex );
# endif
                return FAILURE;
            }
            pconn = (php_msgsrv_conn *) le->ptr;
            msgsrv_socket = pconn->socket;
            if (MSGSRV_SG(trace_mode)) {
                syslog(LOG_DEBUG, _LOGGER_FUNS "[%ld] - [Connect]Try to get a persistent connection for %s.", msgsrv_socket->idx, msgsrv_socket->persistent_id);
            }
            #ifdef MSGSRV_DEBUG_PHP
            php_printf(_LOGGER_FUNS "[%ld] - [Connect]Try to get a persistent connection for %s.\n", msgsrv_socket->idx, msgsrv_socket->persistent_id);
            #endif

            if (msgsrv_socket->busy == YES) {
                if (MSGSRV_SG(trace_mode)) {
                    syslog(LOG_DEBUG, _LOGGER_FUNS "[%ld] - [Connect]Current persistent connection %s is busy, switch to non persistent!", msgsrv_socket->idx, msgsrv_socket->persistent_id);
                }
                #ifdef MSGSRV_DEBUG_PHP
                php_printf(_LOGGER_FUNS "[%ld] - [Connect]Current persistent connection %s is busy, switch to non persistent!\n", msgsrv_socket->idx, msgsrv_socket->persistent_id);
                #endif

                efree(conn);
                // switch none persistent connect
                persistent = FALSE;
# ifdef ZTS
        tsrm_mutex_unlock( locale_mutex );
# endif
                goto connect;
            } else {
                /* Try to fix mutil thread... */
                pconn->socket->refcount ++;
                if (pconn->socket->refcount > 1) {
                    if (MSGSRV_SG(trace_mode)) {
                        syslog(LOG_DEBUG, _LOGGER_FUNS "[%ld] - [Connect]Current persistent connection %s is busy, switch to non persistent!", msgsrv_socket->idx, msgsrv_socket->persistent_id);
                    }
                    #ifdef MSGSRV_DEBUG_PHP
                    php_printf(_LOGGER_FUNS "[%ld] - [Connect]Current persistent connection %s is busy, switch to non persistent!\n", msgsrv_socket->idx, msgsrv_socket->persistent_id);
                    #endif

                    efree(conn);

                    // switch none persistent connect
                    persistent = FALSE;

# ifdef ZTS
        tsrm_mutex_unlock( locale_mutex );
# endif
                    goto connect;
                }
                pconn->socket->busy = YES;
            }

            conn->socket = pconn->socket;
            conn->error = pconn->error;
            // Do Link Test...
            if (msgsrv_socket_ping(msgsrv_socket, MSGSRV_SG(trace_mode) TSRMLS_CC) == FAILURE) {
                pconn->socket->busy = NO;
                pconn->socket->refcount = 0;

                if (MSGSRV_SG(trace_mode)) {
                    syslog(LOG_DEBUG, _LOGGER_FUNS "[%ld] - [Connect]Current persistent connection %s ping failed!", msgsrv_socket->idx, msgsrv_socket->persistent_id);
                }
                #ifdef MSGSRV_DEBUG_PHP
                php_printf(_LOGGER_FUNS "[%ld] - [Connect]Current persistent connection %s ping failed!\n", msgsrv_socket->idx, msgsrv_socket->persistent_id);
                #endif
                
                if (MSGSRV_SG(trace_mode)) {
                    syslog(LOG_DEBUG, _LOGGER_FUNS "[%ld] - [Connect]Reconnect msgsrv://%s:%ld...", msgsrv_socket->idx, host, port);
                }
                #ifdef MSGSRV_DEBUG_PHP
                php_printf(_LOGGER_FUNS "[%ld] - [Connect]Reconnect msgsrv://%s:%ld...\n", msgsrv_socket->idx, host, port);
                #endif

                zend_hash_del(&EG(persistent_list), msgsrv_socket->hash_key, msgsrv_socket->hash_key_len + 1);
                MSGSRV_SG(num_links)--;
                
                efree(conn);
# ifdef ZTS
        tsrm_mutex_unlock( locale_mutex );
# endif
                goto pconnect;
            }
            /* Push this new link into page link table! */
            conn_id = zend_hash_next_free_element(MSGSRV_SG(page_links));
            if (zend_hash_index_update(MSGSRV_SG(page_links), conn_id, (void *)conn, sizeof(php_msgsrv_conn), (void **)&conn_dest) == FAILURE) {
                pconn->socket->busy = NO;
                pconn->socket->refcount = 0;

                if (MSGSRV_SG(trace_mode)) {
                    syslog(LOG_ERR, _LOGGER_FUNS "[%ld] - [Open]Out of memory while cache link in page link table!", msgsrv_socket->idx);
                }
                #ifdef MSGSRV_DEBUG_PHP
                php_printf(_LOGGER_FUNS "[%ld] - [Open]Out of memory while cache link in page link table!\n", msgsrv_socket->idx);
                #endif

                php_error(E_WARNING, "Out of memory while cache link in page link table!");

                msgsrv_socket_disconnect(msgsrv_socket, MSGSRV_SG(trace_mode) TSRMLS_CC);
                msgsrv_free_socket(msgsrv_socket);    
                efree(conn);
# ifdef ZTS
        tsrm_mutex_unlock( locale_mutex );
# endif
                return FAILURE;
            }
            efree(conn);

            /* add it to the list */            
            ZEND_REGISTER_RESOURCE(return_value, conn_dest, msgsrv_plink);
            conn_dest->res = return_value;
            conn_dest->id = conn_id;

        } else {  /* we don't */
            conn->error = MSGSRV_ERROR_NONE;

            if (MSGSRV_SG(trace_mode)) {
                syslog(LOG_DEBUG, _LOGGER_FUNS "[%ld] - [Connect]Try to create a persistent connection for %s.", msgsrv_socket->idx, msgsrv_socket->persistent_id);
            }
            #ifdef MSGSRV_DEBUG_PHP
            php_printf(_LOGGER_FUNS "[%ld] - [Connect]Try to create a persistent connection for %s.\n", msgsrv_socket->idx, msgsrv_socket->persistent_id);
            #endif

            pconn = (php_msgsrv_conn *) pemalloc(sizeof(php_msgsrv_conn), persistent);    /* create the link detail */
            if (!pconn) {                
                if (MSGSRV_SG(trace_mode)) {
                    syslog(LOG_ERR, _LOGGER_FUNS "[%ld] - [Connect]Out of memory while allocating memory for a persistent link.", msgsrv_socket->idx);
                }
                #ifdef MSGSRV_DEBUG_PHP
                php_printf(_LOGGER_FUNS "[%ld] - [Connect]Out of memory while allocating memory for a persistent link.\n", msgsrv_socket->idx);
                #endif
                msgsrv_free_socket(msgsrv_socket);
                php_error(E_ERROR, "Out of memory while allocating memory for a persistent link");
                efree(conn);
# ifdef ZTS
        tsrm_mutex_unlock( locale_mutex );
# endif                
                return FAILURE;
            }

            if (msgsrv_socket_open(msgsrv_socket, YES, YES, MSGSRV_SG(trace_mode) TSRMLS_CC) < 0) {
                msgsrv_socket_disconnect(msgsrv_socket, MSGSRV_SG(trace_mode) TSRMLS_CC);
                msgsrv_free_socket(msgsrv_socket);
                efree(conn);
                pefree(pconn, persistent);
# ifdef ZTS
        tsrm_mutex_unlock( locale_mutex );
# endif
                return FAILURE;
            }
            conn->socket = msgsrv_socket;
            pconn->socket = msgsrv_socket;
            pconn->error = MSGSRV_ERROR_NONE;
            pconn->socket->refcount = 1;
            pconn->socket->busy = YES;

            /* hash it up */
            new_le.type = msgsrv_plink;
            new_le.ptr = pconn;
            if (zend_hash_update(&EG(persistent_list), msgsrv_socket->hash_key, msgsrv_socket->hash_key_len + 1, (void *) &new_le, sizeof(list_entry), NULL) == FAILURE) {
                msgsrv_socket_disconnect(msgsrv_socket, MSGSRV_SG(trace_mode) TSRMLS_CC);
                msgsrv_free_socket(msgsrv_socket);
                efree(conn);
                pefree(pconn, persistent);
# ifdef ZTS
        tsrm_mutex_unlock( locale_mutex );
# endif
                return FAILURE;
            }

            /* **************DEBUG
            syslog(LOG_INFO, _LOGGER_FUNS "Pool Links..........");
            for(zend_hash_internal_pointer_reset(&EG(persistent_list));
                zend_hash_has_more_elements(&EG(persistent_list)) == SUCCESS; zend_hash_move_forward(&EG(persistent_list))) {
                // #0:get msgsrv link
                if (zend_hash_get_current_data(&EG(persistent_list), (void**)&le) == FAILURE) {
                    // Should never actually fail, since the key is known to exist.
                    continue;
                }
                conn_dest = (php_msgsrv_conn *) le->ptr;
                syslog(LOG_ERR, _LOGGER_FUNS "%s\n", conn_dest->socket->full_app);
            }
            //**************DEBUG */

            /* Push this new link into page link table! */
            conn_id = zend_hash_next_free_element(MSGSRV_SG(page_links));
            if (zend_hash_index_update(MSGSRV_SG(page_links), conn_id, (void *)conn, sizeof(php_msgsrv_conn), (void **)&conn_dest) == FAILURE) {
                if (MSGSRV_SG(trace_mode)) {
                    syslog(LOG_ERR, _LOGGER_FUNS "[%ld] - [Open]Out of memory while cache link in page link table!", msgsrv_socket->idx);
                }
                #ifdef MSGSRV_DEBUG_PHP
                php_printf(_LOGGER_FUNS "[%ld] - [Open]Out of memory while cache link in page link table!\n", msgsrv_socket->idx);
                #endif

                php_error(E_WARNING, "Out of memory while cache link in page link table!");

                msgsrv_socket_disconnect(msgsrv_socket, MSGSRV_SG(trace_mode) TSRMLS_CC);
                msgsrv_free_socket(msgsrv_socket);    
                efree(conn);
                pefree(pconn, persistent);
# ifdef ZTS
        tsrm_mutex_unlock( locale_mutex );
# endif
                return FAILURE;
            }
            efree(conn);

            /* add it to the list */
            ZEND_REGISTER_RESOURCE(return_value, conn_dest, msgsrv_plink);
            conn_dest->res = return_value;
            conn_dest->id = conn_id;
            MSGSRV_SG(num_persistent)++;
            MSGSRV_SG(num_links)++;
        }

# ifdef ZTS
        tsrm_mutex_unlock( locale_mutex );
# endif

        return SUCCESS;
    } else { /* non persistent */

connect:

        msgsrv_socket = msgsrv_socket_create(
                            host, port, 
                            app, 
                            user, 
                            pass,
                            timeout, 
                            read_timeout,
                            FALSE, 0 TSRMLS_CC);

        conn = (php_msgsrv_conn *) emalloc(sizeof(php_msgsrv_conn));    /* create the link */
        if (!conn) {
            if (MSGSRV_SG(trace_mode)) {
                syslog(LOG_ERR, _LOGGER_FUNS "[%ld] - [Connect]Out of memory while allocating memory for a link.", msgsrv_socket->idx);
            }
            #ifdef MSGSRV_DEBUG_PHP
            php_printf(_LOGGER_FUNS "[%ld] - [Connect]Out of memory while allocating memory for a link.\n", msgsrv_socket->idx);
            #endif

            msgsrv_free_socket(msgsrv_socket);
            php_error(E_ERROR, "Out of memory while allocating memory for a link");
            return FAILURE;
        }
        conn->error = MSGSRV_ERROR_NONE;

        if (msgsrv_socket_open(msgsrv_socket, YES, YES, MSGSRV_SG(trace_mode) TSRMLS_CC) < 0) {
            msgsrv_socket_disconnect(msgsrv_socket, MSGSRV_SG(trace_mode) TSRMLS_CC);
            msgsrv_free_socket(msgsrv_socket);
            efree(conn);
            return FAILURE;
        }
        conn->socket = msgsrv_socket;

        /* Push this new link into page link table! */
        conn_id = zend_hash_next_free_element(MSGSRV_SG(page_links));
        if (zend_hash_index_update(MSGSRV_SG(page_links), conn_id, (void *)conn, sizeof(php_msgsrv_conn), (void **)&conn_dest) == FAILURE) {
            if (MSGSRV_SG(trace_mode)) {
                syslog(LOG_ERR, _LOGGER_FUNS "[%ld] - [Open]Out of memory while cache link in page link table!", msgsrv_socket->idx);
            }
            #ifdef MSGSRV_DEBUG_PHP
            php_printf(_LOGGER_FUNS "[%ld] - [Open]Out of memory while cache link in page link table!\n", msgsrv_socket->idx);
            #endif

            php_error(E_WARNING, "Out of memory while cache link in page link table!");

            msgsrv_socket_disconnect(msgsrv_socket, MSGSRV_SG(trace_mode) TSRMLS_CC);
            msgsrv_free_socket(msgsrv_socket);
            efree(conn);        
            return FAILURE;       
        }
        efree(conn);

        /* add it to the list */
        ZEND_REGISTER_RESOURCE(return_value, conn_dest, msgsrv_link);
        conn_dest->res = return_value;
        conn_dest->id = conn_id;
        MSGSRV_SG(num_links)++;
        return SUCCESS;
    }    
}
/* }}} */


/* {{{ php_msgsrv_do_send
 */
static int php_msgsrv_do_send(INTERNAL_FUNCTION_PARAMETERS TSRMLS_DC) {
    zval * link_res = NULL;
    php_msgsrv_conn *conn = NULL;
    MsgSrvSocket *msgsrv_socket = NULL;
    char *playload = NULL;

    const char * target = NULL;
    int target_len = 0;
    const char * cmd = NULL;
    int cmd_len = 0;
    const char * body = NULL;
    int body_len = 0;

    if (zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC, "sssr", &target, &target_len, &cmd, &cmd_len, &body, &body_len, &link_res) == FAILURE) {
        return FAILURE;
    }
    /* Get php_msgsrv_conn from resources */
    ZEND_FETCH_RESOURCE2(conn, php_msgsrv_conn *, &link_res, -1, NULL, msgsrv_link, msgsrv_plink)
    if (conn == NULL) {
        if (MSGSRV_SG(trace_mode)) {
            syslog(LOG_ERR, _LOGGER_FUNS "[%ld] - [Send]Playload(%s %s %s) Failed(connection not found, closed?)!", UNKOWN_LINK_IDX, target, cmd, body);
        }
        #ifdef MSGSRV_DEBUG_PHP
        php_printf(_LOGGER_FUNS "[%ld] - [Send]Playload(%s %s %s) Failed(connection not found, closed?)!\n", UNKOWN_LINK_IDX, target, cmd, body);
        #endif

        php_error(E_WARNING, "Playload(%s %s %s) Failed(connection not found, closed?)!", target, cmd, body);
        FREE_RESOURCE(link_res);
        return FAILURE;
    }
    if (conn->socket == NULL) {
        if (MSGSRV_SG(trace_mode)) {
            syslog(LOG_ERR, _LOGGER_FUNS "[%ld] - [Send]Playload(%s %s %s) Failed(illegal resource, bug?)!", UNKOWN_LINK_IDX, target, cmd, body);
        }
        #ifdef MSGSRV_DEBUG_PHP
        php_printf(_LOGGER_FUNS "[%ld] - [Send]Playload(%s %s %s) Failed(illegal resource, bug?)!\n", UNKOWN_LINK_IDX, target, cmd, body);
        #endif

        php_error(E_WARNING, "Playload(%s %s %s) Failed(illegal resource, bug?)!", target, cmd, body);
        FREE_RESOURCE(link_res);
        return FAILURE;
    }
    if (conn->socket->status == MSGSRV_SOCKET_STATUS_FINISH) {
        if (MSGSRV_SG(trace_mode)) {
            syslog(LOG_ERR, _LOGGER_FUNS "[%ld] - [Send]Playload(%s %s %s) Refused(link error, get another link)!", UNKOWN_LINK_IDX, target, cmd, body);
        }
        #ifdef MSGSRV_DEBUG_PHP
        php_printf(_LOGGER_FUNS "[%ld] - [Send]Playload(%s %s %s) Refused(link error, get another link)!\n", UNKOWN_LINK_IDX, target, cmd, body);
        #endif

        php_error(E_WARNING, "Playload(%s %s %s) Refused(link error, get another link)!", target, cmd, body);
        FREE_RESOURCE(link_res);
        return FAILURE;
    }

    /* Get socket connection */
    msgsrv_socket = conn->socket;

    /* packet playload */
    if (msgsrv_playload_builder(&playload, target, cmd, body) == 0) {
        if (MSGSRV_SG(trace_mode)) {
            syslog(LOG_ERR, _LOGGER_FUNS "[%ld] - [Send]Playload(%s %s %s) Failed(packet failed, bug?)!", msgsrv_socket->idx, target, cmd, body);
        }
        #ifdef MSGSRV_DEBUG_PHP
        php_printf(_LOGGER_FUNS "[%ld] - [Send]Playload(%s %s %s) Failed(packet failed, bug?)!\n", msgsrv_socket->idx, target, cmd, body);
        #endif

        php_error(E_WARNING, "Playload(%s %s %s) Failed(packet failed, bug?)!", target, cmd, body);
        return FAILURE;
    }
    /*
    if (MSGSRV_SG(trace_mode)) {
        syslog(LOG_INFO, _LOGGER_FUNS "[%ld] - [Send]Playload => [%s]...", msgsrv_socket->idx, playload);
    }
    #ifdef MSGSRV_DEBUG_PHP
    php_printf(_LOGGER_FUNS "[%ld] - [Send]Playload => [%s]...\n", msgsrv_socket->idx, playload);
    #endif
    */

    /* playload packet, do real send! */
    if (msgsrv_socket_write(msgsrv_socket, playload, MSGSRV_SG(trace_mode) TSRMLS_CC) == 0) {
        if (MSGSRV_SG(trace_mode)) {
            syslog(LOG_DEBUG, _LOGGER_FUNS "[%ld] - [Send]Playload => [%s] Successfully!", msgsrv_socket->idx, playload);
        }
        #ifdef MSGSRV_DEBUG_PHP
        php_printf(_LOGGER_FUNS "[%ld] - [Send]Playload => [%s] Successfully!\n", msgsrv_socket->idx, playload);
        #endif

        efree(playload);
        return SUCCESS;
    } else {
        if (MSGSRV_SG(trace_mode)) {
            syslog(LOG_ERR, _LOGGER_FUNS "[%ld] - [Send]Playload => [%s] Failed(stream write failed, link down?)!", msgsrv_socket->idx, playload);
        }
        #ifdef MSGSRV_DEBUG_PHP
        php_printf(_LOGGER_FUNS "[%ld] - [Send]Playload => [%s] Failed(stream write failed, link down?)!\n", msgsrv_socket->idx, playload);
        #endif
        php_error(E_WARNING, "Playload(%s) Failed(stream write failed, link down?)!", playload);

        conn->error = MSGSRV_ERROR_WRITE_FAILED;
        conn->socket->status = MSGSRV_SOCKET_STATUS_FINISH;
        efree(playload);
        return FAILURE;
    }
}
/* }}} */


/* {{{ _php_msgsrv_do_callback */
static int _php_msgsrv_do_callback(
                            zend_fcall_info fci, 
                            zend_fcall_info_cache fcc, 
                            long status,
                            char *from,
                            char *cmd,
                            char *body,
                            zval *link_res) {
    zval *retval_ptr = NULL;
    zval **params[5];
    zval *_status = NULL, *_from = NULL, *_cmd = NULL, *_body = NULL, *_link = NULL;
    SAFE_ZVAL_LONG(_status, status);
    SAFE_ZVAL_STRING(_from, from);
    SAFE_ZVAL_STRING(_cmd, cmd);
    SAFE_ZVAL_STRING(_body, body);
    SAFE_ZVAL_RESOURCE(_link, link_res);
    params[0] = &_status; params[1] = &_from; params[2] = &_cmd; params[3] = &_body; params[4] = &_link;

    fci.params = params;
    fci.retval_ptr_ptr = &retval_ptr;
    fci.param_count = 5;
    fci.no_separation = 1;

    if (zend_call_function(&fci, &fcc TSRMLS_CC) != SUCCESS) return FAILURE;
    if (retval_ptr != NULL) zval_ptr_dtor(&retval_ptr);
    return SUCCESS;
}
/* }}} */


/* MSGSRV_RECEIVE_SAFE_CALLBACK */
#define MSGSRV_RECEIVE_SAFE_CALLBACK(fci, fcc, s, f, c, b, l, r, idx) {                     \
        if (_php_msgsrv_do_callback(fci, fcc, s, f, c, b, l) == FAILURE) {        \
            if (MSGSRV_SG(trace_mode)) {             \
                syslog(LOG_ERR, _LOGGER_FUNS "[%ld] - [Receive]Playload Receive Failed[Timeout:%ld, Limit:%ld](invalid callback function?)!", idx, timeout, limit);   \
            }   \
            php_error(E_WARNING, "Playload Receive Failed[Timeout:%ld, Limit:%ld](invalid callback function?)!", timeout, limit);   \
            if (r) return FAILURE; \
        } else {    \
            if (MSGSRV_SG(trace_mode)) { \
                syslog(LOG_INFO, _LOGGER_FUNS "[%ld] - [Receive]Playload Receive Callback Successfully!", idx);   \
            }   \
            if (r) return SUCCESS; \
        }   \
    }


/* {{{ _php_msgsrv_do_receive_any */
static int _php_msgsrv_do_receive_any(zend_fcall_info fci, zend_fcall_info_cache fcc, long timeout, long limit TSRMLS_DC) {
    int  received = 0, ret = -1, _i = 0, failed = 0;
    HashTable *link_hash = MSGSRV_SG(page_links);
    php_msgsrv_conn *conn = NULL;
    char *playload = NULL, *_from = NULL, *_cmd = NULL, *_body = NULL;
    struct timeval now, start;
    ulong conn_id;

    // set receive start time...
    gettimeofday(&start, NULL);

    // Check if we have any link to read from...
    zend_hash_internal_pointer_reset(link_hash);
    if (zend_hash_has_more_elements(link_hash) != SUCCESS) {
        if (MSGSRV_SG(trace_mode)) {
            syslog(LOG_ERR, _LOGGER_FUNS "[%ld] - [Receive]Playload Receive Refused(There is No MsgSrv-Link Available!).", UNKOWN_LINK_IDX);
        }
        #ifdef MSGSRV_DEBUG_PHP
        php_printf(_LOGGER_FUNS "[%ld] - [Receive]Playload Receive Refused(There is No MsgSrv-Link Available!).\n", UNKOWN_LINK_IDX);
        #endif

        php_error(E_WARNING, "Playload Receive Refused - There is No MsgSrv-Link Available!");
        return FAILURE;
    }

    /* try to receive data */
    do {
        // keep read until timeout or max buffer size or reach the end of line '\n'.
        gettimeofday(&now, NULL);
        if ((now.tv_sec - start.tv_sec) >= timeout) {   /* receive timeout... */
            if (MSGSRV_SG(trace_mode)) {
                syslog(LOG_ERR, _LOGGER_FUNS "[%ld] - [Receive]Playload Receive Timeout(Start: %lds, Now: %lds, Timeout: %lds, Received: %d, Limit: %ld).", UNKOWN_LINK_IDX, start.tv_sec, now.tv_sec, timeout, received, limit);
            }
            #ifdef MSGSRV_DEBUG_PHP
            php_printf(_LOGGER_FUNS "[%ld] - [Receive]Playload Receive Timeout(Start: %lds, Now: %lds, Timeout: %lds, Received: %d, Limit: %ld).\n", UNKOWN_LINK_IDX, start.tv_sec, now.tv_sec, timeout, received, limit);
            #endif

            php_error(E_WARNING, "Playload Receive Timeout(Start: %lds, Now: %lds, Timeout: %lds, Received: %d, Limit: %ld).", start.tv_sec, now.tv_sec, timeout, received, limit);

            for (_i = received; _i < limit; ++_i) {
                MSGSRV_RECEIVE_SAFE_CALLBACK(fci, fcc, MSGSRV_STATUS_TIMEOUT, NULL, NULL, NULL, NULL, NO, UNKOWN_LINK_IDX);
            }

            return FAILURE;
        }

        // loop all links
        for(zend_hash_internal_pointer_reset(link_hash);
                zend_hash_has_more_elements(link_hash) == SUCCESS; zend_hash_move_forward(link_hash)) {
            // #0:get msgsrv link
            if (zend_hash_get_current_data(link_hash, (void**)&conn) == FAILURE) {
                // Should never actually fail, since the key is known to exist.
                continue;
            }

            if (conn == NULL || conn->socket == NULL) continue;

            // Check Link Status...
            if (conn->socket->status == MSGSRV_SOCKET_STATUS_FINISH) {
                if (MSGSRV_SG(trace_mode)) {
                    syslog(LOG_NOTICE, _LOGGER_FUNS "[%ld] - [Receive]Playload Receive Skip(Connection is not usable)!", conn->socket->idx);
                }
                #ifdef MSGSRV_DEBUG_PHP
                php_printf(_LOGGER_FUNS "[%ld] - [Receive]Playload Receive Skip(Connection is not usable)!\n", conn->socket->idx);
                #endif
                continue;
            }

            /* #1: read from socket */
            ret = msgsrv_socket_read(conn->socket, &playload, timeout, MSGSRV_SG(trace_mode) TSRMLS_CC);
            switch (ret) {
                case 0: // link closed!
                    if (MSGSRV_SG(trace_mode)) {
                        syslog(LOG_ERR, _LOGGER_FUNS "[%ld] - [Receive]Playload Receive Failed(lose connection while watching the playload)!", conn->socket->idx);
                    }
                    #ifdef MSGSRV_DEBUG_PHP
                    php_printf(_LOGGER_FUNS "[%ld] - [Receive]Playload Receive Failed(lose connection while watching the playload)!\n", conn->socket->idx);
                    #endif
                    php_error(E_WARNING, "Playload Receive Failed(lose connection while watching the playload)!");

                    conn->error = MSGSRV_ERROR_DISCONNECT;

                    MSGSRV_RECEIVE_SAFE_CALLBACK(fci, fcc, MSGSRV_STATUS_DISCONNECT, NULL, NULL, NULL, conn->res, YES, conn->socket->idx);

                case -9:    // timeout
                    continue;
                case -1:
                case -2:    // read error close link!
                    if (MSGSRV_SG(trace_mode)) {
                        syslog(LOG_ERR, _LOGGER_FUNS "[%ld] - [Receive]Playload Receive Failed(read error while watching the playload)!", conn->socket->idx);
                    }
                    #ifdef MSGSRV_DEBUG_PHP
                    php_printf(_LOGGER_FUNS "[%ld] - [Receive]Playload Receive Failed(read error while watching the playload)!\n", conn->socket->idx);
                    #endif
                    php_error(E_WARNING, "Playload Receive Failed(read error while watching the playload)!");

                    conn->error = MSGSRV_ERROR_READ_ERROR;
                    conn->socket->status = MSGSRV_SOCKET_STATUS_FINISH;

                    MSGSRV_RECEIVE_SAFE_CALLBACK(fci, fcc, MSGSRV_STATUS_READ_ERROR, NULL, NULL, NULL, conn->res, YES, conn->socket->idx);

                default:
                    if (ret < 1) {  // unkown error!
                        if (MSGSRV_SG(trace_mode)) {
                            syslog(LOG_ERR, _LOGGER_FUNS "[%ld] - [Receive]Playload Receive Failed(unkown error:%d while watching the playload)!", conn->socket->idx, ret);
                        }
                        #ifdef MSGSRV_DEBUG_PHP
                        php_printf(_LOGGER_FUNS "[%ld] - [Receive]Playload Receive Failed(unkown error:%d while watching the playload)!\n", conn->socket->idx, ret);
                        #endif
                        php_error(E_WARNING, "Playload Receive Failed(unkown error:%d while watching the playload)!", ret);

                        conn->error = MSGSRV_ERROR_READ_ERROR;
                        conn->socket->status = MSGSRV_SOCKET_STATUS_FINISH;
                        
                        MSGSRV_RECEIVE_SAFE_CALLBACK(fci, fcc, MSGSRV_STATUS_UNKOWN, NULL, NULL, NULL, conn->res, YES, conn->socket->idx);
                    }
                    break;
            }

            if (MSGSRV_SG(trace_mode)) {
                syslog(LOG_INFO, _LOGGER_FUNS "[%ld] - [Receive]Playload Received => [%s]", conn->socket->idx, playload);
            }
            #ifdef MSGSRV_DEBUG_PHP
            php_printf(_LOGGER_FUNS "[%ld] - [Receive]Playload Received => [%s]\n", conn->socket->idx, playload);
            #endif

            /* #2: playload reached! */
            received ++;
            ret = msgsrv_playload_parser(playload, &_from, &_cmd, &_body);
            switch(ret) {
                case MSGSRV_PLAYLOAD_MINI:
                case MSGSRV_PLAYLOAD_DEFAULT:
                    break;

                case MSGSRV_PLAYLOAD_ERROR:   // error playload
                default:    // unkown error
                    if (MSGSRV_SG(trace_mode)) {
                        syslog(LOG_ERR, _LOGGER_FUNS "[%ld] - [Receive]Playload Receive Error, Illegal Replay Playload => [%s]", conn->socket->idx, playload);
                    }
                    #ifdef MSGSRV_DEBUG_PHP
                    php_printf(_LOGGER_FUNS "[%ld] - [Receive]Playload Receive Error, Illegal Replay Playload => [%s]\n", conn->socket->idx, playload);
                    #endif
                    php_error(E_WARNING, "Playload Receive Error, Illegal Replay Playload => [%s].", playload);
                    efree(playload);

                    conn->error = MSGSRV_ERROR_PLAYLOAD;
                    conn->socket->status = MSGSRV_SOCKET_STATUS_FINISH;
                    MSGSRV_RECEIVE_SAFE_CALLBACK(fci, fcc, MSGSRV_STATUS_ERROR_PLAYLOAD, NULL, NULL, NULL, conn->res, YES, conn->socket->idx);
                    continue;
            }
            /* #3: detect msgsrv error */
            if (strcmp(LOCAL_MSGSRV_APPNAME, _from) == 0 && strcmp(MSGSRV_CMD_ERROR, _cmd) == 0) {  // msgsrv error detected
                failed ++;
                if (strcspn(_body, MSGSRV_ERROR_FLAG_APPLICATION) == 0) {   // . ERROR APPLICATION
                    if (MSGSRV_SG(trace_mode)) {
                        syslog(LOG_ERR, _LOGGER_FUNS "[%ld] - [Receive]Playload Receive Error, Target Application Not Found(%s).", conn->socket->idx, _body);
                    }
                    #ifdef MSGSRV_DEBUG_PHP
                    php_printf(_LOGGER_FUNS "[%ld] - [Receive]Playload Receive Error, Target Application Not Found(%s).\n", conn->socket->idx, _body);
                    #endif
                    php_error(E_WARNING, "Playload Receive Error, Target Application Not Found(%s).", _body);
                    
                    MSGSRV_RECEIVE_SAFE_CALLBACK(fci, fcc, MSGSRV_STATUS_ERROR_APPLICATION, _from, _cmd, _body, conn->res, NO, conn->socket->idx);
                } else {
                    if (MSGSRV_SG(trace_mode)) {
                        syslog(LOG_ERR, _LOGGER_FUNS "[%ld] - [Receive]Playload Receive Error, Unkown Error(%s).", conn->socket->idx, _body);
                    }
                    #ifdef MSGSRV_DEBUG_PHP
                    php_printf(_LOGGER_FUNS "[%ld] - [Receive]Playload Receive Error, Unkown Error(%s).\n", conn->socket->idx, _body);
                    #endif
                    php_error(E_WARNING, "Playload Receive Error, Unkown Error(%s).", _body);

                    MSGSRV_RECEIVE_SAFE_CALLBACK(fci, fcc, MSGSRV_STATUS_UNKOWN, _from, _cmd, _body, conn->res, NO, conn->socket->idx);
                }
            } else {
                MSGSRV_RECEIVE_SAFE_CALLBACK(fci, fcc, MSGSRV_STATUS_OK, _from, _cmd, _body, conn->res, NO, conn->socket->idx);
            }
            efree(playload);
        }
    } while (received < limit);

    return failed > 0 ? FAILURE : SUCCESS;
}
/* }}} */


/* {{{ _php_msgsrv_do_receive_link */
static int _php_msgsrv_do_receive_link(zend_fcall_info fci, zend_fcall_info_cache fcc, zval *link_res, long timeout, long limit, INTERNAL_FUNCTION_PARAMETERS TSRMLS_DC) {
    int  received = 0, ret = -1, _i = 0, failed = 0;
    php_msgsrv_conn *conn = NULL;
    char *playload = NULL, *_from = NULL, *_cmd = NULL, *_body = NULL;
    struct timeval now, start;

    ZEND_FETCH_RESOURCE2(conn, php_msgsrv_conn *, &link_res, -1, NULL, msgsrv_link, msgsrv_plink);
    if (conn == NULL) {
        if (MSGSRV_SG(trace_mode)) {
            syslog(LOG_ERR, _LOGGER_FUNS "[%ld] - [Receive]Playload Receive Failed[Timeout:%ld, Limit:%ld](connection not found, closed?)!", UNKOWN_LINK_IDX, timeout, limit);
        }
        #ifdef MSGSRV_DEBUG_PHP
        php_printf(_LOGGER_FUNS "[%ld] - [Receive]Playload Receive Failed[Timeout:%ld, Limit:%ld](connection not found, closed?)!\n", UNKOWN_LINK_IDX, timeout, limit);
        #endif

        php_error(E_WARNING, "Playload Receive Failed[Timeout:%ld, Limit:%ld](connection not found, closed?)!", timeout, limit);

        FREE_RESOURCE(link_res);
        return FAILURE;
    }
    if (conn->socket == NULL) {
        if (MSGSRV_SG(trace_mode)) {
            syslog(LOG_ERR, _LOGGER_FUNS "[%ld] - [Receive]Playload Receive Failed[Timeout:%ld, Limit:%ld](illegal resource, bug?)!", UNKOWN_LINK_IDX, timeout, limit);
        }
        #ifdef MSGSRV_DEBUG_PHP
        php_printf(_LOGGER_FUNS "[%ld] - [Receive]Playload Receive Failed[Timeout:%ld, Limit:%ld](illegal resource, bug?)!\n", UNKOWN_LINK_IDX, timeout, limit);
        #endif

        php_error(E_WARNING, "Playload Receive Failed[Timeout:%ld, Limit:%ld](illegal resource, bug?)!", timeout, limit);

        FREE_RESOURCE(link_res);
        return FAILURE;
    }

    // set receive start time...
    gettimeofday(&start, NULL);

    /* try to receive data */
    do {
        // keep read until timeout or max buffer size or reach the end of line '\n'.
        gettimeofday(&now, NULL);
        if ((now.tv_sec - start.tv_sec) >= timeout) {   /* receive timeout... */
            if (MSGSRV_SG(trace_mode)) {
                syslog(LOG_ERR, _LOGGER_FUNS "[%ld] - [Receive]Playload Receive Timeout(Start: %lds, Now: %lds, Timeout: %lds, Received: %d, Limit: %ld).", conn->socket->idx, start.tv_sec, now.tv_sec, timeout, received, limit);
            }
            #ifdef MSGSRV_DEBUG_PHP
            php_printf(_LOGGER_FUNS "[%ld] - [Receive]Playload Receive Timeout(Start: %lds, Now: %lds, Timeout: %lds, Received: %d, Limit: %ld).\n", conn->socket->idx, start.tv_sec, now.tv_sec, timeout, received, limit);
            #endif

            php_error(E_WARNING, "Playload Receive Timeout(Start: %lds, Now: %lds, Timeout: %lds, Received: %d, Limit: %ld).", start.tv_sec, now.tv_sec, timeout, received, limit);

            for (_i = received; _i < limit; ++_i) {
                MSGSRV_RECEIVE_SAFE_CALLBACK(fci, fcc, MSGSRV_STATUS_TIMEOUT, NULL, NULL, NULL, link_res, NO, conn->socket->idx);
            }
            
            conn->error = MSGSRV_ERROR_READ_TIMEOUT;
            conn->socket->status = MSGSRV_SOCKET_STATUS_FINISH;
            return FAILURE;
        }
        // Check Link Status...
        if (conn->socket->status == MSGSRV_SOCKET_STATUS_FINISH) {
            if (MSGSRV_SG(trace_mode)) {
                syslog(LOG_ERR, _LOGGER_FUNS "[%ld] - [Receive]Playload Receive Failed[Timeout:%ld, Limit:%ld](link error, get another link)!", conn->socket->idx, timeout, limit);
            }
            #ifdef MSGSRV_DEBUG_PHP
            php_printf(_LOGGER_FUNS "[%ld] - [Receive]Playload Receive Failed[Timeout:%ld, Limit:%ld](link error, get another link)!\n", conn->socket->idx, timeout, limit);
            #endif

            php_error(E_WARNING, "Playload Receive Failed[Timeout:%ld, Limit:%ld](link error, get another link)!", timeout, limit);

            for (_i = received; _i < limit; ++_i) {
                MSGSRV_RECEIVE_SAFE_CALLBACK(fci, fcc, MSGSRV_SOCKET_STATUS_DISCONNECTED, NULL, NULL, NULL, link_res, NO, conn->socket->idx);
            }
            
            conn->error = MSGSRV_ERROR_DISCONNECT;
            return FAILURE;
        }
        /* #1: read from socket */
        ret = msgsrv_socket_read(conn->socket, &playload, timeout, MSGSRV_SG(trace_mode) TSRMLS_CC);
        switch (ret) {
            case 0: // link closed!
                if (MSGSRV_SG(trace_mode)) {
                    syslog(LOG_ERR, _LOGGER_FUNS "[%ld] - [Receive]Playload Receive Failed(lose connection while watching the playload)!", conn->socket->idx);
                }
                #ifdef MSGSRV_DEBUG_PHP
                php_printf(_LOGGER_FUNS "[%ld] - [Receive]Playload Receive Failed(lose connection while watching the playload)!\n", conn->socket->idx);
                #endif
                php_error(E_WARNING, "Playload Receive Failed(lose connection while watching the playload)!");

                conn->error = MSGSRV_ERROR_DISCONNECT;

                MSGSRV_RECEIVE_SAFE_CALLBACK(fci, fcc, MSGSRV_STATUS_DISCONNECT, NULL, NULL, NULL, link_res, YES, conn->socket->idx);

            case -9:    // timeout
                continue;
            case -1:
            case -2:    // read error close link!
                if (MSGSRV_SG(trace_mode)) {
                    syslog(LOG_ERR, _LOGGER_FUNS "[%ld] - [Receive]Playload Receive Failed(read error while watching the playload)!", conn->socket->idx);
                }
                #ifdef MSGSRV_DEBUG_PHP
                php_printf(_LOGGER_FUNS "[%ld] - [Receive]Playload Receive Failed(read error while watching the playload)!\n", conn->socket->idx);
                #endif
                php_error(E_WARNING, "Playload Receive Failed(read error while watching the playload)!");

                conn->error = MSGSRV_ERROR_READ_ERROR;
                conn->socket->status = MSGSRV_SOCKET_STATUS_FINISH;

                MSGSRV_RECEIVE_SAFE_CALLBACK(fci, fcc, MSGSRV_STATUS_READ_ERROR, NULL, NULL, NULL, link_res, YES, conn->socket->idx);

            default:
                if (ret < 1) {  // unkown error!
                    if (MSGSRV_SG(trace_mode)) {
                        syslog(LOG_ERR, _LOGGER_FUNS "[%ld] - [Receive]Playload Receive Failed(unkown error:%d while watching the playload)!", conn->socket->idx, ret);
                    }
                    #ifdef MSGSRV_DEBUG_PHP
                    php_printf(_LOGGER_FUNS "[%ld] - [Receive]Playload Receive Failed(unkown error:%d while watching the playload)!\n", conn->socket->idx, ret);
                    #endif
                    php_error(E_WARNING, "Playload Receive Failed(unkown error:%d while watching the playload)!", ret);

                    conn->error = MSGSRV_ERROR_READ_ERROR;
                    conn->socket->status = MSGSRV_SOCKET_STATUS_FINISH;
                    
                    MSGSRV_RECEIVE_SAFE_CALLBACK(fci, fcc, MSGSRV_STATUS_UNKOWN, NULL, NULL, NULL, link_res, YES, conn->socket->idx);
                }
                break;
        }

        if (MSGSRV_SG(trace_mode)) {
            syslog(LOG_INFO, _LOGGER_FUNS "[%ld] - [Receive]Playload Received => [%s]", conn->socket->idx, playload);
        }
        #ifdef MSGSRV_DEBUG_PHP
        php_printf(_LOGGER_FUNS "[%ld] - [Receive]Playload Received => [%s]\n", conn->socket->idx, playload);
        #endif

        /* #2: playload reached! */
        received ++;
        ret = msgsrv_playload_parser(playload, &_from, &_cmd, &_body);
        switch(ret) {
            case MSGSRV_PLAYLOAD_MINI:
            case MSGSRV_PLAYLOAD_DEFAULT:
                break;

            case MSGSRV_PLAYLOAD_ERROR:   // error playload
            default:    // unkown error
                if (MSGSRV_SG(trace_mode)) {
                    syslog(LOG_ERR, _LOGGER_FUNS "[%ld] - [Receive]Playload Receive Error, Illegal Replay Playload => [%s]", conn->socket->idx, playload);
                }
                #ifdef MSGSRV_DEBUG_PHP
                php_printf(_LOGGER_FUNS "[%ld] - [Receive]Playload Receive Error, Illegal Replay Playload => [%s]\n", conn->socket->idx, playload);
                #endif
                php_error(E_WARNING, "Playload Receive Error, Illegal Replay Playload => [%s].", playload);
                efree(playload);

                conn->error = MSGSRV_ERROR_PLAYLOAD;
                conn->socket->status = MSGSRV_SOCKET_STATUS_FINISH;
                MSGSRV_RECEIVE_SAFE_CALLBACK(fci, fcc, MSGSRV_STATUS_ERROR_PLAYLOAD, NULL, NULL, NULL, link_res, YES, conn->socket->idx);
                continue;
        }
        /* #3: detect msgsrv error */
        if (strcmp(LOCAL_MSGSRV_APPNAME, _from) == 0 && strcmp(MSGSRV_CMD_ERROR, _cmd) == 0) {  // msgsrv error detected
            failed ++;
            if (strcspn(_body, MSGSRV_ERROR_FLAG_APPLICATION) == 0) {   // . ERROR APPLICATION
                if (MSGSRV_SG(trace_mode)) {
                    syslog(LOG_ERR, _LOGGER_FUNS "[%ld] - [Receive]Playload Receive Error, Target Application Not Found(%s).", conn->socket->idx, _body);
                }
                #ifdef MSGSRV_DEBUG_PHP
                php_printf(_LOGGER_FUNS "[%ld] - [Receive]Playload Receive Error, Target Application Not Found(%s).\n", conn->socket->idx, _body);
                #endif
                php_error(E_WARNING, "Playload Receive Error, Target Application Not Found(%s).", _body);
                
                MSGSRV_RECEIVE_SAFE_CALLBACK(fci, fcc, MSGSRV_STATUS_ERROR_APPLICATION, _from, _cmd, _body, link_res, NO, conn->socket->idx);
            } else {
                if (MSGSRV_SG(trace_mode)) {
                    syslog(LOG_ERR, _LOGGER_FUNS "[%ld] - [Receive]Playload Receive Error, Unkown Error(%s).", conn->socket->idx, _body);
                }
                #ifdef MSGSRV_DEBUG_PHP
                php_printf(_LOGGER_FUNS "[%ld] - [Receive]Playload Receive Error, Unkown Error(%s).\n", conn->socket->idx, _body);
                #endif
                php_error(E_WARNING, "Playload Receive Error, Unkown Error(%s).", _body);

                MSGSRV_RECEIVE_SAFE_CALLBACK(fci, fcc, MSGSRV_STATUS_UNKOWN, _from, _cmd, _body, link_res, NO, conn->socket->idx);
            }
        } else {
            MSGSRV_RECEIVE_SAFE_CALLBACK(fci, fcc, MSGSRV_STATUS_OK, _from, _cmd, _body, link_res, NO, conn->socket->idx);
        }
        efree(playload);
    } while (received < limit);

    return failed > 0 ? FAILURE : SUCCESS;
}
/* }}} */


/* {{{ php_msgsrv_do_receive
 */
static int php_msgsrv_do_receive(INTERNAL_FUNCTION_PARAMETERS TSRMLS_DC) {
    zend_fcall_info fci = empty_fcall_info;
    zend_fcall_info_cache fcc = empty_fcall_info_cache;
    zval * link_res = NULL;
    long timeout = MSGSRV_SG(request_timeout);
    long limit = 1;

    if (zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC, "f|l|l|r", &fci, &fcc, &timeout, &limit, &link_res) == FAILURE) {
        return FAILURE;
    }
    // fix timeout
    if (timeout < 1) timeout = MSGSRV_SG(request_timeout);

    if (link_res == NULL) { /* try to receive from any link in curren page */
        return _php_msgsrv_do_receive_any(fci, fcc, timeout, limit TSRMLS_CC);
    } else {    /* try to receive from special link resource */
        return _php_msgsrv_do_receive_link(fci, fcc, link_res, timeout, limit, INTERNAL_FUNCTION_PARAM_PASSTHRU TSRMLS_CC);
    }
}
/* }}} */


/* {{{ php_msgsrv_do_request
 */
static int php_msgsrv_do_request(INTERNAL_FUNCTION_PARAMETERS TSRMLS_DC) {
    php_msgsrv_conn *conn = NULL;
    MsgSrvSocket *msgsrv_socket = NULL;
    char *playload = NULL, *reply = NULL;
    int ret;
    char *_from = NULL, *_cmd = NULL, *_body = NULL;    // replay's

    zval * link_res = NULL;
    const char * target = NULL;
    int target_len = 0;
    const char * cmd = NULL;
    int cmd_len = 0;
    const char * body = NULL;
    int body_len = 0;
    long timeout = MSGSRV_SG(request_timeout);   // in seconds

    if (zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC, "sssr|l", &target, &target_len, &cmd, &cmd_len, &body, &body_len, &link_res, &timeout) == FAILURE) {
        return FAILURE;
    }
    /* Get php_msgsrv_conn from resources */
    ZEND_FETCH_RESOURCE2(conn, php_msgsrv_conn *, &link_res, -1, NULL, msgsrv_link, msgsrv_plink);
    if (conn == NULL) {
        if (MSGSRV_SG(trace_mode)) {
            syslog(LOG_ERR, _LOGGER_FUNS "[%ld] - [Request]Playload(%s %s %s) Failed(connection not found, closed?)!", UNKOWN_LINK_IDX, target, cmd, body);
        }
        #ifdef MSGSRV_DEBUG_PHP
        php_printf(_LOGGER_FUNS "[%ld] - [Request]Playload(%s %s %s) Failed(connection not found, closed?)!\n", UNKOWN_LINK_IDX, target, cmd, body);
        #endif

        php_error(E_WARNING, "Playload(%s %s %s) Failed(connection not found, closed?)!", target, cmd, body);
        FREE_RESOURCE(link_res);
        return FAILURE;
    }
    if (conn->socket == NULL) {
        if (MSGSRV_SG(trace_mode)) {
            syslog(LOG_ERR, _LOGGER_FUNS "[%ld] - [Request]Playload(%s %s %s) Failed(illegal resource, bug?)!", UNKOWN_LINK_IDX, target, cmd, body);
        }
        #ifdef MSGSRV_DEBUG_PHP
        php_printf(_LOGGER_FUNS "[%ld] - [Request]Playload(%s %s %s) Failed(illegal resource, bug?)!\n", UNKOWN_LINK_IDX, target, cmd, body);
        #endif

        php_error(E_WARNING, "Playload(%s %s %s) Request Failed(illegal resource, bug?)!", target, cmd, body);
        FREE_RESOURCE(link_res);
        return FAILURE;
    }

    if (conn->socket->status == MSGSRV_SOCKET_STATUS_FINISH) {
        if (MSGSRV_SG(trace_mode)) {
            syslog(LOG_ERR, _LOGGER_FUNS "[%ld] - [Request]Playload(%s %s %s) Refused(link error, get another link)!", UNKOWN_LINK_IDX, target, cmd, body);
        }
        #ifdef MSGSRV_DEBUG_PHP
        php_printf(_LOGGER_FUNS "[%ld] - [Request]Playload(%s %s %s) Refused(link error, get another link)!\n", UNKOWN_LINK_IDX, target, cmd, body);
        #endif

        php_error(E_WARNING, "Playload(%s %s %s) Refused(link error, get another link)!", target, cmd, body);

        conn->error = MSGSRV_ERROR_DISCONNECT;
        // FREE_RESOURCE(link_res);
        return FAILURE;
    }

    /* Get socket connection */
    msgsrv_socket = conn->socket;

    /* #0: packet playload */
    if (msgsrv_playload_builder(&playload, target, cmd, body) == 0) {
        if (MSGSRV_SG(trace_mode)) {
            syslog(LOG_ERR, _LOGGER_FUNS "[%ld] - [Request]Playload(%s %s %s) Failed(packet failed, bug?)!", msgsrv_socket->idx, target, cmd, body);
        }
        #ifdef MSGSRV_DEBUG_PHP
        php_printf(_LOGGER_FUNS "[%ld] - [Request]Playload(%s %s %s) Failed(packet failed, bug?)!\n", msgsrv_socket->idx, target, cmd, body);
        #endif

        php_error(E_WARNING, "Playload(%s %s %s) Failed(packet failed, bug?)!", target, cmd, body);
        return FAILURE;
    }
    if (MSGSRV_SG(trace_mode)) {
        syslog(LOG_INFO, _LOGGER_FUNS "[%ld] - [Request]Playload => [%s]...", msgsrv_socket->idx, playload);
    }
    #ifdef MSGSRV_DEBUG_PHP
    php_printf(_LOGGER_FUNS "[%ld] - [Request]Playload => [%s]...\n", msgsrv_socket->idx, playload);
    #endif

    /* #1: send our request playload */
    if (msgsrv_socket_write(msgsrv_socket, playload, MSGSRV_SG(trace_mode) TSRMLS_CC) != 0) {
        if (MSGSRV_SG(trace_mode)) {
            syslog(LOG_ERR, _LOGGER_FUNS "[%ld] - [Request]Playload => [%s] Failed(stream write failed, link down?)!", msgsrv_socket->idx, playload);
        }
        #ifdef MSGSRV_DEBUG_PHP
        php_printf(_LOGGER_FUNS "[%ld] - [Request]Playload => [%s] Failed(stream write failed, link down?)!\n", msgsrv_socket->idx, playload);
        #endif
        php_error(E_WARNING, "Playload(%s) Request Failed(stream write failed, link down?)!", playload);

        conn->error = MSGSRV_ERROR_WRITE_FAILED;
        conn->socket->status = MSGSRV_SOCKET_STATUS_FINISH;
        efree(playload);
        return FAILURE;
    }
    /*
    if (MSGSRV_SG(trace_mode)) {
        syslog(LOG_ERR, _LOGGER_FUNS "[%ld] - [Request]Playload => [%s] Sent!", msgsrv_socket->idx, playload);
    }
    #ifdef MSGSRV_DEBUG_PHP
    php_printf(_LOGGER_FUNS "[%ld] - [Request]Playload => [%s] Sent!\n", msgsrv_socket->idx, playload);
    #endif
    */

    /* #2: waite response in timeout */
    ret = msgsrv_socket_read(msgsrv_socket, &reply, timeout, MSGSRV_SG(trace_mode) TSRMLS_CC);
    switch (ret) {
        case 0: // link closed!
            if (MSGSRV_SG(trace_mode)) {
                syslog(LOG_ERR, _LOGGER_FUNS "[%ld] - [Request]Playload => [%s] Failed(lose connection while watching the response)!", msgsrv_socket->idx, playload);
            }
            #ifdef MSGSRV_DEBUG_PHP
            php_printf(_LOGGER_FUNS "[%ld] - [Request]Playload => [%s] Failed(lose connection while watching the response)!\n", msgsrv_socket->idx, playload);
            #endif
            php_error(E_WARNING, "Playload(%s) Failed(lose connection while watching the response)!", playload);

            conn->error = MSGSRV_ERROR_DISCONNECT;
            efree(playload);
            return FAILURE;

        case -9:    // timeout
            if (MSGSRV_SG(trace_mode)) {
                syslog(LOG_ERR, _LOGGER_FUNS "[%ld] - [Request]Playload => [%s] Timeout[%ld](timeout while watching the response)!", msgsrv_socket->idx, playload, timeout);
            }
            #ifdef MSGSRV_DEBUG_PHP
            php_printf(_LOGGER_FUNS "[%ld] - [Request]Playload => [%s] Timeout[%ld](timeout while watching the response)!\n", msgsrv_socket->idx, playload, timeout);
            #endif
            php_error(E_WARNING, "Playload(%s) Timeout[%ld](timeout while watching the response)!", playload, timeout);

            conn->error = MSGSRV_ERROR_READ_TIMEOUT;
            conn->socket->status = MSGSRV_SOCKET_STATUS_FINISH;
            efree(playload);
            return FAILURE;

        case -1:
        case -2:    // read error close link!
            if (MSGSRV_SG(trace_mode)) {
                syslog(LOG_ERR, _LOGGER_FUNS "[%ld] - [Request]Playload => [%s] Failed(read error while watching the response)!", msgsrv_socket->idx, playload);
            }
            #ifdef MSGSRV_DEBUG_PHP
            php_printf(_LOGGER_FUNS "[%ld] - [Request]Playload => [%s] Failed(read error while watching the response)!\n", msgsrv_socket->idx, playload);
            #endif
            php_error(E_WARNING, "Playload(%s) Request Failed(connection closed while watching the response)!", playload);

            conn->error = MSGSRV_ERROR_READ_ERROR;
            conn->socket->status = MSGSRV_SOCKET_STATUS_FINISH;
            efree(playload);
            return FAILURE;

        default:
            if (ret < 1) {  // unkown error!
                if (MSGSRV_SG(trace_mode)) {
                    syslog(LOG_ERR, _LOGGER_FUNS "[%ld] - [Request]Playload => [%s] Failed(unkown error:%d while watching the response)!", msgsrv_socket->idx, playload, ret);
                }
                #ifdef MSGSRV_DEBUG_PHP
                php_printf(_LOGGER_FUNS "[%ld] - [Request]Playload => [%s] Failed(unkown error:%d while watching the response)!\n", msgsrv_socket->idx, playload, ret);
                #endif
                php_error(E_WARNING, "Playload(%s) Request Failed(unkown error:%d while watching the response)!", playload, ret);

                conn->error = MSGSRV_ERROR_READ_ERROR;
                conn->socket->status = MSGSRV_SOCKET_STATUS_FINISH;
                efree(playload);
                return FAILURE;
            }
            break;
    }
    /* #3: response reached! */
    ret = msgsrv_playload_parser(reply, &_from, &_cmd, &_body);
    switch(ret) {
        case MSGSRV_PLAYLOAD_MINI:
        case MSGSRV_PLAYLOAD_DEFAULT:
            break;

        case MSGSRV_PLAYLOAD_ERROR:   // error playload
        default:    // unkown error
            if (MSGSRV_SG(trace_mode)) {
                syslog(LOG_ERR, _LOGGER_FUNS "[%ld] - [Request]Playload => [%s] Illegal Replay Playload! Response => [%s]", msgsrv_socket->idx, playload, reply);
            }                
            #ifdef MSGSRV_DEBUG_PHP
            php_printf(_LOGGER_FUNS "[%ld] - [Request]Playload => [%s] Illegal Replay Playload! Response => [%s]\n", msgsrv_socket->idx, playload, reply);
            #endif
            php_error(E_WARNING, "Playload(%s) Request Failed(illegal reply[%s] received)!", playload, reply);

            efree(playload);
            efree(reply);
            return FAILURE;
    }
    /* #4: error response ? */
    if (strcmp(LOCAL_MSGSRV_APPNAME, target) == 0) {  // msgsrv request
        array_init(return_value);
        add_assoc_string(return_value, MSGSRV_PLAYLOAD_FROM, _from, TRUE);
        add_assoc_string(return_value, MSGSRV_PLAYLOAD_CMD, _cmd, TRUE);
        if (MSGSRV_PLAYLOAD_DEFAULT == ret) {
            add_assoc_string(return_value, MSGSRV_PLAYLOAD_BODY, _body, TRUE);
        }

        if (MSGSRV_SG(trace_mode)) {
            syslog(LOG_INFO, _LOGGER_FUNS "[%ld] - [Request]Playload => [%s] Successfully! Response => [%s %s %s]", msgsrv_socket->idx, playload, _from, _cmd, _body);
        }
        #ifdef MSGSRV_DEBUG_PHP
        php_printf(_LOGGER_FUNS "[%ld] - [Request]Playload => [%s] Successfully! Response => [%s %s %s]\n", msgsrv_socket->idx, playload, _from, _cmd, _body);
        #endif
        
        efree(playload);
        efree(reply);
        return SUCCESS;
    } else if (strcmp(LOCAL_MSGSRV_APPNAME, _from) == 0 && strcmp(MSGSRV_CMD_ERROR, _cmd) == 0) {  // msgsrv error detected
        if (strcspn(_body, MSGSRV_ERROR_FLAG_APPLICATION) == 0) {   // . ERROR APPLICATION
            if (MSGSRV_SG(trace_mode)) {
                syslog(LOG_NOTICE, _LOGGER_FUNS "[%ld] - [Request]Playload => [%s] Failed(target application:%s not found)!", msgsrv_socket->idx, playload, target);
            }
            #ifdef MSGSRV_DEBUG_PHP
            php_printf(_LOGGER_FUNS "[%ld] - [Request]Playload => [%s] Failed(target application:%s not found)!\n", msgsrv_socket->idx, playload, target);
            #endif
            php_error(E_WARNING, "Playload(%s) Request Failed(target application:%s not found)!", playload, target);

            conn->error = MSGSRV_ERROR_APPLICATION;
            efree(playload);
            efree(reply);
            return FAILURE;
        } else {
            if (MSGSRV_SG(trace_mode)) {
                syslog(LOG_ERR, _LOGGER_FUNS "[%ld] - [Request]Playload => [%s] Failed(unkown error:%s from msgsrv)!", msgsrv_socket->idx, playload, _body);
            }
            #ifdef MSGSRV_DEBUG_PHP
            php_printf(_LOGGER_FUNS "[%ld] - [Request]Playload => [%s] Failed(unkown error:%s from msgsrv)!\n", msgsrv_socket->idx, playload, _body);
            #endif
            php_error(E_WARNING, "Playload(%s) Request Failed(unkown error:%s from msgsrv)!", playload, _body);

            conn->error = MSGSRV_ERROR_UNKOWN;
            efree(playload);
            efree(reply);
            return FAILURE;
        }
    } else {
        array_init(return_value);
        add_assoc_string(return_value, MSGSRV_PLAYLOAD_FROM, _from, TRUE);
        add_assoc_string(return_value, MSGSRV_PLAYLOAD_CMD, _cmd, TRUE);
        if (MSGSRV_PLAYLOAD_DEFAULT == ret) {
            add_assoc_string(return_value, MSGSRV_PLAYLOAD_BODY, _body, TRUE);
        }

        if (MSGSRV_SG(trace_mode)) {
            syslog(LOG_INFO, _LOGGER_FUNS "[%ld] - [Request]Playload => [%s] Successfully! Response => [%s %s %s]", msgsrv_socket->idx, playload, _from, _cmd, _body);
        }
        #ifdef MSGSRV_DEBUG_PHP
        php_printf(_LOGGER_FUNS "[%ld] - [Request]Playload => [%s] Successfully! Response => [%s %s %s]\n", msgsrv_socket->idx, playload, _from, _cmd, _body);
        #endif

        efree(playload);
        efree(reply);
        return SUCCESS;
    }
}
/* }}} */


/* {{{ php_msgsrv_get_last_error
 */
static int php_msgsrv_get_last_error(INTERNAL_FUNCTION_PARAMETERS TSRMLS_DC) {
    zval * link_res = NULL;
    php_msgsrv_conn *conn = NULL;

    if (zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC, "r", &link_res) == FAILURE) {
        return FAILURE;
    }
    /* Get php_msgsrv_conn from resources */
    ZEND_FETCH_RESOURCE2(conn, php_msgsrv_conn *, &link_res, -1, NULL, msgsrv_link, msgsrv_plink);

    if (conn == NULL) {
        if (MSGSRV_SG(trace_mode)) {
            syslog(LOG_ERR, _LOGGER_FUNS "[%ld] - [LastError]Get Failed(connection not found, closed?)", UNKOWN_LINK_IDX);
        }
        #ifdef MSGSRV_DEBUG
        php_printf(_LOGGER_FUNS "[%ld] - [LastError]Get Failed(connection not found, closed?)\n", UNKOWN_LINK_IDX);
        #endif

        php_error(E_WARNING, "LastError Get Failed(connection not found, closed?)!");
        return FAILURE;
    }
    if (conn->socket == NULL) {
        if (MSGSRV_SG(trace_mode)) {
            syslog(LOG_ERR, _LOGGER_FUNS "[%ld] - [LastError]Get Failed(illegal resource, bug?)", UNKOWN_LINK_IDX);
        }
        #ifdef MSGSRV_DEBUG
        php_printf(_LOGGER_FUNS "[%ld] - [LastError]Get Failed(illegal resource, bug?)\n", UNKOWN_LINK_IDX);
        #endif

        FREE_RESOURCE(link_res);

        php_error(E_WARNING, "LastError Get Failed(illegal resource, bug?)!");
        return FAILURE;
    } else {
        if (MSGSRV_SG(trace_mode)) {
            syslog(LOG_INFO, _LOGGER_FUNS "[%ld] - [LastError]Get Successfully => [%ld]", conn->socket->idx, conn->error);
        }
        #ifdef MSGSRV_DEBUG
        php_printf(_LOGGER_FUNS "[%ld] - [LastError]Get Successfully => [%ld]\n", conn->socket->idx, conn->error);
        #endif

        RETVAL_LONG(conn->error);
        return SUCCESS;
    }
}
/* }}} */


/* {{{ php_msgsrv_get_full_app
 */
static int php_msgsrv_get_full_app(INTERNAL_FUNCTION_PARAMETERS TSRMLS_DC) {
    zval * link_res = NULL;
    php_msgsrv_conn *conn = NULL;

    if (zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC, "r", &link_res) == FAILURE) {
        return FAILURE;
    }
    /* Get php_msgsrv_conn from resources */
    ZEND_FETCH_RESOURCE2(conn, php_msgsrv_conn *, &link_res, -1, NULL, msgsrv_link, msgsrv_plink);

    if (conn == NULL) {
        if (MSGSRV_SG(trace_mode)) {
            syslog(LOG_ERR, _LOGGER_FUNS "[%ld] - [FullApp]Get Failed(connection not found, closed?)", UNKOWN_LINK_IDX);
        }
        #ifdef MSGSRV_DEBUG_PHP
        php_printf(_LOGGER_FUNS "[%ld] - [FullApp]Get Failed(connection not found, closed?)\n", UNKOWN_LINK_IDX);
        #endif

        php_error(E_WARNING, "FullApp Get Failed(connection not found, closed?)!");
        return FAILURE;
    }
    if (conn->socket == NULL) {
        if (MSGSRV_SG(trace_mode)) {
            syslog(LOG_ERR, _LOGGER_FUNS "[%ld] - [FullApp]Get Failed(illegal resource, bug?)", UNKOWN_LINK_IDX);
        }
        #ifdef MSGSRV_DEBUG_PHP
        php_printf(_LOGGER_FUNS "[%ld] - [FullApp]Get Failed(illegal resource, bug?)\n", UNKOWN_LINK_IDX);
        #endif

        FREE_RESOURCE(link_res);

        php_error(E_WARNING, "FullApp Get Failed(illegal resource, bug?)!");
        return FAILURE;
    } else {
        if (MSGSRV_SG(trace_mode)) {
            syslog(LOG_INFO, _LOGGER_FUNS "[%ld] - [FullApp]Get Successfully => %s", conn->socket->idx, conn->socket->full_app);
        }
        #ifdef MSGSRV_DEBUG_PHP
        php_printf(_LOGGER_FUNS "[%ld] - [FullApp]Get Successfully => %s\n", conn->socket->idx, conn->socket->full_app);
        #endif

        RETVAL_STRING(conn->socket->full_app, TRUE);
        return SUCCESS;
    }
}
 /* }}} */


/* {{{ php_msgsrv_do_msgsrv_close
 */
static int php_msgsrv_do_close(INTERNAL_FUNCTION_PARAMETERS TSRMLS_DC) {
    zval * link_res = NULL;
    php_msgsrv_conn *conn = NULL;
    zend_rsrc_list_entry *le;

    if (zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC, "r", &link_res) == FAILURE) {
        return FAILURE;
    }
    /* Get php_msgsrv_conn from resources */
    ZEND_FETCH_RESOURCE2(conn, php_msgsrv_conn *, &link_res, -1, NULL, msgsrv_link, msgsrv_plink);

    if (conn == NULL) {
        if (MSGSRV_SG(trace_mode)) {
            syslog(LOG_ERR, _LOGGER_FUNS "[%ld] - [Close]Connection not found, closed?", UNKOWN_LINK_IDX);
        }
        #ifdef MSGSRV_DEBUG_PHP
        php_printf(_LOGGER_FUNS "[%ld] - [Close]Connection not found, closed?\n", UNKOWN_LINK_IDX);
        #endif

        return FAILURE;
    }
    if (conn->socket == NULL) {
        if (MSGSRV_SG(trace_mode)) {
            syslog(LOG_ERR, _LOGGER_FUNS "[%ld] - [Close]Illegal resource, bug?!", UNKOWN_LINK_IDX);
        }
        #ifdef MSGSRV_DEBUG_PHP
        php_printf(_LOGGER_FUNS "[%ld] - [Close]Illegal resource, bug?\n", UNKOWN_LINK_IDX);
        #endif
        FREE_RESOURCE(link_res);

        return FAILURE;
    }
    if (conn->socket->persistent) {
        FREE_RESOURCE(link_res);
        return SUCCESS;
    } else {
        FREE_RESOURCE(link_res);
        return SUCCESS;
    }
}
/* }}} */


/* {{{ _close_msgsrv_link
 */
static void _close_msgsrv_link(zend_rsrc_list_entry *rsrc TSRMLS_DC)
{
    php_msgsrv_conn *link = (php_msgsrv_conn *)rsrc->ptr;

    if (link->socket != NULL) {
        if (link->socket->persistent) {
            if (link->socket->status == MSGSRV_SOCKET_STATUS_FINISH) {  // close this not reusable link
                if (MSGSRV_SG(trace_mode)) {
                    syslog(LOG_NOTICE, _LOGGER_SYS "[%ld] - Connection Error, Give Up MsgSrv-Link(%s)[Persistent][%s]...", link->socket->idx, link->socket->full_app, link->socket->hash_key);
                }
                #ifdef MSGSRV_DEBUG_PHP
                php_printf(_LOGGER_SYS "[%ld] - Connection Error, Give Up MsgSrv-Link(%s)[Persistent][%s]...\n", link->socket->idx, link->socket->full_app, link->socket->hash_key);
                #endif

                zend_hash_del(&EG(persistent_list), link->socket->hash_key, link->socket->hash_key_len + 1);
            } else {
                if (MSGSRV_SG(trace_mode)) {
                    syslog(LOG_DEBUG, _LOGGER_SYS "[%ld] - Give Back MsgSrv-Link(%s)[Persistent][%s]...", link->socket->idx, link->socket->full_app, link->socket->hash_key);
                }
                #ifdef MSGSRV_DEBUG_PHP
                php_printf(_LOGGER_SYS "[%ld] - Give Back MsgSrv-Link(%s)[Persistent][%s]...\n", link->socket->idx, link->socket->full_app, link->socket->hash_key);
                #endif

                link->socket->busy = NO;
                link->socket->refcount = 0;
            }
        } else {
            if (MSGSRV_SG(trace_mode)) {
                syslog(LOG_DEBUG, _LOGGER_SYS "[%ld] - Free MsgSrv-Link(%s)...", link->socket->idx, link->socket->full_app);
            }
            #ifdef MSGSRV_DEBUG_PHP
            php_printf(_LOGGER_SYS "[%ld] - Free MsgSrv-Link(%s)...\n", link->socket->idx, link->socket->full_app);
            #endif

            msgsrv_socket_disconnect(link->socket, MSGSRV_SG(trace_mode) TSRMLS_CC);
            msgsrv_free_socket(link->socket);
        }
        MSGSRV_SG(num_links)--;
    }
    if (link->id >= 0) {
        link->socket = NULL;
        zend_hash_index_del(MSGSRV_SG(page_links), link->id);
    } else {
        link->socket = NULL;
        efree(link);
    }
}
/* }}} */


/* {{{ _close_msgsrv_plink
 */
static void _close_msgsrv_plink(zend_rsrc_list_entry *rsrc TSRMLS_DC)
{
    php_msgsrv_conn *link = (php_msgsrv_conn *)rsrc->ptr;
    if (link->socket != NULL) {
        if (MSGSRV_SG(trace_mode)) {
            syslog(LOG_DEBUG, _LOGGER_SYS "[%ld] - Free MsgSrv-Link(%s)[Persistent]...", link->socket->idx, link->socket->full_app);
        }
        
        msgsrv_socket_disconnect(link->socket, MSGSRV_SG(trace_mode) TSRMLS_CC);
        msgsrv_free_socket(link->socket);

        MSGSRV_SG(num_persistent)--;
        MSGSRV_SG(num_links)--;
    }
    free(link);
}
/* }}} */

/* {{{ PHP_MINIT_FUNCTION */
PHP_MINIT_FUNCTION(msgsrv)
{
    openlog ("msgsrv", LOG_CONS | LOG_PID | LOG_NDELAY, LOG_LOCAL0);

    ZEND_INIT_MODULE_GLOBALS(msgsrv, php_msgsrv_minit_globals, php_msgsrv_mshutdown_globals)
    REGISTER_INI_ENTRIES();
    msgsrv_link = zend_register_list_destructors_ex(_close_msgsrv_link, NULL, PHP_MSGSRV_DESCRIPTOR_RES_NAME, module_number);
    msgsrv_plink = zend_register_list_destructors_ex(_close_msgsrv_link, _close_msgsrv_plink, PHP_MSGSRV_DESCRIPTOR_PERSISTENT_RES_NAME, module_number);
    Z_TYPE(msgsrv_module_entry) = type;

    /* Alloc Persistent Link Table */
    if (MSGSRV_SG(allow_persistent)) {
        ALLOC_HASHTABLE(MSGSRV_SG(persistent_links));
        if (zend_hash_init(MSGSRV_SG(persistent_links), PHP_MSGSRV_PAGE_PLINKS_INIT_SIZE, NULL, NULL, TRUE) == FAILURE) {
            FREE_HASHTABLE(MSGSRV_SG(persistent_links));
            return FAILURE;
        }
    }

    return SUCCESS;
}
/* }}} */



/* {{{ php_msgsrv_persistent_helper */
/*static int php_msgsrv_persistent_helper(zend_rsrc_list_entry *le TSRMLS_DC)
{
    if (le->type == msgsrv_plink) {
        php_printf("xxxx...\n");
        //mysqlnd_end_psession(((php_mysql_conn *) le->ptr)->conn);
    }
    return ZEND_HASH_APPLY_KEEP;
}*/
/* }}} */

/* {{{ PHP_MSHUTDOWN_FUNCTION */

PHP_MSHUTDOWN_FUNCTION(msgsrv)
{
    UNREGISTER_INI_ENTRIES();

    /* Free Persistent Link Table */
    if (MSGSRV_SG(allow_persistent)) {
        /* Bug? */
        //zend_hash_destroy(MSGSRV_SG(persistent_links));
        //FREE_HASHTABLE(MSGSRV_SG(persistent_links));
        //zend_hash_apply(MSGSRV_SG(persistent_links), (apply_func_t) php_msgsrv_persistent_helper TSRMLS_CC);
    }
    closelog ();
    return SUCCESS;
}
/* }}} */


/* {{{ PHP_RINIT_FUNCTION */
PHP_RINIT_FUNCTION(msgsrv)
{
    MSGSRV_SG(num_links) = MSGSRV_SG(num_persistent);
    #ifdef MSGSRV_DEBUG
    MSGSRV_SG(trace_mode) = YES;
    #endif

    /* Alloc Page Link Table */
    ALLOC_HASHTABLE(MSGSRV_SG(page_links));
    if (zend_hash_init(MSGSRV_SG(page_links), PHP_MSGSRV_PAGE_LINKS_INIT_SIZE, NULL, NULL, FALSE) == FAILURE) {
        FREE_HASHTABLE(MSGSRV_SG(page_links));
        return FAILURE;
    }
    
    return SUCCESS;
}
/* }}} */


/* {{{ PHP_RSHUTDOWN_FUNCTION */
PHP_RSHUTDOWN_FUNCTION(msgsrv)
{
    /* Free Page Link Table */
    FREE_HASHTABLE(MSGSRV_SG(page_links));
    return SUCCESS;
}
/* }}} */


/* {{{ PHP_MINFO_FUNCTION */
PHP_MINFO_FUNCTION(msgsrv)
{
    char buf[32];
    php_msgsrv_conn *conn = NULL;
    list_entry  *le;
    char *key = NULL;
    long index = -1;
    zend_bool duplicate;

    duplicate = 0;
    php_info_print_table_start();
    php_info_print_table_header(2, "MsgSrv Support", "enabled");
    php_info_print_table_row(2, "Version", PHP_MSGSRV_VERSION);
    php_info_print_table_row(2, "Released", PHP_MSGSRV_RELEASED);
    php_info_print_table_row(2, "Authors", PHP_MSGSRV_AUTHORS);

    snprintf(buf, sizeof(buf), "%ld", MSGSRV_SG(num_persistent));
    php_info_print_table_row(2, "Active Persistent Links", buf);
    snprintf(buf, sizeof(buf), "%ld", MSGSRV_SG(num_links));
    php_info_print_table_row(2, "Active Links", buf);
    php_info_print_table_end();
    
    DISPLAY_INI_ENTRIES();

    if (MSGSRV_SG(allow_persistent)) {
        php_info_print_table_start();

        snprintf(buf, sizeof(buf), "Total: %ld", MSGSRV_SG(num_persistent));
        php_info_print_table_header(2, "MsgSrv Persistent Pool Links", buf);
        syslog(LOG_INFO, _LOGGER_FUNS "####################[MsgSrv Persistent Pool Links]####################");
        for(zend_hash_internal_pointer_reset(&EG(persistent_list));
            zend_hash_has_more_elements(&EG(persistent_list)) == SUCCESS; 
            zend_hash_move_forward(&EG(persistent_list))) {
            // #0:get msgsrv link hash key
            if (zend_hash_get_current_key(&EG(persistent_list), &key, &index, duplicate) == FAILURE) {
                // Should never actually fail, since the key is known to exist.
                continue;
            }
            // #0:get msgsrv link
            if (zend_hash_get_current_data(&EG(persistent_list), (void**)&le) == FAILURE) {
                // Should never actually fail, since the key is known to exist.
                continue;
            }
            conn = (php_msgsrv_conn *) le->ptr;
            if (conn->socket->full_app == NULL) continue;

            syslog(LOG_ERR, _LOGGER_FUNS "%ld => %s\n", conn->socket->idx, conn->socket->full_app);
            snprintf(buf, sizeof(buf), "%ld", conn->socket->idx);

            php_info_print_table_row(2, buf, conn->socket->full_app);
        }
        syslog(LOG_INFO, _LOGGER_FUNS "######################################################################");
        php_info_print_table_end();
    }

}
/* }}} */


/* {{{ proto resource|bool msgsrv_open(string host, int port, string app, string user, string pass)
   * Compatible with v1.0, open a none-persistent connection, just same as msgsrv_connect.
   */
PHP_FUNCTION(msgsrv_open)
{
#ifdef ZTS
    /* not sure how in threaded mode this works so disabled persistents at first */
    if (php_msgsrv_do_connect(INTERNAL_FUNCTION_PARAM_PASSTHRU, FALSE TSRMLS_CC) == FAILURE) {
        RETURN_FALSE;
    }
#else
    long persistent = FALSE;
    char *nil = NULL;
    int  nil_len;
    long port = 0;

    if (zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC, "slsss|l", &nil, &nil_len, &port, &nil, &nil_len, &nil, &nil_len, &nil, &nil_len, &persistent) == FAILURE) {
        RETURN_FALSE;
    }
    if (php_msgsrv_do_connect(INTERNAL_FUNCTION_PARAM_PASSTHRU, persistent ? TRUE : FALSE TSRMLS_CC) == FAILURE) {
        RETURN_FALSE;
    }
#endif    
}
/* }}} msgsrv_open */


/* {{{ proto resource|bool msgsrv_connect(string host, int port, string app, string user, string pass)
   */
PHP_FUNCTION(msgsrv_connect)
{
    if (php_msgsrv_do_connect(INTERNAL_FUNCTION_PARAM_PASSTHRU, FALSE TSRMLS_CC) == FAILURE) {
        RETURN_FALSE;
    }
}
/* }}} msgsrv_connect */


/* {{{ proto resource|bool msgsrv_pconnect(string host, int port, string app, string user, string pass)
   */
PHP_FUNCTION(msgsrv_pconnect)
{
    if (php_msgsrv_do_connect(INTERNAL_FUNCTION_PARAM_PASSTHRU, TRUE TSRMLS_CC) == FAILURE) {
        RETURN_FALSE;
    }
}
/* }}} msgsrv_pconnect */


/* {{{ proto bool msgsrv_send(string target, string cmd, string content, resource link)
   */
PHP_FUNCTION(msgsrv_send)
{
    if (php_msgsrv_do_send(INTERNAL_FUNCTION_PARAM_PASSTHRU TSRMLS_CC) == FAILURE) {
        RETURN_FALSE;
    } else {
        RETURN_TRUE;
    }
}
/* }}} msgsrv_send */


/* {{{ proto bool msgsrv_receive(callback callback[, int timeout[, resource link]])
   */
PHP_FUNCTION(msgsrv_receive)
{
    if (php_msgsrv_do_receive(INTERNAL_FUNCTION_PARAM_PASSTHRU TSRMLS_CC) == FAILURE) {
        RETURN_FALSE;
    } else {
        RETURN_TRUE;
    }
}
/* }}} msgsrv_receive */


/* {{{ proto array msgsrv_request(string target, string cmd, string content, resource link[, int timeout])
   */
PHP_FUNCTION(msgsrv_request)
{
    if (php_msgsrv_do_request(INTERNAL_FUNCTION_PARAM_PASSTHRU TSRMLS_CC) == FAILURE) {
        RETURN_FALSE;
    }
}
/* }}} msgsrv_request */


/* {{{ proto bool msgsrv_last_error(resource link)
   */
PHP_FUNCTION(msgsrv_last_error)
{
    if (php_msgsrv_get_last_error(INTERNAL_FUNCTION_PARAM_PASSTHRU TSRMLS_CC) == FAILURE) {
        RETURN_FALSE;
    }
}
/* }}} msgsrv_last_error */


/* {{{ proto bool msgsrv_full_app(resource link)
   */
PHP_FUNCTION(msgsrv_full_app)
{
    if (php_msgsrv_get_full_app(INTERNAL_FUNCTION_PARAM_PASSTHRU TSRMLS_CC) == FAILURE) {
        RETURN_FALSE;
    }
}
/* }}} msgsrv_full_app */


/* {{{ proto bool msgsrv_close(resource link)
   */
PHP_FUNCTION(msgsrv_close)
{
    if (php_msgsrv_do_close(INTERNAL_FUNCTION_PARAM_PASSTHRU TSRMLS_CC) == FAILURE) {
        RETURN_FALSE;
    } else {
        RETURN_TRUE;
    }
}
/* }}} msgsrv_close */

#endif /* HAVE_MSGSRV */


/*
 * Local variables:
 * tab-width: 4
 * c-basic-offset: 4
 * End:
 * vim600: noet sw=4 ts=4 fdm=marker
 * vim<600: noet sw=4 ts=4
 */
