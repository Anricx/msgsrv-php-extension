/*
 +----------------------------------------------------------------------+
 | ChinaRoad license:                                                      |
 +----------------------------------------------------------------------+
 | Authors: Deng Tao <dengt@007ka.com>                                  |
 +----------------------------------------------------------------------+
 */

/* $ Id: $ */

#include "php_msgsrv.h"
#include "syslog.h"
#include "md5lib.h"

#if HAVE_MSGSRV

/* True globals, no need for thread safety */
static int msgsrv_link;

#include "msgsrv_constants.h"

#ifdef PHP_WIN32
# include "win32_socket.c"
#else
# include "unix_socket.c"
#endif

typedef struct _php_msgsrv_link {
    int sockfd;
    char *phy_addr;
    zval *res;
}php_msgsrv_link;

typedef struct _php_msgsrv_msg {
    char *appname;
    char *command;
    char *content;
}php_msgsrv_msg;

#include "msgsrv_functions.c"

#if (PHP_MAJOR_VERSION == 5 && PHP_MINOR_VERSION < 3)
const zend_fcall_info empty_fcall_info = { 0, NULL, NULL, NULL, NULL, 0, NULL, NULL, 0 };
#undef ZEND_BEGIN_ARG_INFO_EX
#define ZEND_BEGIN_ARG_INFO_EX(name, pass_rest_by_reference, return_reference, required_num_args) \
    static zend_arg_info name[] = { \
        { NULL, 0, NULL, 0, 0, 0, pass_rest_by_reference, return_reference, required_num_args },
#endif

#if (PHP_MAJOR_VERSION == 5) && (PHP_MINOR_VERSION < 3)
#define zend_parse_parameters_none()                                        \
        zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC, "")
#endif

/* {{{ msgsrv_functions[] */
zend_function_entry msgsrv_functions[] = {
    PHP_FE(msgsrv_open , msgsrv_open_arg_info)
    PHP_FE(msgsrv_phy_addr     , msgsrv_phy_addr_arg_info)
    PHP_FE(msgsrv_send , msgsrv_send_arg_info)
    PHP_FE(msgsrv_receive , msgsrv_receive_arg_info)
    PHP_FE(msgsrv_request , msgsrv_request_arg_info)
    PHP_FE(msgsrv_last_error , msgsrv_last_error_arg_info)
    PHP_FE(msgsrv_close , msgsrv_close_arg_info)
    {   NULL, NULL, NULL}
};
/* }}} */

/* {{{ msgsrv_module_entry
 */
zend_module_entry msgsrv_module_entry = {
    STANDARD_MODULE_HEADER,
    "msgsrv",
    msgsrv_functions,
    PHP_MINIT(msgsrv), /* Replace with NULL if there is nothing to do at php startup   */
    PHP_MSHUTDOWN(msgsrv), /* Replace with NULL if there is nothing to do at php shutdown  */
    PHP_RINIT(msgsrv), /* Replace with NULL if there is nothing to do at request start */
    PHP_RSHUTDOWN(msgsrv), /* Replace with NULL if there is nothing to do at request end   */
    PHP_MINFO(msgsrv),
    PHP_MSGSRV_VERSION,
    STANDARD_MODULE_PROPERTIES
};
/* }}} */

#ifdef COMPILE_DL_MSGSRV
ZEND_GET_MODULE(msgsrv)
#endif

/* {{{ globals and ini entries */
ZEND_DECLARE_MODULE_GLOBALS(msgsrv)

#ifndef ZEND_ENGINE_2
#define OnUpdateLong OnUpdateInt
#endif
PHP_INI_BEGIN()
STD_PHP_INI_ENTRY("msgsrv.debug", "0", PHP_INI_SYSTEM, OnUpdateBool, debug, zend_msgsrv_globals, msgsrv_globals)
STD_PHP_INI_ENTRY("msgsrv.read_buffer_size", "1024", PHP_INI_SYSTEM, OnUpdateLong, read_buffer_size, zend_msgsrv_globals, msgsrv_globals)
STD_PHP_INI_ENTRY("msgsrv.request_timeout", "30000", PHP_INI_SYSTEM, OnUpdateLong, request_timeout, zend_msgsrv_globals, msgsrv_globals)
STD_PHP_INI_ENTRY("msgsrv.read_timeout", "30000", PHP_INI_SYSTEM, OnUpdateLong, read_timeout, zend_msgsrv_globals, msgsrv_globals)
STD_PHP_INI_ENTRY("msgsrv.select_timeout", "1000", PHP_INI_SYSTEM, OnUpdateLong, select_timeout, zend_msgsrv_globals, msgsrv_globals)
PHP_INI_END()

static void php_msgsrv_minit_globals(zend_msgsrv_globals *msgsrv_globals) {
    msgsrv_globals->debug = 0;
    msgsrv_globals->read_buffer_size = 0;
    msgsrv_globals->request_timeout = 0;
    msgsrv_globals->read_timeout = 0;
    msgsrv_globals->select_timeout = 0;
}

static void php_msgsrv_mshutdown_globals(zend_msgsrv_globals *msgsrv_globals) {

}

