#pragma once

#include <QtCore/qglobal.h>

#if defined(MINIGRAPH_LIBRARY)
#  define MINIGRAPH_EXPORT Q_DECL_EXPORT
#else
#  define MINIGRAPH_EXPORT Q_DECL_IMPORT
#endif

