/*
 +----------------------------------------------------------------------+
 | ChinaRoad license:                                                      |
 +----------------------------------------------------------------------+
 | Authors: Deng Tao <dengt@007ka.com>                                  |
 +----------------------------------------------------------------------+
 */

/* $ Id: $ */

#ifndef PHP_MSGSRV_H
#define PHP_MSGSRV_H

#ifdef  __cplusplus
extern "C" {
#endif

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <php.h>

#ifdef HAVE_MSGSRV
#define PHP_MSGSRV_VERSION "1.1.0 (Release)"
#define PHP_MSGSRV_RELEASED "2014-08-21"
#define PHP_MSGSRV_AUTHORS "dengt 'dengt@660pp.com' (lead)\n"

#include <php_ini.h>
#include <SAPI.h>
#include <ext/standard/info.h>
#include <Zend/zend_extensions.h>
#ifdef  __cplusplus
} // extern "C" 
#endif
#ifdef  __cplusplus
extern "C" {
#endif

extern zend_module_entry msgsrv_module_entry;
#define phpext_msgsrv_ptr &msgsrv_module_entry

#ifdef PHP_WIN32
#define PHP_MSGSRV_API __declspec(dllexport)
#else
#define PHP_MSGSRV_API
#endif

PHP_MINIT_FUNCTION(msgsrv);
PHP_MSHUTDOWN_FUNCTION(msgsrv);
PHP_RINIT_FUNCTION(msgsrv);
PHP_RSHUTDOWN_FUNCTION(msgsrv);
PHP_MINFO_FUNCTION(msgsrv);

#ifdef ZTS
#include "TSRM.h"
#endif

#define FREE_RESOURCE(resource) zend_list_delete(Z_LVAL_P(resource))

#define PROP_GET_LONG(name)    Z_LVAL_P(zend_read_property(_this_ce, _this_zval, #name, strlen(#name), 1 TSRMLS_CC))
#define PROP_SET_LONG(name, l) zend_update_property_long(_this_ce, _this_zval, #name, strlen(#name), l TSRMLS_CC)

#define PROP_GET_DOUBLE(name)    Z_DVAL_P(zend_read_property(_this_ce, _this_zval, #name, strlen(#name), 1 TSRMLS_CC))
#define PROP_SET_DOUBLE(name, d) zend_update_property_double(_this_ce, _this_zval, #name, strlen(#name), d TSRMLS_CC)

#define PROP_GET_STRING(name)    Z_STRVAL_P(zend_read_property(_this_ce, _this_zval, #name, strlen(#name), 1 TSRMLS_CC))
#define PROP_GET_STRLEN(name)    Z_STRLEN_P(zend_read_property(_this_ce, _this_zval, #name, strlen(#name), 1 TSRMLS_CC))
#define PROP_SET_STRING(name, s) zend_update_property_string(_this_ce, _this_zval, #name, strlen(#name), s TSRMLS_CC)
#define PROP_SET_STRINGL(name, s, l) zend_update_property_stringl(_this_ce, _this_zval, #name, strlen(#name), s, l TSRMLS_CC)

ZEND_BEGIN_MODULE_GLOBALS(msgsrv)
zend_bool debug;
long read_buffer_size;
long request_timeout;
long read_timeout;
long select_timeout;
long last_error; // last error
HashTable *link_table;// connection table			:sockfd => php_msgsrv_link

ZEND_END_MODULE_GLOBALS(msgsrv)

#ifdef ZTS
#define MSGSRV_G(v) TSRMG(msgsrv_globals_id, zend_msgsrv_globals *, v)
#else
#define MSGSRV_G(v) (msgsrv_globals.v)
#endif

PHP_FUNCTION(msgsrv_open);
#if (PHP_MAJOR_VERSION >= 5)
ZEND_BEGIN_ARG_INFO_EX(msgsrv_open_arg_info, ZEND_SEND_BY_VAL, ZEND_RETURN_VALUE, 5)
ZEND_ARG_INFO(0, host)
ZEND_ARG_INFO(0, port)
ZEND_ARG_INFO(0, appname)
ZEND_ARG_INFO(0, username)
ZEND_ARG_INFO(0, password)
ZEND_END_ARG_INFO()
#else /* PHP 4.x */
#define msgsrv_open_arg_info NULL
#endif

PHP_FUNCTION(msgsrv_phy_addr);
#if (PHP_MAJOR_VERSION >= 5)
ZEND_BEGIN_ARG_INFO_EX(msgsrv_phy_addr_arg_info, ZEND_SEND_BY_VAL, ZEND_RETURN_VALUE, 1)
  ZEND_ARG_INFO(0, link)
ZEND_END_ARG_INFO()
#else /* PHP 4.x */
#define msgsrv_phy_addr_arg_info NULL
#endif

PHP_FUNCTION(msgsrv_send);
#if (PHP_MAJOR_VERSION >= 5)
ZEND_BEGIN_ARG_INFO_EX(msgsrv_send_arg_info, ZEND_SEND_BY_VAL, ZEND_RETURN_VALUE, 4)
ZEND_ARG_INFO(0, target)
ZEND_ARG_INFO(0, cmd)
ZEND_ARG_INFO(0, content)
ZEND_ARG_INFO(0, link)
ZEND_END_ARG_INFO()
#else /* PHP 4.x */
#define msgsrv_send_arg_info NULL
#endif

PHP_FUNCTION(msgsrv_receive);
#if (PHP_MAJOR_VERSION >= 5)
ZEND_BEGIN_ARG_INFO_EX(msgsrv_receive_arg_info, ZEND_SEND_BY_VAL, ZEND_RETURN_VALUE, 6)
ZEND_ARG_INFO(0, link)
ZEND_ARG_INFO(0, callback)
ZEND_ARG_INFO(0, timeout)
ZEND_END_ARG_INFO()
#else /* PHP 4.x */
#define msgsrv_receive_arg_info NULL
#endif

PHP_FUNCTION(msgsrv_request);
#if (PHP_MAJOR_VERSION >= 5)
ZEND_BEGIN_ARG_INFO_EX(msgsrv_request_arg_info, ZEND_SEND_BY_VAL, ZEND_RETURN_VALUE, 4)
ZEND_ARG_INFO(0, target)
ZEND_ARG_INFO(0, cmd)
ZEND_ARG_INFO(0, content)
ZEND_ARG_INFO(0, link)
ZEND_ARG_INFO(0, timeout)
ZEND_END_ARG_INFO()
#else /* PHP 4.x */
#define msgsrv_request_arg_info NULL
#endif

PHP_FUNCTION(msgsrv_last_error);
#if (PHP_MAJOR_VERSION >= 5)
ZEND_BEGIN_ARG_INFO_EX(msgsrv_last_error_arg_info, ZEND_SEND_BY_VAL, ZEND_RETURN_VALUE, 0)
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

#ifdef  __cplusplus
} // extern "C" 
#endif

#endif /* PHP_HAVE_MSGSRV */

#endif /* PHP_MSGSRV_H */

/*
 * Local variables:
 * tab-width: 4
 * c-basic-offset: 4
 * End:
 * vim600: noet sw=4 ts=4 fdm=marker
 * vim<600: noet sw=4 ts=4
 */
