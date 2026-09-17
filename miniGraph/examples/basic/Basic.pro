TEMPLATE = app
TARGET = MiniGraphExample
CONFIG += c++11

QT += widgets

INCLUDEPATH += $$PWD/../../include
LIBS += -L$$OUT_PWD/../../bin -lMiniGraph

SOURCES += \
    $$PWD/Main.cpp
