    QT += core gui xml
    greaterThan(QT_MAJOR_VERSION, 4): QT += widgets

SOURCES += \
    main.cpp \
    MainWindow.cpp \
    Screen.cpp \
    M6502.cpp \
    Memory.cpp \
    MemoryViewer.cpp

HEADERS += \
    MainWindow.h \
    Screen.h \
    M6502.h \
    Memory.h \
    MemoryViewer.h

TARGET = qtpom1

RESOURCES += \
    qtpom1.qrc

OTHER_FILES += \
    images/pom1.png \
    images/reset.png \
    images/saveMemory.png \
    images/screen.png \
    images/fleche.png \
    images/quit.png \
    README.md \
    LICENSE \
    images/about.png \
    images/aboutQt.png \
    images/debug.png \
    images/hardreset.png \
    images/loadMemory.png \
    images/memory.png \
    images/pasteCode.png \
    roms/basic.rom \
    roms/charmap.rom \
    roms/WozMonitor.rom
