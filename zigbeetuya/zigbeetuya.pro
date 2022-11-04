include(../plugins.pri)

PKGCONFIG += nymea-zigbee

SOURCES += \
    dpvalue.cpp \
    integrationpluginzigbeetuya.cpp \
    ../common/zigbeeintegrationplugin.cpp

HEADERS += \
    dpvalue.h \
    integrationpluginzigbeetuya.h \
    ../common/zigbeeintegrationplugin.h



