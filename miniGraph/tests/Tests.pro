TEMPLATE = app
TARGET = MiniGraphTests

QT += testlib widgets
CONFIG += testcase c++11

INCLUDEPATH += $$PWD/../include

SOURCES += \
    $$PWD/TstCanvasCore.cpp

LIBS += -L$$OUT_PWD/../bin -lMiniGraph
