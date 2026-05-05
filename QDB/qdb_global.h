#pragma once

#include <QtCore/qglobal.h>

#ifndef BUILD_STATIC
# if defined(QDB_LIB)
#  define QDB_EXPORT Q_DECL_EXPORT
# else
#  define QDB_EXPORT Q_DECL_IMPORT
# endif
#else
# define QDB_EXPORT
#endif