static int php_msgsrv_rinit_globals() {
    // init connection table
    ALLOC_HASHTABLE(MSGSRV_G(link_table));
    if (zend_hash_init(MSGSRV_G(link_table), 32, NULL, NULL, 0) == FAILURE) {
        FREE_HASHTABLE(MSGSRV_G(link_table));
        return FAILURE;
    }
    return SUCCESS;
}

static int php_msgsrv_rshutdown_globals() {
    FREE_HASHTABLE(MSGSRV_G(link_table));
    return SUCCESS;
}/* }}} */

/* {{{ _close_msgsrv_link */
static void _close_msgsrv_link(zend_rsrc_list_entry *rsrc TSRMLS_DC) {
    php_msgsrv_link *link = (php_msgsrv_link*)rsrc->ptr;
    close(link->sockfd); // close msgsrv connection
    if (MSGSRV_G(debug)) syslog(LOG_INFO, "[MsgSrv][C][I][%d] - Connection closed! [%s]", link->sockfd, link->phy_addr);
}/* }}} */

/* {{{ _msgsrv_do_callback */
static int _msgsrv_do_callback(zend_fcall_info fci, zend_fcall_info_cache fcc, zval ***params, int param_count) {
    if (!ZEND_FCI_INITIALIZED(fci)) {
        return FAILURE;
    }
    zval *retval_ptr = NULL;
    fci.params = params;
    fci.retval_ptr_ptr = &retval_ptr;
    fci.param_count = param_count;
    fci.no_separation = 1;
    if (zend_call_function(&fci, &fcc TSRMLS_CC) != SUCCESS) return FAILURE;
    if (retval_ptr != NULL) zval_ptr_dtor(&retval_ptr);
    return SUCCESS;
}
/* }}} */

/* {{{ PHP_MINIT_FUNCTION */
PHP_MINIT_FUNCTION(msgsrv) {
    ZEND_INIT_MODULE_GLOBALS(msgsrv, php_msgsrv_minit_globals, php_msgsrv_mshutdown_globals)
    REGISTER_INI_ENTRIES();
    msgsrv_link = zend_register_list_destructors_ex(_close_msgsrv_link, NULL, PHP_MSGSRV_DESCRIPTOR_RES_NAME, module_number);
    return SUCCESS;
}
/* }}} */

/* {{{ PHP_MSHUTDOWN_FUNCTION */
PHP_MSHUTDOWN_FUNCTION(msgsrv) {
    UNREGISTER_INI_ENTRIES();
    return SUCCESS;
}
/* }}} */

/* {{{ PHP_RINIT_FUNCTION */
PHP_RINIT_FUNCTION(msgsrv) {
    return php_msgsrv_rinit_globals();
}
/* }}} */

/* {{{ PHP_RSHUTDOWN_FUNCTION */
PHP_RSHUTDOWN_FUNCTION(msgsrv) {
    return php_msgsrv_rshutdown_globals();
}
/* }}} */

/* {{{ PHP_MINFO_FUNCTION */
PHP_MINFO_FUNCTION(msgsrv)
{
    php_info_print_table_start();
    php_info_print_table_row(2, "Version", PHP_MSGSRV_VERSION);
    php_info_print_table_row(2, "Released", PHP_MSGSRV_RELEASED);
    php_info_print_table_row(2, "Authors", PHP_MSGSRV_AUTHORS);
    php_info_print_table_end();
    DISPLAY_INI_ENTRIES();
}
/* }}} */

static int msgsrv_write(const int sockfd, char *data, long select_timeout) {
    int rs = 0;
    if ((rs = msgsrv_writeline(sockfd, data, MSGSRV_G(select_timeout))) < 0) {
        if (MSGSRV_G(debug)) syslog(LOG_WARNING, "[MsgSrv][N][E][%d] - [%s]", sockfd, data);
    } else {
        if (MSGSRV_G(debug)) syslog(LOG_DEBUG, "[MsgSrv][S][I][%d] - [%s]", sockfd, data);
    }
    return rs;
}

static int msgsrv_receive(const int sockfd, long timeout, php_msgsrv_msg **message) {
    int rs = 0;
    char *line = NULL;

    // STEP1:read data
    if ((rs = msgsrv_readline(sockfd, &line, MSGSRV_G(read_buffer_size), MSGSRV_G(select_timeout), timeout)) < 0) {
        if (rs == SOCKET_READ_TIMEOUT) MSGSRV_G(last_error) = ERROR_SOCKET_READ_TIMEOUT;
        else MSGSRV_G(last_error) = ERROR_SOCKET_READ_ERROR;
        if (MSGSRV_G(debug)) syslog(LOG_WARNING, "[MsgSrv][R][E][%d] - Read error [%ld]", sockfd, MSGSRV_G(last_error));
        return FAILURE;
    }

    // STEP2:resove message
    if (msgsrv_split(line, message) <= 0) {
        efree(line);
        MSGSRV_G(last_error) = ERROR_INTERNAL_EXCEPTION;
        return FAILURE;
    }
    if (MSGSRV_G(debug)) syslog(LOG_DEBUG, "[MsgSrv][R][I][%d] - [%s]", sockfd, line);
    efree(line);
    MSGSRV_G(last_error) = ERROR_NONE;
    return SUCCESS;
}

