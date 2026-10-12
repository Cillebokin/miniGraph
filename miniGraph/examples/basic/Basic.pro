TEMPLATE = app
TARGET = MiniGraphExample
CONFIG += c++11
win32-msvc {
    QMAKE_CXXFLAGS += /utf-8
}

QT += widgets

win32:RC_ICONS = $$PWD/../../../branding/minigraph.ico

INCLUDEPATH += $$PWD/../../include
LIBS += -L$$OUT_PWD/../../bin -lMiniGraph

SOURCES += \
    $$PWD/Main.cpp \
    $$PWD/BasicCanvasData.cpp \
    $$PWD/BasicCustomRenderer.cpp \
    $$PWD/BasicCircleCreationAction.cpp \
    $$PWD/BasicLineCreationAction.cpp \
    $$PWD/BasicPointPlacementAction.cpp \
    $$PWD/GeometryCreateDialog.cpp \
    $$PWD/GeometryEditDialog.cpp

HEADERS += \
    $$PWD/BasicCanvasData.h \
    $$PWD/BasicCustomRenderer.h \
    $$PWD/BasicCircleCreationAction.h \
    $$PWD/BasicLineCreationAction.h \
    $$PWD/BasicPointPlacementAction.h \
    $$PWD/GeometryCreateDialog.h \
    $$PWD/GeometryEditDialog.h \
    $$PWD/PolylineText.h

RESOURCES += \
    $$PWD/Basic.qrc
