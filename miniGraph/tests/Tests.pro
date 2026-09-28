TEMPLATE = app
TARGET = MiniGraphTests

QT += testlib widgets
CONFIG += testcase c++11

INCLUDEPATH += $$PWD/../include \
               $$PWD/../examples/basic

SOURCES += \
    $$PWD/TstCanvasCore.cpp \
    $$PWD/../examples/basic/BasicCustomRenderer.cpp \
    $$PWD/../examples/basic/GeometryCreateDialog.cpp \
    $$PWD/../examples/basic/GeometryEditDialog.cpp

HEADERS += \
    $$PWD/../examples/basic/BasicCustomRenderer.h \
    $$PWD/../examples/basic/GeometryCreateDialog.h \
    $$PWD/../examples/basic/GeometryEditDialog.h \
    $$PWD/../examples/basic/PolylineText.h

LIBS += -L$$OUT_PWD/../bin -lMiniGraph