static int msgsrv_login(const int sockfd, const char *appname, const char *username, const char *password, php_msgsrv_link **link) {
    int len = 0, cl = 0, tmp_len = 0;
    char *data = NULL, *tmp = NULL;
    php_msgsrv_msg *message = NULL;

    for (;;) {
        if (msgsrv_receive(sockfd, MSGSRV_G(read_timeout), &message) == FAILURE) {
            MSGSRV_G(last_error) = ERROR_SOCKET_READ_ERROR;
            close(sockfd);
            if (MSGSRV_G(debug)) php_error_docref(NULL TSRMLS_CC, E_WARNING, "msgsrv login failed, socket read error [%ld].", MSGSRV_G(last_error));
            return FAILURE;
        }

        // msgsrv's msg
        if (strcmp(LOCAL_MSGSRV_APPNAME, message->appname) != 0) { // not msgsrv's message
            MSGSRV_G(last_error) = ERROR_INVALID_PACKET;
            close(sockfd);
            if (MSGSRV_G(debug)) php_error_docref(NULL TSRMLS_CC, E_WARNING, "msgsrv login failed, invalid packet receive!");
            return FAILURE;
        }

        // msgsrv's msg
        if (strcmp(MSGSRV_CMD_HELLO, message->command) == 0) { // msgsrv's HELLO
            efree(message);
            len = 10 + strlen(appname);
            data = (char *) emalloc((len + 1) * sizeof(char));
            if (data == NULL) {
                close(sockfd);
                MSGSRV_G(last_error) = ERROR_INTERNAL_EXCEPTION;
                if (MSGSRV_G(debug)) php_error_docref(NULL TSRMLS_CC, E_WARNING, "msgsrv login failed, internal exception caught [%ld].", MSGSRV_G(last_error));
                return FAILURE;
            }
            snprintf(data, len + 1, "%s %s %s", LOCAL_MSGSRV_APPNAME, MSGSRV_CMD_APPNAME, appname);
            if (msgsrv_write(sockfd, data, MSGSRV_G(select_timeout)) < 0) {
                efree(data);
                close(sockfd);
                MSGSRV_G(last_error) = ERROR_SOCKET_WRITE_ERROR;
                if (MSGSRV_G(debug)) php_error_docref(NULL TSRMLS_CC, E_WARNING, "msgsrv login failed, socket write error [%ld].", MSGSRV_G(last_error));
                return FAILURE;
            }
            efree(data);
        } else if (strcmp(MSGSRV_CMD_AUTH, message->command) == 0) { // msgsrv's AUTH
            if (message->content == NULL || strlen(message->content) == 0) {
                efree(message);
                close(sockfd);
                MSGSRV_G(last_error) = ERROR_INVALID_PACKET;
                if (MSGSRV_G(debug)) php_error_docref(NULL TSRMLS_CC, E_WARNING, "msgsrv login failed, invalid packet received [%ld].", MSGSRV_G(last_error));
                return FAILURE;
            }
            len = 43 + strlen(username);
            data = (char *) emalloc((len + 1) * sizeof(char));
            if (data == NULL) {
                efree(message);
                close(sockfd);
                MSGSRV_G(last_error) = ERROR_INTERNAL_EXCEPTION;
                if (MSGSRV_G(debug)) php_error_docref(NULL TSRMLS_CC, E_WARNING, "msgsrv login failed, internal exception caught [%ld].", MSGSRV_G(last_error));
                return FAILURE;
            }

            tmp_len = strlen(password) + strlen(message->content);
            tmp = (char *) emalloc((tmp_len + 1) * sizeof(char));
            if (tmp == NULL) {
                efree(data);
                efree(message);
                close(sockfd);
                MSGSRV_G(last_error) = ERROR_INTERNAL_EXCEPTION;
                if (MSGSRV_G(debug)) php_error_docref(NULL TSRMLS_CC, E_WARNING, "msgsrv login failed, internal exception caught [%ld].", MSGSRV_G(last_error));
                return FAILURE;
            }
            snprintf(tmp, tmp_len + 1, "%s%s", password, message->content);
            snprintf(data, len + 1, "%s %s %s %s", LOCAL_MSGSRV_APPNAME, MSGSRV_CMD_USER, username, md5_str(tmp));
            efree(tmp);
            efree(message);

            if (msgsrv_write(sockfd, data, MSGSRV_G(select_timeout)) < 0) {
                efree(data);
                close(sockfd);
                MSGSRV_G(last_error) = ERROR_SOCKET_WRITE_ERROR;
                if (MSGSRV_G(debug)) php_error_docref(NULL TSRMLS_CC, E_WARNING, "msgsrv login failed, socket write error [%ld].", MSGSRV_G(last_error));
                return FAILURE;
            }
            efree(data);
        } else if (strcmp(MSGSRV_CMD_ACCESS, message->command) == 0) { // msgsrv's ACCESS
            if (message->content == NULL || strlen(message->content) == 0) {
                efree(message);
                close(sockfd);
                MSGSRV_G(last_error) = ERROR_INVALID_PACKET;
                if (MSGSRV_G(debug)) php_error_docref(NULL TSRMLS_CC, E_WARNING, "msgsrv login failed, invalid packet received [%ld].", MSGSRV_G(last_error));
                return FAILURE;
            }

            if (strcmp(MSGSRV_ACCESS_DENY, message->content) == 0) {
                efree(message);
                MSGSRV_G(last_error) = ERROR_AUTHORIZE_FAILED;
                close(sockfd);
                if (MSGSRV_G(debug)) php_error_docref(NULL TSRMLS_CC, E_WARNING, "msgsrv login failed, authorize failed!");
                return FAILURE;
            }

            char **params = NULL;
            if (split(message->content, &params, 2, MSGSRV_DELIMITER) != 2) {
                efree(message);
                close(sockfd);
                MSGSRV_G(last_error) = ERROR_INVALID_PACKET;
                if (MSGSRV_G(debug)) php_error_docref(NULL TSRMLS_CC, E_WARNING, "msgsrv login failed, invalid packet received [%ld].", MSGSRV_G(last_error));
                return FAILURE;
            }

            if (strcmp(MSGSRV_ACCESS_GRANTED, params[0]) != 0) {
                efree(message);
                efree(params);
                close(sockfd);
                MSGSRV_G(last_error) = ERROR_INVALID_PACKET;
                if (MSGSRV_G(debug)) php_error_docref(NULL TSRMLS_CC, E_WARNING, "msgsrv login failed, invalid packet received [%ld].", MSGSRV_G(last_error));
                return FAILURE;
            }

            php_msgsrv_link *tmp_link = (php_msgsrv_link *) emalloc(sizeof(php_msgsrv_link));
            if (tmp_link == NULL) {
                close(sockfd);
                MSGSRV_G(last_error) = ERROR_INTERNAL_EXCEPTION;
                if (MSGSRV_G(debug)) php_error_docref(NULL TSRMLS_CC, E_WARNING, "msgsrv login failed, internal exception caught [%ld].", MSGSRV_G(last_error));
                return FAILURE;
            }
            tmp_link->sockfd = sockfd;
            tmp_link->phy_addr = params[1];
            efree(params);

            if (zend_hash_index_update(MSGSRV_G(link_table), sockfd, (void *)tmp_link, sizeof(php_msgsrv_link), (void **)link) == FAILURE) {
                efree(tmp_link);
                close(sockfd);
                MSGSRV_G(last_error) = ERROR_INTERNAL_EXCEPTION;
                if (MSGSRV_G(debug)) php_error_docref(NULL TSRMLS_CC, E_WARNING, "msgsrv login failed, internal exception caught [%ld].", MSGSRV_G(last_error));
                return FAILURE;
            }
            efree(tmp_link);
            MSGSRV_G(last_error) = ERROR_NONE;
            return SUCCESS;
        } else if (strcmp(MSGSRV_CMD_ERROR, message->command) == 0) {
            efree(message);
            close(sockfd);
            MSGSRV_G(last_error) = ERROR_MSGSRV_ERROR;
            if (MSGSRV_G(debug)) php_error_docref(NULL TSRMLS_CC, E_WARNING, "msgsrv login failed, msgsrv error received [%ld].", MSGSRV_G(last_error));
            return FAILURE;
        } else {
            efree(message);
            close(sockfd);
            MSGSRV_G(last_error) = ERROR_INVALID_PACKET;
            if (MSGSRV_G(debug)) php_error_docref(NULL TSRMLS_CC, E_WARNING, "msgsrv login failed, invalid packet received [%ld].", MSGSRV_G(last_error));
            return FAILURE;
        }
    }

}

