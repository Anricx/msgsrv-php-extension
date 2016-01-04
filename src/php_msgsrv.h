/*
   +----------------------------------------------------------------------+
   | PHP MsgSrv Extension v1.2                                            |
   +----------------------------------------------------------------------+
   | Copyright (c) 2015 ChinaRoad Co., Ltd.  All rights reserved.         |
   +----------------------------------------------------------------------+
   | Author: Deng Tao <dengt@660pp.com>                                   |
   +----------------------------------------------------------------------+
*/

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
#define PHP_MSGSRV_VERSION "1.3.0 (Release)"
#define PHP_MSGSRV_RELEASED "2015-12-25"
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

#ifdef ZEND_ENGINE_2
# include "zend_exceptions.h"
#else
  /* PHP 4 compat */
# define OnUpdateLong   OnUpdateInt
# define E_STRICT       E_NOTICE
#endif

#if (PHP_MAJOR_VERSION == 5 && PHP_MINOR_VERSION < 3)
const zend_fcall_info empty_fcall_info = { 0, NULL, NULL, NULL, NULL, 0, NULL, NULL, 0 };
#endif

#if (PHP_MAJOR_VERSION >= 5 && PHP_MINOR_VERSION > 3)
#define list_entry zend_rsrc_list_entry
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

#include "msgsrv_structs.h"

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
