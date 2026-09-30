QT += core gui widgets qml quick quickcontrols2 network dbus

CONFIG += c++17
CONFIG += warn_on
TARGET = omaimage
TEMPLATE = app

LIBS += -lcrypt

HEADERS += \
    src/backend.h \
    src/customise.h \
    src/drives.h \
    src/systemtheme.h \
    src/writer.h

SOURCES += \
    src/main.cpp \
    src/backend.cpp \
    src/customise.cpp \
    src/drives.cpp \
    src/systemtheme.cpp \
    src/writer.cpp

RESOURCES += src/resources.qrc

TRANSLATIONS += translations/omaimage_de.ts
CONFIG += lrelease embed_translations