/* {{{ proto resource msgsrv_open(string host, int port, string appname, string username, string password) */
PHP_FUNCTION(msgsrv_open) {

    const char * host = NULL;
    int host_len = 0;
    long port = 0;
    const char * appname = NULL;
    int appname_len = 0;
    const char * username = NULL;
    int username_len = 0;
    const char * password = NULL;
    int password_len = 0;

    if (zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC, "slsss", &host, &host_len, &port, &appname, &appname_len, &username, &username_len, &password, &password_len) == FAILURE) {
        return;
    }

    if (MSGSRV_G(debug)) syslog(LOG_INFO, "[MsgSrv][O][I][NA] - Connecting to %s:%ld...", host, port);

    int sockfd = -1;
    if ((sockfd = socket_open(host, port)) < 0) {
        switch (sockfd) {
            case -1:
                if (MSGSRV_G(debug)) {
                    syslog(LOG_WARNING, "[MsgSrv][O][E] - Unable to create socket [%d]: %s", errno, strerror(errno));
                    php_error_docref(NULL TSRMLS_CC, E_WARNING, "unable to create socket [%d]: %s", errno, strerror(errno));
                }
                MSGSRV_G(last_error) = ERROR_SOCKET_CREATE_ERROR;
                RETURN_FALSE;
            case -2:
                if (MSGSRV_G(debug)) {
                    syslog(LOG_WARNING, "[MsgSrv][O][E] - Unable to connect to server [%d]: %s", errno, strerror(errno));
                    php_error_docref(NULL TSRMLS_CC, E_WARNING, "unable to connect to server [%d]: %s", errno, strerror(errno));
                }
                MSGSRV_G(last_error) = ERROR_SOCKET_CONNECT_ERROR;
                RETURN_FALSE;
            default:
                if (MSGSRV_G(debug)) {
                    syslog(LOG_WARNING, "[MsgSrv][O][E] - Unkown error [%d]: %s", errno, strerror(errno));
                    php_error_docref(NULL TSRMLS_CC, E_WARNING, "unkown error [%d]: %s", errno, strerror(errno));
                }
                MSGSRV_G(last_error) = ERROR_SOCKET_UNKOWN_ERROR;
                RETURN_FALSE;
        }
    }

    if (MSGSRV_G(debug)) syslog(LOG_INFO, "[MsgSrv][O][I][%d] - Connection established!", sockfd, host, port);

    // try to login msgsrv
    php_msgsrv_link *link = NULL;
    if (msgsrv_login(sockfd, appname, username, password, &link) == SUCCESS) {
        if (MSGSRV_G(debug)) syslog(LOG_INFO, "[MsgSrv][O][I][%d] - Login success [%s]!", sockfd, link->phy_addr);
        ZEND_REGISTER_RESOURCE(return_value, link, msgsrv_link);
        link->res = return_value;
    } else {
        if (MSGSRV_G(debug)) syslog(LOG_WARNING, "[MsgSrv][O][E][%d] - Login failed [%ld]!", sockfd, MSGSRV_G(last_error));
        RETURN_FALSE;
    }

}
/* }}} msgsrv_open */

