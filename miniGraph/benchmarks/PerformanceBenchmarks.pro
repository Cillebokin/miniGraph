TEMPLATE = app
TARGET = MiniGraphPerformanceBenchmarks
CONFIG += console c++11
contains(CONFIG, performancebench) {
    DEFINES += MINIGRAPH_ENABLE_PERF_PROFILING
}

QT += widgets

win32-msvc {
    QMAKE_CXXFLAGS += /utf-8
}

INCLUDEPATH += $$PWD/../include
DESTDIR = $$OUT_PWD/../bin
LIBS += -L$$OUT_PWD/../bin -lMiniGraph
win32:LIBS += -lpsapi

SOURCES += $$PWD/CanvasPerformanceBenchmarks.cpp
