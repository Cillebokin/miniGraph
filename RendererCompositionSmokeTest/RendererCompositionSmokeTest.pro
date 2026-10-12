TEMPLATE = app
TARGET = RendererCompositionSmokeTest

QT += testlib widgets
CONFIG += testcase c++11

win32-msvc {
    QMAKE_CXXFLAGS += /utf-8
}

MINIGRAPH_ROOT = $$clean_path($$PWD/../miniGraph)

CONFIG(debug, debug|release) {
    MINIGRAPH_CONFIGURATION = debug
} else {
    MINIGRAPH_CONFIGURATION = release
}

MINIGRAPH_BIN_DIR = $$MINIGRAPH_ROOT/build/build_polyline_qt512_$$MINIGRAPH_CONFIGURATION/bin

INCLUDEPATH += $$MINIGRAPH_ROOT/include
LIBS += -L$$MINIGRAPH_BIN_DIR -lMiniGraph
PRE_TARGETDEPS += $$MINIGRAPH_BIN_DIR/MiniGraph.lib

DESTDIR = $$OUT_PWD
MINIGRAPH_DLL_PATH = $$shell_path($$MINIGRAPH_BIN_DIR/MiniGraph.dll)
MINIGRAPH_COPY_PATH = $$shell_path($$DESTDIR/MiniGraph.dll)
QMAKE_POST_LINK += $$QMAKE_COPY_FILE \
                   $$MINIGRAPH_DLL_PATH \
                   $$MINIGRAPH_COPY_PATH

SOURCES += \
    $$PWD/TstRendererComposition.cpp \
    $$PWD/RendererModuleA.cpp \
    $$PWD/RendererModuleB.cpp

HEADERS += \
    $$PWD/RendererModuleA.h \
    $$PWD/RendererModuleB.h