/* {{{ proto resource msgsrv_phy_addr(resource link) */
PHP_FUNCTION(msgsrv_phy_addr) {
    zval * link_res = NULL;
    php_msgsrv_link *link = NULL;

    if (zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC, "r", &link_res) == FAILURE) {
        return;
    }

    ZEND_FETCH_RESOURCE(link, php_msgsrv_link *, &link_res, -1, PHP_MSGSRV_DESCRIPTOR_RES_NAME, msgsrv_link);
    if (link == NULL) {
        if (MSGSRV_G(debug)) php_error_docref(NULL TSRMLS_CC, E_WARNING, "msgsrv link physical address get failure, link already cloesd!");
        RETURN_FALSE;
    } else {
        RETURN_STRING(link->phy_addr, TRUE);
    }
}
/* }}} msgsrv_phy_addr */

/* {{{ msgsrv_send -- try to send to msgsrv */
static int msgsrv_send(const int sockfd, const char *target, const int target_len, const char *cmd, const int cmd_len, const char *content, int content_len) {
    char *message = NULL;
    int ml = 0;

    ml = target_len + cmd_len + 2 + content_len;
    message = (char *) emalloc((ml + 1) * sizeof(char));
    if (message == NULL) {
        MSGSRV_G(last_error) = ERROR_INTERNAL_EXCEPTION;
        return FAILURE;
    }
    snprintf(message, ml + 1, "%s %s %s", target, cmd, content);
    if (msgsrv_write(sockfd, message, MSGSRV_G(select_timeout)) > 0) {
        efree(message);
        MSGSRV_G(last_error) = ERROR_NONE;
        return SUCCESS;
    } else {
        efree(message);
        MSGSRV_G(last_error) = ERROR_SOCKET_WRITE_ERROR;
        return FAILURE;
    }
}

/* {{{ proto bool msgsrv_send(string target, string cmd, string content, resource link) */
PHP_FUNCTION(msgsrv_send) {
    zval * link_res = NULL;
    php_msgsrv_link *link = NULL;

    const char * target = NULL;
    int target_len = 0;
    const char * cmd = NULL;
    int cmd_len = 0;
    const char * content = NULL;
    int content_len = 0;

    if (zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC, "sssr", &target, &target_len, &cmd, &cmd_len, &content, &content_len, &link_res) == FAILURE) {
        return;
    }
    ZEND_FETCH_RESOURCE(link, php_msgsrv_link *, &link_res, -1, PHP_MSGSRV_DESCRIPTOR_RES_NAME, msgsrv_link);
    if (link == NULL) {
        if (MSGSRV_G(debug)) php_error_docref(NULL TSRMLS_CC, E_WARNING, "msgsrv message send failure, link already cloesd!");
        RETURN_FALSE;
    }
    // try to send..
    if (msgsrv_send(link->sockfd, target, target_len, cmd, cmd_len, content, content_len) == FAILURE) {
        if (MSGSRV_G(debug)) php_error_docref(NULL TSRMLS_CC, E_WARNING, "msgsrv send message failure [%ld].", MSGSRV_G(last_error));
        RETURN_FALSE;
    } else {
        RETURN_TRUE;
    }
}
/* }}} msgsrv_send */

