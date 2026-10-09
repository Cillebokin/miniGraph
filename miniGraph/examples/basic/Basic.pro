TEMPLATE = app
TARGET = MiniGraphExample
CONFIG += c++11

QT += widgets

win32:RC_ICONS = $$PWD/../../../branding/minigraph.ico

INCLUDEPATH += $$PWD/../../include
LIBS += -L$$OUT_PWD/../../bin -lMiniGraph

SOURCES += \
    $$PWD/Main.cpp \
    $$PWD/BasicCustomRenderer.cpp \
    $$PWD/GeometryCreateDialog.cpp \
    $$PWD/GeometryEditDialog.cpp

HEADERS += \
    $$PWD/BasicCustomRenderer.h \
    $$PWD/GeometryCreateDialog.h \
    $$PWD/GeometryEditDialog.h \
    $$PWD/PolylineText.h

RESOURCES += \
    $$PWD/Basic.qrc
