/*
   +----------------------------------------------------------------------+
   | PHP MsgSrv Extension v1.2                                            |
   +----------------------------------------------------------------------+
   | Copyright (c) 2015 ChinaRoad Co., Ltd.  All rights reserved.         |
   +----------------------------------------------------------------------+
   | Author: Deng Tao <dengt@660pp.com>                                   |
   +----------------------------------------------------------------------+
*/

#ifndef PHP_MSGSRV_STRUCTS_H
#define PHP_MSGSRV_STRUCTS_H

ZEND_BEGIN_MODULE_GLOBALS(msgsrv)
    long num_links,num_persistent;
    long max_links,max_persistent, max_pool_size;
    long allow_persistent;
    long connect_timeout;
    long read_timeout;
    long request_timeout;
    long trace_mode;
    long status;    // 1: alive, 0 dead

    // HashTable *persistent_links; // persistent link table
    HashTable *page_links; // current page link table
ZEND_END_MODULE_GLOBALS(msgsrv)

#ifdef ZTS
# define MSGSRV_SG(v) TSRMG(msgsrv_globals_id, zend_msgsrv_globals *, v)
#else
# define MSGSRV_SG(v) (msgsrv_globals.v)
#endif

/* {{{ msgsrv_functions[] */
PHP_FUNCTION(msgsrv_open);
#if (PHP_MAJOR_VERSION >= 5)
ZEND_BEGIN_ARG_INFO_EX(msgsrv_open_arg_info, ZEND_SEND_BY_VAL, ZEND_RETURN_VALUE, 5)
  ZEND_ARG_INFO(0, host)
  ZEND_ARG_INFO(0, port)
  ZEND_ARG_INFO(0, app)
  ZEND_ARG_INFO(0, user)
  ZEND_ARG_INFO(0, pass)
  ZEND_ARG_INFO(0, persistent)
ZEND_END_ARG_INFO()
#else /* PHP 4.x */
#define msgsrv_open_arg_info NULL
#endif

PHP_FUNCTION(msgsrv_connect);
#if (PHP_MAJOR_VERSION >= 5)
ZEND_BEGIN_ARG_INFO_EX(msgsrv_connect_arg_info, ZEND_SEND_BY_VAL, ZEND_RETURN_VALUE, 5)
  ZEND_ARG_INFO(0, host)
  ZEND_ARG_INFO(0, port)
  ZEND_ARG_INFO(0, app)
  ZEND_ARG_INFO(0, user)
  ZEND_ARG_INFO(0, pass)
ZEND_END_ARG_INFO()
#else /* PHP 4.x */
#define msgsrv_connect_arg_info NULL
#endif

PHP_FUNCTION(msgsrv_pconnect);
#if (PHP_MAJOR_VERSION >= 5)
ZEND_BEGIN_ARG_INFO_EX(msgsrv_pconnect_arg_info, ZEND_SEND_BY_VAL, ZEND_RETURN_VALUE, 5)
  ZEND_ARG_INFO(0, host)
  ZEND_ARG_INFO(0, port)
  ZEND_ARG_INFO(0, app)
  ZEND_ARG_INFO(0, user)
  ZEND_ARG_INFO(0, pass)
ZEND_END_ARG_INFO()
#else /* PHP 4.x */
#define msgsrv_pconnect_arg_info NULL
#endif

PHP_FUNCTION(msgsrv_send);
#if (PHP_MAJOR_VERSION >= 5)
ZEND_BEGIN_ARG_INFO_EX(msgsrv_send_arg_info, ZEND_SEND_BY_VAL, ZEND_RETURN_VALUE, 4)
  ZEND_ARG_INFO(0, target)
  ZEND_ARG_INFO(0, cmd)
  ZEND_ARG_INFO(0, body)
  ZEND_ARG_INFO(0, link)
ZEND_END_ARG_INFO()
#else /* PHP 4.x */
#define msgsrv_send_arg_info NULL
#endif

PHP_FUNCTION(msgsrv_receive);
#if (PHP_MAJOR_VERSION >= 5)
ZEND_BEGIN_ARG_INFO_EX(msgsrv_receive_arg_info, ZEND_SEND_BY_VAL, ZEND_RETURN_VALUE, 1)
ZEND_ARG_INFO(0, callback)
ZEND_ARG_INFO(0, timeout)
ZEND_ARG_INFO(0, limit)
ZEND_ARG_INFO(0, link)
ZEND_END_ARG_INFO()
#else /* PHP 4.x */
#define msgsrv_receive_arg_info NULL
#endif

PHP_FUNCTION(msgsrv_request);
#if (PHP_MAJOR_VERSION >= 5)
ZEND_BEGIN_ARG_INFO_EX(msgsrv_request_arg_info, ZEND_SEND_BY_VAL, ZEND_RETURN_VALUE, 4)
  ZEND_ARG_INFO(0, target)
  ZEND_ARG_INFO(0, cmd)
  ZEND_ARG_INFO(0, body)
  ZEND_ARG_INFO(0, link)
  ZEND_ARG_INFO(0, timeout)