/* {{{ proto bool msgsrv_receive(resource link, callback callback[, int timeout]) */
PHP_FUNCTION(msgsrv_receive) {
    zend_fcall_info fci = empty_fcall_info;
    zend_fcall_info_cache fcc = empty_fcall_info_cache;

    long timeout = MSGSRV_G(request_timeout);

    zval * link_res = NULL;
    php_msgsrv_link *link = NULL;

    if (zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC, "f|l|r", &fci, &fcc, &timeout, &link_res) == FAILURE) {
        return;
    }
    zval **params[5];
    php_msgsrv_msg *message;

    if (!link_res) {
        HashTable *link_hash = MSGSRV_G(link_table);

        for(zend_hash_internal_pointer_reset(link_hash);
                zend_hash_has_more_elements(link_hash) == SUCCESS; zend_hash_move_forward(link_hash)) {

            link = NULL;
            message = NULL;
            // SETP1:get msgsrv link
            if (zend_hash_get_current_data(link_hash, (void**)&link) == FAILURE) {
                // Should never actually fail, since the key is known to exist.
                continue;
            }

            // STEP2:do receive
            if (msgsrv_receive(link->sockfd, timeout, &message) == FAILURE) {
                // php_error_docref(NULL TSRMLS_CC, E_WARNING, "msgsrv receive message error [%ld].", MSGSRV_G(last_error));
                continue;
            }

            link_res = link->res;

            if (start_with(message->appname, LOCAL_MSGSRV_APPNAME) && strcmp(MSGSRV_CMD_ERROR, message->command) == 0) {
                // set status
                zval *status = NULL;
                MAKE_STD_ZVAL(status);
                if (start_with(message->content, MSGSRV_ERROR_APPLICATION)) {
                    MSGSRV_G(last_error) = ERROR_APPLICATION_NOT_FOUND;
                    ZVAL_LONG(status, STATUS_TARGET_NOT_FOUND);
                    if (MSGSRV_G(debug)) php_error_docref(NULL TSRMLS_CC, E_WARNING, "msgsrv receive message failed, msgsrv return application not found [%ld].", MSGSRV_G(last_error));
                } else {
                    MSGSRV_G(last_error) = ERROR_MSGSRV_ERROR;
                    ZVAL_LONG(status, STATUS_MSGSRV_ERROR);
                    if (MSGSRV_G(debug)) php_error_docref(NULL TSRMLS_CC, E_WARNING, "msgsrv receive message failed, msgsrv return error [%ld].", MSGSRV_G(last_error));
                }

                params[0] = &status;
                // set source
                zval *source = NULL;
                MAKE_STD_ZVAL(source);
                ZVAL_STRING(source, message->appname, TRUE);
                params[1] = &source;
                // set command
                zval *command = NULL;
                MAKE_STD_ZVAL(command);
                ZVAL_STRING(command, message->command, TRUE);
                params[2] = &command;
                // set content
                zval *content = NULL;
                MAKE_STD_ZVAL(content);
                ZVAL_STRING(content, message->content, TRUE);
                params[3] = &content;
                // set link
                zval *tmp_link = NULL;
                MAKE_STD_ZVAL(tmp_link);
                ZVAL_RESOURCE(tmp_link, Z_LVAL_P(link_res));
                params[4] = &tmp_link;

                _msgsrv_do_callback(fci, fcc, params, 5);

                efree(message);
                RETURN_FALSE;
            } else {
                // set status
                zval *status = NULL;
                MAKE_STD_ZVAL(status);
                ZVAL_LONG(status, STATUS_OK);
                params[0] = &status;

                // set source
                zval *source = NULL;
                MAKE_STD_ZVAL(source);
                ZVAL_STRING(source, message->appname, TRUE);
                params[1] = &source;

                // set command
                zval *command = NULL;
                MAKE_STD_ZVAL(command);
                ZVAL_STRING(command, message->command, TRUE);
                params[2] = &command;

                // set content
                zval *content = NULL;
                MAKE_STD_ZVAL(content);
                ZVAL_STRING(content, message->content, TRUE);
                params[3] = &content;

                // set link
                zval *tmp_link = NULL;
                MAKE_STD_ZVAL(tmp_link);
                ZVAL_RESOURCE(tmp_link, Z_LVAL_P(link_res));
                params[4] = &tmp_link;

                _msgsrv_do_callback(fci, fcc, params, 5);
                efree(message);
                RETURN_TRUE;
            }
        }

        if (MSGSRV_G(debug)) php_error_docref(NULL TSRMLS_CC, E_WARNING, "msgsrv receive message error, no msgsrv link is available!");

        // set status
        zval *status = NULL;
        MAKE_STD_ZVAL(status);
        ZVAL_LONG(status, STATUS_NO_AVAILABLE);
        params[0] = &status;

        // set source
        zval *source = NULL;
        MAKE_STD_ZVAL(source);
        ZVAL_NULL(source);
        params[1] = &source;

        // set command
        zval *command = NULL;
        MAKE_STD_ZVAL(command);
        ZVAL_NULL(command);
        params[2] = &command;

        // set content
        zval *content = NULL;
        MAKE_STD_ZVAL(content);
        ZVAL_NULL(content);
        params[3] = &content;

        // set link
        zval *tmp_link = NULL;
        MAKE_STD_ZVAL(tmp_link);
        ZVAL_NULL(tmp_link);
        params[4] = &tmp_link;

        _msgsrv_do_callback(fci, fcc, params, 5);
        RETURN_FALSE;
    } else {
        // STEP1: check resource
        ZEND_FETCH_RESOURCE_NO_RETURN(link, php_msgsrv_link *, &link_res, -1, PHP_MSGSRV_DESCRIPTOR_RES_NAME, msgsrv_link);
        if (!link) {
            if (MSGSRV_G(debug)) php_error_docref(NULL TSRMLS_CC, E_WARNING, "msgsrv message receive failure, link already cloesd!");

            // set status
            zval *status = NULL;
            MAKE_STD_ZVAL(status);
            ZVAL_LONG(status, STATUS_CLOSED);
            params[0] = &status;

            // set source
            zval *source = NULL;
            MAKE_STD_ZVAL(source);
            ZVAL_NULL(source);
            params[1] = &source;

            // set command
            zval *command = NULL;
            MAKE_STD_ZVAL(command);
            ZVAL_NULL(command);
            params[2] = &command;

            // set content
            zval *content = NULL;
            MAKE_STD_ZVAL(content);
            ZVAL_NULL(content);
            params[3] = &content;

            // set link
            zval *tmp_link = NULL;
            MAKE_STD_ZVAL(tmp_link);
            ZVAL_RESOURCE(tmp_link, Z_LVAL_P(link_res));
            params[4] = &tmp_link;

            _msgsrv_do_callback(fci, fcc, params, 5);
            RETURN_FALSE;
        }

        // STEP2:do receive
        message = NULL;

        if (msgsrv_receive(link->sockfd, timeout, &message) == FAILURE) {
            if (MSGSRV_G(debug)) php_error_docref(NULL TSRMLS_CC, E_WARNING, "msgsrv receive message error [%ld].", MSGSRV_G(last_error));

            // set status
            zval *status = NULL;
            MAKE_STD_ZVAL(status);
            if (MSGSRV_G(last_error) == ERROR_SOCKET_READ_TIMEOUT) {
                ZVAL_LONG(status, STATUS_TIMEOUT);
            } else if (MSGSRV_G(last_error) == ERROR_SOCKET_READ_ERROR) {
                ZVAL_LONG(status, STATUS_READ_ERROR);
            } else if (MSGSRV_G(last_error) == ERROR_INTERNAL_EXCEPTION) {
                ZVAL_LONG(status, STATUS_INTERNAL_ERROR);
            } else {
                ZVAL_LONG(status, STATUS_INTERNAL_ERROR);
            }
            params[0] = &status;

            // set source
            zval *source = NULL;
            MAKE_STD_ZVAL(source);
            ZVAL_NULL(source);
            params[1] = &source;

            // set command
            zval *command = NULL;
            MAKE_STD_ZVAL(command);
            ZVAL_NULL(command);
            params[2] = &command;

            // set content
            zval *content = NULL;
            MAKE_STD_ZVAL(content);
            ZVAL_NULL(content);
            params[3] = &content;

            // set link
            zval *tmp_link = NULL;
            MAKE_STD_ZVAL(tmp_link);
            ZVAL_RESOURCE(tmp_link, Z_LVAL_P(link_res));
            params[4] = &tmp_link;

            _msgsrv_do_callback(fci, fcc, params, 5);
            RETURN_FALSE;
        }

        if (start_with(message->appname, LOCAL_MSGSRV_APPNAME) && strcmp(MSGSRV_CMD_ERROR, message->command) == 0) {
            // set status
            zval *status = NULL;
            MAKE_STD_ZVAL(status);
            if (start_with(message->content, MSGSRV_ERROR_APPLICATION)) {
                MSGSRV_G(last_error) = ERROR_APPLICATION_NOT_FOUND;
                ZVAL_LONG(status, STATUS_TARGET_NOT_FOUND);
                if (MSGSRV_G(debug)) php_error_docref(NULL TSRMLS_CC, E_WARNING, "msgsrv receive message failed, msgsrv return application not found [%ld].", MSGSRV_G(last_error));
            } else {
                MSGSRV_G(last_error) = ERROR_MSGSRV_ERROR;
                ZVAL_LONG(status, STATUS_MSGSRV_ERROR);
                if (MSGSRV_G(debug)) php_error_docref(NULL TSRMLS_CC, E_WARNING, "msgsrv receive message failed, msgsrv return error [%ld].", MSGSRV_G(last_error));
            }
            params[0] = &status;
            // set source
            zval *source = NULL;
            MAKE_STD_ZVAL(source);
            ZVAL_STRING(source, message->appname, TRUE);
            params[1] = &source;
            // set command
            zval *command = NULL;
            MAKE_STD_ZVAL(command);
            ZVAL_STRING(command, message->command, TRUE);
            params[2] = &command;
            // set content
            zval *content = NULL;
            MAKE_STD_ZVAL(content);
            ZVAL_STRING(content, message->content, TRUE);
            params[3] = &content;
            // set link
            zval *tmp_link = NULL;
            MAKE_STD_ZVAL(tmp_link);
            ZVAL_RESOURCE(tmp_link, Z_LVAL_P(link_res));
            params[4] = &tmp_link;

            _msgsrv_do_callback(fci, fcc, params, 5);

            efree(message);
            RETURN_FALSE;
        } else {
            // set status
            zval *status = NULL;
            MAKE_STD_ZVAL(status);
            ZVAL_LONG(status, STATUS_OK);
            params[0] = &status;

            // set source
            zval *source = NULL;
            MAKE_STD_ZVAL(source);
            ZVAL_STRING(source, message->appname, TRUE);
            params[1] = &source;

            // set command
            zval *command = NULL;
            MAKE_STD_ZVAL(command);
            ZVAL_STRING(command, message->command, TRUE);
            params[2] = &command;

            // set content
            zval *content = NULL;
            MAKE_STD_ZVAL(content);
            ZVAL_STRING(content, message->content, TRUE);
            params[3] = &content;

            // set link
            zval *tmp_link = NULL;
            MAKE_STD_ZVAL(tmp_link);
            ZVAL_RESOURCE(tmp_link, Z_LVAL_P(link_res));
            params[4] = &tmp_link;

            _msgsrv_do_callback(fci, fcc, params, 5);
            efree(message);
            RETURN_TRUE;
        }
    }
}
/* }}} msgsrv_receive */

