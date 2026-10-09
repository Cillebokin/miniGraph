TEMPLATE = lib
CONFIG += dll c++11
TARGET = MiniGraph
DESTDIR = $$OUT_PWD/../bin

QT += widgets

DEFINES += MINIGRAPH_LIBRARY
INCLUDEPATH += $$PWD/../include

HEADERS += \
    $$PWD/../include/MiniGraph/MiniGraph_global.h \
    $$PWD/../include/MiniGraph/CanvasVector.h \
    $$PWD/../include/MiniGraph/CanvasEntities.h \
    $$PWD/../include/MiniGraph/CanvasData.h \
    $$PWD/../include/MiniGraph/CanvasGlobalData.h \
    $$PWD/../include/MiniGraph/CanvasTypes.h \
    $$PWD/../include/MiniGraph/CanvasGeometry.h \
    $$PWD/../include/MiniGraph/CanvasSnap.h \
    $$PWD/../include/MiniGraph/CanvasTransform.h \
    $$PWD/../include/MiniGraph/CanvasDocument.h \
    $$PWD/../include/MiniGraph/CanvasRenderer.h \
    $$PWD/../include/MiniGraph/CanvasView.h \
    $$PWD/../include/MiniGraph/MiniGraph.h \
    $$PWD/CanvasGeometryPrivate.h \
    $$PWD/CanvasPainterRenderer.h

SOURCES += \
    $$PWD/CanvasVector.cpp \
    $$PWD/CanvasEntities.cpp \
    $$PWD/CanvasGeometry.cpp \
    $$PWD/CanvasSnap.cpp \
    $$PWD/CanvasTransform.cpp \
    $$PWD/CanvasDocument.cpp \
    $$PWD/CanvasRenderer.cpp \
    $$PWD/CanvasPainterRenderer.cpp \
    $$PWD/CanvasView.cpp
