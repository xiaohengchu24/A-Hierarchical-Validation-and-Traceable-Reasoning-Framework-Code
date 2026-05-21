QT       += core gui concurrent network sql

greaterThan(QT_MAJOR_VERSION, 4): QT += widgets

CONFIG += c++17

# You can make your code fail to compile if it uses deprecated APIs.
# In order to do so, uncomment the following line.
#DEFINES += QT_DISABLE_DEPRECATED_BEFORE=0x060000    # disables all the APIs deprecated before Qt 6.0.0

SOURCES += \
    aiclient.cpp \
    aiinputbuilder.cpp \
    main.cpp \
    mainwindow.cpp \
    statisticsmanager.cpp \
    validator.cpp

HEADERS += \
    ReasoningRule.h \
    aiclient.h \
    aiinputbuilder.h \
    mainwindow.h \
    statisticsmanager.h \
    validator.h

FORMS += \
    mainwindow.ui

TRANSLATIONS += \
    AIChatProject_en_AS.ts
CONFIG += lrelease
CONFIG += embed_translations

# 发布配置
CONFIG(release, debug|release) {
    DESTDIR = $$PWD/../release
    OBJECTS_DIR = release/obj
    MOC_DIR = release/moc
    RCC_DIR = release/rcc
    UI_DIR = release/ui
}

# 调试配置
CONFIG(debug, debug|release) {
    DESTDIR = $$PWD/../debug
    OBJECTS_DIR = debug/obj
    MOC_DIR = debug/moc
    RCC_DIR = debug/rcc
    UI_DIR = debug/ui
}

# Windows特定设置 - 简化版本
win32 {
    # 对于MSVC编译器，不需要手动设置subsystem
    # QT会自动处理
    CONFIG += console
}



# Default rules for deployment.
qnx: target.path = /tmp/$${TARGET}/bin
else: unix:!android: target.path = /opt/$${TARGET}/bin
!isEmpty(target.path): INSTALLS += target