/* {{{ proto array msgsrv_request(string target, string cmd, string content, resource link[, int timeout]) */
PHP_FUNCTION(msgsrv_request)
{
    zval * link_res = NULL;
    php_msgsrv_link *link = NULL;

    const char * target = NULL;
    int target_len = 0;
    const char * cmd = NULL;
    int cmd_len = 0;
    const char * content = NULL;
    int content_len = 0;
    long timeout = MSGSRV_G(request_timeout);

    if (zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC, "sssr|l", &target, &target_len, &cmd, &cmd_len, &content, &content_len, &link_res, &timeout) == FAILURE) {
        return;
    }

    ZEND_FETCH_RESOURCE(link, php_msgsrv_link *, &link_res, -1, PHP_MSGSRV_DESCRIPTOR_RES_NAME, msgsrv_link);

    if (link == NULL) {
        if (MSGSRV_G(debug)) php_error_docref(NULL TSRMLS_CC, E_WARNING, "msgsrv request message failure, link already cloesd!");
        RETURN_FALSE;
    }

    // STEP1:try to send..
    if (msgsrv_send(link->sockfd, target, target_len, cmd, cmd_len, content, content_len) == FAILURE) {
        if (MSGSRV_G(debug)) php_error_docref(NULL TSRMLS_CC, E_WARNING, "msgsrv request message failure, send error [%ld].", MSGSRV_G(last_error));
        RETURN_FALSE;
    }

    // STEP2:waite results.
    php_msgsrv_msg *message;
    if (msgsrv_receive(link->sockfd, timeout, &message) == FAILURE) {
        if (MSGSRV_G(debug)) php_error_docref(NULL TSRMLS_CC, E_WARNING, "msgsrv request message error, received error [%ld].", MSGSRV_G(last_error));
        RETURN_FALSE;
    }
    if (start_with(message->appname, LOCAL_MSGSRV_APPNAME) && strcmp(MSGSRV_CMD_ERROR, message->command) == 0) {
        if (start_with(message->content, MSGSRV_ERROR_APPLICATION)) {
            efree(message);
            MSGSRV_G(last_error) = ERROR_APPLICATION_NOT_FOUND;
            if (MSGSRV_G(debug)) php_error_docref(NULL TSRMLS_CC, E_WARNING, "msgsrv request message failed, target application not found [%ld].", MSGSRV_G(last_error));
            RETURN_FALSE;
        } else {
            efree(message);
            MSGSRV_G(last_error) = ERROR_MSGSRV_ERROR;
            if (MSGSRV_G(debug)) php_error_docref(NULL TSRMLS_CC, E_WARNING, "msgsrv request message failed, msgsrv return error [%ld].", MSGSRV_G(last_error));
            RETURN_FALSE;
        }
    } else {
        array_init(return_value);
        add_assoc_string(return_value, MSGSRV_KEY_APPNAME, message->appname, 1);
        add_assoc_string(return_value, MSGSRV_KEY_COMMAND, message->command, 1);
        add_assoc_string(return_value, MSGSRV_KEY_CONTENT, message->content, 1);
        efree(message);
    }
}
/* }}} msgsrv_request */

/* {{{ proto int msgsrv_last_error() */
PHP_FUNCTION(msgsrv_last_error) {
    if (ZEND_NUM_ARGS()>0) {
        WRONG_PARAM_COUNT;
    }
    RETURN_LONG(MSGSRV_G(last_error));
}
/* }}} msgsrv_last_error */

/* {{{ proto void msgsrv_close(resource link) */
PHP_FUNCTION(msgsrv_close) {
    zval * link_res = NULL;
    if (zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC, "r", &link_res) == FAILURE) {
        return;
    }
    FREE_RESOURCE(link_res);
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
