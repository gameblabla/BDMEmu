######################################################################
# Bandai Design Master Emulator - Qt6 + qmake project
#
# C11 emulator core + thin C++/Qt6 frontend. Build on Linux with:
#   qmake6 BDMEmu.pro && make
# Package AppImage with:
#   make -f Makefile.linux appimage
#
# Live audio uses SDL3's queue/stream path, matching the SDL3 frontend.
# Disable it explicitly with:
#   qmake6 BDMEmu.pro CONFIG+=no_sdl3_audio
######################################################################

QT += core gui widgets
CONFIG += c++17
TARGET = BDMEmu
TEMPLATE = app

QMAKE_CFLAGS += -std=c11
QMAKE_CFLAGS_WARN_ON += -Wall -Wextra

INCLUDEPATH += include src/core src/qt

SOURCES += \
    src/core/h8.c \
    src/core/bdm_core.c \
    src/video/bdm_video.c \
    src/input/bdm_input.c \
    src/sound/bdm_sound.c \
    src/frontend/bdm_frontend.c \
    src/qt/main.cpp \
    src/qt/Engine.cpp \
    src/qt/MainWindow.cpp \
    src/qt/VideoWidget.cpp

HEADERS += \
    include/bdm_core.h \
    include/bdm_frontend.h \
    include/bdm_input.h \
    include/bdm_sound.h \
    include/bdm_video.h \
    src/core/h8.h \
    src/qt/Engine.h \
    src/qt/MainWindow.h \
    src/qt/VideoWidget.h

!no_sdl3_audio {
    CONFIG += link_pkgconfig
    packagesExist(sdl3) {
        PKGCONFIG += sdl3
        DEFINES += BDM_QT_SDL3_AUDIO
        SOURCES += src/qt/SdlAudio.cpp
        HEADERS += src/qt/SdlAudio.h
    } else {
        warning("SDL3 development package not found; Qt6 frontend will build without live audio. Install SDL3 development files or pass CONFIG+=no_sdl3_audio intentionally.")
    }
}

appimage.target = appimage
appimage.commands = QMAKE=$$QMAKE_QMAKE $$PWD/packaging/make_appimage.sh
QMAKE_EXTRA_TARGETS += appimage
QMAKE_DISTCLEAN += -r $$PWD/build-appimage
