dnl
dnl $ Id: $
dnl

PHP_ARG_ENABLE(msgsrv, whether to enable msgsrv functions,
[  --enable-msgsrv         Enable msgsrv support])

if test "$PHP_MSGSRV" != "no"; then
  export OLD_CPPFLAGS="$CPPFLAGS"
  export CPPFLAGS="$CPPFLAGS $INCLUDES -DHAVE_MSGSRV"

  AC_MSG_CHECKING(PHP version)
  AC_TRY_COMPILE([#include <php_version.h>], [
#if PHP_VERSION_ID < 40000
#error  this extension requires at least PHP version 4.0.0
#endif
],
[AC_MSG_RESULT(ok)],
[AC_MSG_ERROR([need at least PHP 4.0.0])])

  export CPPFLAGS="$OLD_CPPFLAGS"


  PHP_SUBST(MSGSRV_SHARED_LIBADD)
  AC_DEFINE(HAVE_MSGSRV, 1, [ ])

  PHP_NEW_EXTENSION(msgsrv, msgsrv.c md5.c library.c, $ext_shared)

fi

