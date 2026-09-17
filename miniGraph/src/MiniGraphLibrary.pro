TEMPLATE = lib
CONFIG += dll c++11
TARGET = MiniGraph
DESTDIR = $$OUT_PWD/../bin

QT += widgets

DEFINES += MINIGRAPH_LIBRARY
INCLUDEPATH += $$PWD/../include

HEADERS += \
    $$PWD/../include/MiniGraph/MiniGraph_global.h \
    $$PWD/../include/MiniGraph/CanvasTypes.h \
    $$PWD/../include/MiniGraph/CanvasTransform.h \
    $$PWD/../include/MiniGraph/CanvasDocument.h \
    $$PWD/../include/MiniGraph/CanvasView.h

SOURCES += \
    $$PWD/CanvasTransform.cpp \
    $$PWD/CanvasDocument.cpp \
    $$PWD/CanvasView.cpp

