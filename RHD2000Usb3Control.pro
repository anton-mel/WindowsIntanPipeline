QT += core

QT -= gui

TARGET = RHD2000Usb3Control

TEMPLATE = app
CONFIG += console
CONFIG -= app_bundle

SOURCES += main.cpp \
    okFrontPanelDLL.cpp \
    rhd2000evalboardusb3.cpp \
    rhd2000registersusb3.cpp \
    rhd2000datablockusb3.cpp

HEADERS += \
    okFrontPanelDLL.h \
    rhd2000evalboardusb3.h \
    rhd2000registersusb3.h \
    rhd2000datablockusb3.h

