include(../plugins.pri)

PKGCONFIG += nymea-zigbee

SOURCES += \
    integrationpluginzigbeephilipshue.cpp \
    ../common/zigbeeintegrationplugin.cpp

HEADERS += \
    integrationpluginzigbeephilipshue.h \
    ../common/zigbeeintegrationplugin.h