ZEND_END_ARG_INFO()
#else /* PHP 4.x */
#define msgsrv_request_arg_info NULL
#endif

PHP_FUNCTION(msgsrv_full_app);
#if (PHP_MAJOR_VERSION >= 5)
ZEND_BEGIN_ARG_INFO_EX(msgsrv_full_app_arg_info, ZEND_SEND_BY_VAL, ZEND_RETURN_VALUE, 1)
  ZEND_ARG_INFO(0, link)
ZEND_END_ARG_INFO()
#else /* PHP 4.x */
#define msgsrv_full_app_arg_info NULL
#endif

PHP_FUNCTION(msgsrv_last_error);
#if (PHP_MAJOR_VERSION >= 5)
ZEND_BEGIN_ARG_INFO_EX(msgsrv_last_error_arg_info, ZEND_SEND_BY_VAL, ZEND_RETURN_VALUE, 1)
  ZEND_ARG_INFO(0, link)
ZEND_END_ARG_INFO()
#else /* PHP 4.x */
#define msgsrv_last_error_arg_info NULL
#endif

PHP_FUNCTION(msgsrv_close);
#if (PHP_MAJOR_VERSION >= 5)
ZEND_BEGIN_ARG_INFO_EX(msgsrv_close_arg_info, ZEND_SEND_BY_VAL, ZEND_RETURN_VALUE, 1)
  ZEND_ARG_INFO(0, link)
ZEND_END_ARG_INFO()
#else /* PHP 4.x */
#define msgsrv_close_arg_info NULL
#endif
/* }}} */


/* {{{ msgsrv_functions[] */
zend_function_entry msgsrv_functions[] = {
  PHP_FE(msgsrv_open          , msgsrv_open_arg_info)
  PHP_FE(msgsrv_connect       , msgsrv_connect_arg_info)
  PHP_FE(msgsrv_pconnect      , msgsrv_pconnect_arg_info)
  PHP_FE(msgsrv_send          , msgsrv_send_arg_info)
  PHP_FE(msgsrv_receive       , msgsrv_receive_arg_info)
  PHP_FE(msgsrv_request       , msgsrv_request_arg_info)
  PHP_FE(msgsrv_last_error    , msgsrv_last_error_arg_info)
  PHP_FE(msgsrv_full_app      , msgsrv_full_app_arg_info)
  PHP_FE(msgsrv_close         , msgsrv_close_arg_info)
  { NULL, NULL, NULL }
};
/* }}} */


/* {{{ msgsrv_module_entry
 */
zend_module_entry msgsrv_module_entry = {
  STANDARD_MODULE_HEADER,
  "msgsrv",
  msgsrv_functions,
  PHP_MINIT(msgsrv),     /* Replace with NULL if there is nothing to do at php startup   */ 
  PHP_MSHUTDOWN(msgsrv), /* Replace with NULL if there is nothing to do at php shutdown  */
  PHP_RINIT(msgsrv),     /* Replace with NULL if there is nothing to do at request start */
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

/* {{{ PHP_INI */
PHP_INI_BEGIN()
  STD_PHP_INI_BOOLEAN("msgsrv.trace_mode",     "0",  PHP_INI_ALL,    OnUpdateLong,   trace_mode,     zend_msgsrv_globals,   msgsrv_globals)
  STD_PHP_INI_BOOLEAN("msgsrv.allow_persistent", "1",  PHP_INI_SYSTEM,   OnUpdateLong,   allow_persistent, zend_msgsrv_globals,   msgsrv_globals)
  STD_PHP_INI_ENTRY_EX("msgsrv.max_persistent",  "-1", PHP_INI_SYSTEM,   OnUpdateLong,   max_persistent,   zend_msgsrv_globals,   msgsrv_globals,  display_link_numbers)
  STD_PHP_INI_ENTRY("msgsrv.max_pool_size",    "10", PHP_INI_ALL,    OnUpdateLong,   max_pool_size,  zend_msgsrv_globals,   msgsrv_globals)
  STD_PHP_INI_ENTRY_EX("msgsrv.max_links",     "-1", PHP_INI_SYSTEM,   OnUpdateLong,   max_links,      zend_msgsrv_globals,   msgsrv_globals,  display_link_numbers)
  STD_PHP_INI_ENTRY("msgsrv.connect_timeout",    "10", PHP_INI_ALL,    OnUpdateLong,   connect_timeout,  zend_msgsrv_globals,   msgsrv_globals)
  STD_PHP_INI_ENTRY("msgsrv.request_timeout",    "10", PHP_INI_ALL,    OnUpdateLong,   request_timeout,  zend_msgsrv_globals,   msgsrv_globals)
  STD_PHP_INI_ENTRY("msgsrv.read_timeout",    "1", PHP_INI_ALL,    OnUpdateLong,   read_timeout,  zend_msgsrv_globals,   msgsrv_globals)
PHP_INI_END()
/* }}} */

#endif /* PHP_MSGSRV_STRUCTS_H */