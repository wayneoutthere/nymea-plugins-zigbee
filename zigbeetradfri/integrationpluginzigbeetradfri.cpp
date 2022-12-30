/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * *
*
*
* Copyright 2013 - 2022, nymea GmbH
* Contact: contact@nymea.io

* This file is part of nymea.
* This project including source code and documentation is protected by
* copyright law, and remains the property of nymea GmbH. All rights, including
* reproduction, publication, editing and translation, are reserved. The use of
* this project is subject to the terms of a license agreement to be concluded
* with nymea GmbH in accordance with the terms of use of nymea GmbH, available
* under https://nymea.io/license
*
* GNU Lesser General Public License Usage
* Alternatively, this project may be redistributed and/or modified under the
* terms of the GNU Lesser General Public License as published by the Free
* Software Foundation; version 3. This project is distributed in the hope that
* it will be useful, but WITHOUT ANY WARRANTY; without even the implied
* warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
* Lesser General Public License for more details.
*
* You should have received a copy of the GNU Lesser General Public License
* along with this project. If not, see <https://www.gnu.org/licenses/>.
*
* For any further details and any questions please contact us under
* contact@nymea.io or see our FAQ/Licensing Information on
* https://nymea.io/license/faq
*
* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */

#include "integrationpluginzigbeetradfri.h"
#include "plugininfo.h"

#include <zigbeeutils.h>
#include <hardware/zigbee/zigbeehardwareresource.h>
#include <zcl/general/zigbeeclusteronoff.h>
#include <zcl/general/zigbeeclusterpowerconfiguration.h>
#include <zcl/general/zigbeeclusterscenes.h>

#include <qmath.h>
#include <QMetaMethod>
#include <QJsonDocument>


#define AIR_PURIFIER_CLUSTER_ID 0xfc7d // Input cluster on endopint 1
#define AIR_PURIFIER_CLUSTER_ATTRIBUTE_FILTER_RUNTIME 0x0000 // uint32
#define AIR_PURIFIER_CLUSTER_ATTRIBUTE_REPLACE_FILTER 0x0001 // uint8
#define AIR_PURIFIER_CLUSTER_ATTRIBUTE_FILTER_LIFETIME 0x0002 // uint32
#define AIR_PURIFIER_CLUSTER_ATTRIBUTE_CONTROL_PANEL_LIGHT 0x0003 // bool
#define AIR_PURIFIER_CLUSTER_ATTRIBUTE_PM25_MEASUREMENT 0x0004 // uint16
#define AIR_PURIFIER_CLUSTER_ATTRIBUTE_CHILD_LOCK 0x0005 // bool
#define AIR_PURIFIER_CLUSTER_ATTRIBUTE_FAN_MODE 0x0006 // uint8
#define AIR_PURIFIER_CLUSTER_ATTRIBUTE_FAN_SPEED 0x0007 // uint8
#define AIR_PURIFIER_CLUSTER_ATTRIBUTE_DEVICE_RUNTIME 0x0008 // uint32

IntegrationPluginZigbeeTradfri::IntegrationPluginZigbeeTradfri():
    ZigbeeIntegrationPlugin(ZigbeeHardwareResource::HandlerTypeVendor, dcZigbeeTradfri())
{
    setFirmwareIndexUrl(QUrl("http://fw.ota.homesmart.ikea.net/feed/version_info.json"));
//    setFirmwareIndexUrl(QUrl("http://fw.test.ota.homesmart.ikea.net/feed/version_info.json"));
}

QString IntegrationPluginZigbeeTradfri::name() const
{
    return "Ikea TRADFRI";
}

bool IntegrationPluginZigbeeTradfri::handleNode(ZigbeeNode *node, const QUuid &/*networkUuid*/)
{
    // Make sure this is from ikea 0x117C
    if (node->nodeDescriptor().manufacturerCode != Zigbee::Ikea)
        return false;

    ZigbeeNodeEndpoint *endpoint = node->getEndpoint(1);
    if (!endpoint) {
        qCWarning(dcZigbeeTradfri()) << "No endpoint 1 on node" << node;
        return false;
    }

    if ((endpoint->profile() == Zigbee::ZigbeeProfile::ZigbeeProfileLightLink &&
         endpoint->deviceId() == Zigbee::LightLinkDevice::LightLinkDeviceDimmableLight) ||
            (endpoint->profile() == Zigbee::ZigbeeProfile::ZigbeeProfileHomeAutomation &&
             endpoint->deviceId() == Zigbee::HomeAutomationDeviceDimmableLight)) {

        qCDebug(dcZigbeeTradfri()) << "Handling dimmable light for" << node << endpoint;
        createThing(dimmableLightThingClassId, node);
        bindOnOffCluster(endpoint);
        configureOnOffInputClusterAttributeReporting(endpoint);
        bindLevelControlCluster(endpoint);
        configureLevelControlInputClusterAttributeReporting(endpoint);
        return true;
    }

    if ((endpoint->profile() == Zigbee::ZigbeeProfileLightLink &&
         endpoint->deviceId() == Zigbee::LightLinkDeviceColourTemperatureLight) ||
            (endpoint->profile() == Zigbee::ZigbeeProfileHomeAutomation &&
             endpoint->deviceId() == Zigbee::HomeAutomationDeviceColourTemperatureLight)) {

        qCDebug(dcZigbeeTradfri()) << "Handling color temperature light for" << node << endpoint;
        createThing(colorTemperatureLightThingClassId, node);
        bindOnOffCluster(endpoint);
        configureOnOffInputClusterAttributeReporting(endpoint);
        bindLevelControlCluster(endpoint);
        configureLevelControlInputClusterAttributeReporting(endpoint);
        bindColorControlCluster(endpoint);
        configureColorControlInputClusterAttributeReporting(endpoint);
        return true;
    }

    if ((endpoint->profile() == Zigbee::ZigbeeProfileLightLink && endpoint->deviceId() == Zigbee::LightLinkDeviceColourLight) ||
        (endpoint->profile() == Zigbee::ZigbeeProfileLightLink && endpoint->deviceId() == Zigbee::LightLinkDeviceExtendedColourLight) ||
        (endpoint->profile() == Zigbee::ZigbeeProfileHomeAutomation && endpoint->deviceId() == Zigbee::HomeAutomationDeviceExtendedColourLight) ||
        (endpoint->profile() == Zigbee::ZigbeeProfileHomeAutomation && endpoint->deviceId() == Zigbee::HomeAutomationDeviceDimmableColorLight)) {

        qCDebug(dcZigbeeTradfri()) << "Handling color light for" << node << endpoint;
        createThing(colorLightThingClassId, node);
        bindOnOffCluster(endpoint);
        configureOnOffInputClusterAttributeReporting(endpoint);
        bindLevelControlCluster(endpoint);
        configureLevelControlInputClusterAttributeReporting(endpoint);
        bindColorControlCluster(endpoint);
        configureColorControlInputClusterAttributeReporting(endpoint);
        return true;
    }

    if (endpoint->profile() == Zigbee::ZigbeeProfileHomeAutomation && endpoint->deviceId() == Zigbee::HomeAutomationDeviceNonColourController) {
        if (endpoint->modelIdentifier().contains("on/off switch")) {
            qCDebug(dcZigbeeTradfri()) << "Handling TRADFRI on/off switch" << node << endpoint;
            createThing(onOffSwitchThingClassId, node);
            bindPowerConfigurationCluster(endpoint);
            configurePowerConfigurationInputClusterAttributeReporting(endpoint);
            bindOnOffCluster(endpoint);
            bindLevelControlCluster(endpoint);
            return true;
        } else if (endpoint->modelIdentifier().toLower().contains("shortcut button")) {
            qCDebug(dcZigbeeTradfri()) << "Handling TRADFRI SHORTCUT Button" << node << endpoint;
            createThing(shortcutButtonThingClassId, node);
            bindPowerConfigurationCluster(endpoint);
            configurePowerConfigurationInputClusterAttributeReporting(endpoint);
            bindOnOffCluster(endpoint);
            bindLevelControlCluster(endpoint);
            return true;
        }
    }

    if (endpoint->profile() == Zigbee::ZigbeeProfileHomeAutomation && endpoint->deviceId() == Zigbee::HomeAutomationDeviceOnOffSensor) {
        qCDebug(dcZigbeeTradfri()) << "Handling TRADFRI motion sensor" << node << endpoint;
        createThing(motionSensorThingClassId, node);
        bindPowerConfigurationCluster(endpoint);
        configurePowerConfigurationInputClusterAttributeReporting(endpoint);
        bindOnOffCluster(endpoint);
        return true;
    }

    if (endpoint->modelIdentifier().contains("remote control")) {
        qCDebug(dcZigbeeTradfri()) << "Handling TRADFRI remote control" << node << endpoint;
        createThing(remoteThingClassId, node);
        bindPowerConfigurationCluster(endpoint);
        configurePowerConfigurationInputClusterAttributeReporting(endpoint);
        bindOnOffCluster(endpoint);
        bindLevelControlCluster(endpoint);
        return true;
    }

    if (endpoint->modelIdentifier().contains("SYMFONISK")) {
        qCDebug(dcZigbeeTradfri()) << "Handling TRADFRI Symfonisk sound remote" << node << endpoint;
        createThing(soundRemoteThingClassId, node);
        bindPowerConfigurationCluster(endpoint);
        configurePowerConfigurationInputClusterAttributeReporting(endpoint);
        bindOnOffCluster(endpoint);
        bindLevelControlCluster(endpoint);
        return true;
    }

    if (endpoint->modelIdentifier().contains("STARKVIND")) {
        qCDebug(dcZigbeeTradfri()) << "Handling STARKVIND Air Purifier" << node << endpoint;
        createThing(airPurifierThingClassId, node);
        bindCluster(endpoint, (ZigbeeClusterLibrary::ClusterId)AIR_PURIFIER_CLUSTER_ID);
        configureAirPurifierAttributeReporting(endpoint);
        return true;
    }

    if (endpoint->profile() == Zigbee::ZigbeeProfileHomeAutomation && endpoint->deviceId() == Zigbee::HomeAutomationDeviceRangeExtender) {
        qCDebug(dcZigbeeTradfri()) << "Handling TRADFRI signal repeater" << node << endpoint;
        createThing(signalRepeaterThingClassId, node);
        return true;
    }

    return false;
}

void IntegrationPluginZigbeeTradfri::setupThing(ThingSetupInfo *info)
{
    Thing *thing = info->thing();
    if (!manageNode(thing)) {
        qCWarning(dcZigbeeTradfri()) << "Failed to claim node during setup.";
        info->finish(Thing::ThingErrorHardwareNotAvailable);
        return;
    }

    ZigbeeNode *node = nodeForThing(thing);

    ZigbeeNodeEndpoint *endpoint = node->getEndpoint(1);
    if (!endpoint) {
        qCWarning(dcZigbeeTradfri()) << "Could not find endpoint for" << thing;
        info->finish(Thing::ThingErrorSetupFailed);
        return;
    }

    if (thing->thingClassId() == dimmableLightThingClassId
            || thing->thingClassId() == colorTemperatureLightThingClassId
            || thing->thingClassId() == colorLightThingClassId) {
        connectToOnOffInputCluster(thing, endpoint);
        connectToLevelControlInputCluster(thing, endpoint, "brightness");
        connectToOtaOutputCluster(thing, endpoint);
    }

    if (thing->thingClassId() == onOffSwitchThingClassId) {
        connectToPowerConfigurationInputCluster(thing, endpoint);
        connectToOtaOutputCluster(thing, endpoint);

        // Receive on/off commands
        ZigbeeClusterOnOff *onOffCluster = endpoint->outputCluster<ZigbeeClusterOnOff>(ZigbeeClusterLibrary::ClusterIdOnOff);
        if (!onOffCluster) {
            qCWarning(dcZigbeeTradfri()) << "Could not find on/off client cluster on" << thing << endpoint;
        } else {
            connect(onOffCluster, &ZigbeeClusterOnOff::commandReceived, thing, [=](ZigbeeClusterOnOff::Command command, const QByteArray &/*payload*/, quint8 transactionSequenceNumber){
                if (isDuplicate(transactionSequenceNumber)) {
                    return;
                }
                qCDebug(dcZigbeeTradfri()) << thing << "button pressed" << command;
                if (command == ZigbeeClusterOnOff::CommandOn) {
                    qCDebug(dcZigbeeTradfri()) << thing << "pressed ON";
                    emit emitEvent(Event(onOffSwitchPressedEventTypeId, thing->id(), ParamList() << Param(onOffSwitchPressedEventButtonNameParamTypeId, "ON")));
                } else if (command == ZigbeeClusterOnOff::CommandOff) {
                    qCDebug(dcZigbeeTradfri()) << thing << "pressed OFF";
                    emit emitEvent(Event(onOffSwitchPressedEventTypeId, thing->id(), ParamList() << Param(onOffSwitchPressedEventButtonNameParamTypeId, "OFF")));
                }
            });
        }

        // Receive level control commands
        ZigbeeClusterLevelControl *levelCluster = endpoint->outputCluster<ZigbeeClusterLevelControl>(ZigbeeClusterLibrary::ClusterIdLevelControl);
        if (!levelCluster) {
            qCWarning(dcZigbeeTradfri()) << "Could not find level client cluster on" << thing << endpoint;
        } else {
            connect(levelCluster, &ZigbeeClusterLevelControl::commandReceived, thing, [=](ZigbeeClusterLevelControl::Command command, const QByteArray &payload, quint8 transactionSequenceNumber){
                if (isDuplicate(transactionSequenceNumber)) {
                    return;
                }
                qCDebug(dcZigbeeTradfri()) << thing << "button pressed" << command << payload.toHex();
                switch (command) {
                case ZigbeeClusterLevelControl::CommandMoveWithOnOff:
                    qCDebug(dcZigbeeTradfri()) << thing << "long pressed ON";
                    emit emitEvent(Event(onOffSwitchLongPressedEventTypeId, thing->id(), ParamList() << Param(onOffSwitchLongPressedEventButtonNameParamTypeId, "ON")));
                    break;
                case ZigbeeClusterLevelControl::CommandMove:
                    qCDebug(dcZigbeeTradfri()) << thing << "long pressed OFF";
                    emit emitEvent(Event(onOffSwitchLongPressedEventTypeId, thing->id(), ParamList() << Param(onOffSwitchLongPressedEventButtonNameParamTypeId, "OFF")));
                    break;
                default:
                    break;
                }
            });
        }
    }

    if (thing->thingClassId() == shortcutButtonThingClassId) {
        connectToPowerConfigurationInputCluster(thing, endpoint);
        connectToOtaOutputCluster(thing, endpoint);

        // Receive on/off commands
        ZigbeeClusterOnOff *onOffCluster = endpoint->outputCluster<ZigbeeClusterOnOff>(ZigbeeClusterLibrary::ClusterIdOnOff);
        if (!onOffCluster) {
            qCWarning(dcZigbeeTradfri()) << "Could not find on/off client cluster on" << thing << endpoint;
        } else {
            connect(onOffCluster, &ZigbeeClusterOnOff::commandReceived, thing, [=](ZigbeeClusterOnOff::Command command, const QByteArray &/*payload*/, quint8 transactionSequenceNumber){
                if (isDuplicate(transactionSequenceNumber)) {
                    return;
                }
                qCDebug(dcZigbeeTradfri()) << thing << "button pressed" << command;
                if (command == ZigbeeClusterOnOff::CommandOn) {
                    qCDebug(dcZigbeeTradfri()) << thing << "pressed";
                    emit emitEvent(Event(shortcutButtonPressedEventTypeId, thing->id()));
                }
            });
        }

        // Receive level control commands
        ZigbeeClusterLevelControl *levelCluster = endpoint->outputCluster<ZigbeeClusterLevelControl>(ZigbeeClusterLibrary::ClusterIdLevelControl);
        if (!levelCluster) {
            qCWarning(dcZigbeeTradfri()) << "Could not find level client cluster on" << thing << endpoint;
        } else {
            connect(levelCluster, &ZigbeeClusterLevelControl::commandReceived, thing, [=](ZigbeeClusterLevelControl::Command command, const QByteArray &payload, quint8 transactionSequenceNumber){
                if (isDuplicate(transactionSequenceNumber)) {
                    return;
                }
                qCDebug(dcZigbeeTradfri()) << thing << "button pressed" << command << payload.toHex();
                switch (command) {
                case ZigbeeClusterLevelControl::CommandMoveWithOnOff:
                    qCDebug(dcZigbeeTradfri()) << thing << "long pressed";
                    emit emitEvent(Event(shortcutButtonLongPressedEventTypeId, thing->id()));
                    break;
                case ZigbeeClusterLevelControl::CommandStopWithOnOff:
                    qCDebug(dcZigbeeTradfri()) << thing << "released aftr long pressed";
                    break;
                default:
                    break;
                }
            });
        }
    }

    if (thing->thingClassId() == motionSensorThingClassId) {
        connectToPowerConfigurationInputCluster(thing, endpoint);
        connectToOtaOutputCluster(thing, endpoint);

        // Create plugintimer if required
        if (!m_presenceTimer) {
            m_presenceTimer = hardwareManager()->pluginTimerManager()->registerTimer(1);
        }
        connect(m_presenceTimer, &PluginTimer::timeout, thing, [thing](){
            if (thing->stateValue(motionSensorIsPresentStateTypeId).toBool()) {
                int timeout = thing->setting(motionSensorSettingsTimeoutParamTypeId).toInt();
                QDateTime lastSeenTime = QDateTime::fromMSecsSinceEpoch(thing->stateValue(motionSensorLastSeenTimeStateTypeId).toULongLong() * 1000);
                if (lastSeenTime.addSecs(timeout) < QDateTime::currentDateTime()) {
                    thing->setStateValue(motionSensorIsPresentStateTypeId, false);
                }
            }
        });

        // Receive on/off commands
        ZigbeeClusterOnOff *onOffCluster = endpoint->outputCluster<ZigbeeClusterOnOff>(ZigbeeClusterLibrary::ClusterIdOnOff);
        if (!onOffCluster) {
            qCWarning(dcZigbeeTradfri()) << "Could not find on/off client cluster on" << thing << endpoint;
        } else {
            connect(onOffCluster, &ZigbeeClusterOnOff::commandOnWithTimedOffReceived, thing, [=](bool acceptOnlyWhenOn, quint16 onTime, quint16 offTime){
                qCDebug(dcZigbeeTradfri()) << thing << "command received: Accept only when on:" << acceptOnlyWhenOn << "On time:" << onTime / 10 << "s" << "Off time:" << offTime / 10 << "s";
                thing->setStateValue(motionSensorLastSeenTimeStateTypeId, QDateTime::currentDateTime().toMSecsSinceEpoch() / 1000);
                thing->setStateValue(motionSensorIsPresentStateTypeId, true);
                m_presenceTimer->start();
            });
        }
    }

    if (thing->thingClassId() == remoteThingClassId) {
        connectToPowerConfigurationInputCluster(thing, endpoint);
        connectToOtaOutputCluster(thing, endpoint);

        // Receive on/off commands
        ZigbeeClusterOnOff *onOffCluster = endpoint->outputCluster<ZigbeeClusterOnOff>(ZigbeeClusterLibrary::ClusterIdOnOff);
        if (!onOffCluster) {
            qCWarning(dcZigbeeTradfri()) << "Could not find on/off client cluster on" << thing << endpoint;
        } else {
            connect(onOffCluster, &ZigbeeClusterOnOff::commandReceived, thing, [=](ZigbeeClusterOnOff::Command command, const QByteArray &/*parameters*/, quint8 transactionSequenceNumber){
                if (isDuplicate(transactionSequenceNumber)) {
                    return;
                }
                qCDebug(dcZigbeeTradfri()) << thing << "power command received" << command;
                if (command == ZigbeeClusterOnOff::CommandToggle) {
                    qCDebug(dcZigbeeTradfri()) << thing << "button pressed: Power";
                    emit emitEvent(Event(remotePressedEventTypeId, thing->id(), ParamList() << Param(remotePressedEventButtonNameParamTypeId, "Power")));
                }
            });
        }

        // Receive level control commands
        ZigbeeClusterLevelControl *levelCluster = endpoint->outputCluster<ZigbeeClusterLevelControl>(ZigbeeClusterLibrary::ClusterIdLevelControl);
        if (!levelCluster) {
            qCWarning(dcZigbeeTradfri()) << "Could not find level client cluster on" << thing << endpoint;
        } else {
            connect(levelCluster, &ZigbeeClusterLevelControl::commandMoveReceived, thing, [=](bool withOnOff, ZigbeeClusterLevelControl::MoveMode moveMode, quint8 rate, quint8 transactionSequenceNumber){
                if (isDuplicate(transactionSequenceNumber)) {
                    return;
                }
                qCDebug(dcZigbeeTradfri()) << "level command move received" << withOnOff << moveMode << rate;
                switch (moveMode) {
                case ZigbeeClusterLevelControl::MoveModeUp:
                    qCDebug(dcZigbeeTradfri()) << thing << "button longpressed: Up";
                    emit emitEvent(Event(remoteLongPressedEventTypeId, thing->id(), ParamList() << Param(remoteLongPressedEventButtonNameParamTypeId, "Up")));
                    break;
                case ZigbeeClusterLevelControl::MoveModeDown:
                    qCDebug(dcZigbeeTradfri()) << thing << "button longpressed: Down";
                    emit emitEvent(Event(remoteLongPressedEventTypeId, thing->id(), ParamList() << Param(remoteLongPressedEventButtonNameParamTypeId, "Down")));
                    break;
                }
            });

            connect(levelCluster, &ZigbeeClusterLevelControl::commandStepReceived, thing, [=](bool withOnOff, ZigbeeClusterLevelControl::StepMode stepMode, quint8 stepSize, quint16 transitionTime, quint8 transactionSequenceNumber){
                if (isDuplicate(transactionSequenceNumber)) {
                    return;
                }
                qCDebug(dcZigbeeTradfri()) << "level command step received" << withOnOff << stepMode << stepSize << transitionTime;
                switch (stepMode) {
                case ZigbeeClusterLevelControl::StepModeUp:
                    qCDebug(dcZigbeeTradfri()) << thing << "button pressed: Up";
                    emit emitEvent(Event(remotePressedEventTypeId, thing->id(), ParamList() << Param(remotePressedEventButtonNameParamTypeId, "Up")));
                    break;
                case ZigbeeClusterLevelControl::StepModeDown:
                    qCDebug(dcZigbeeTradfri()) << thing << "button pressed: Down";
                    emit emitEvent(Event(remotePressedEventTypeId, thing->id(), ParamList() << Param(remotePressedEventButtonNameParamTypeId, "Down")));
                    break;
                }
            });
        }

        // Receive scene commands
        ZigbeeClusterScenes *scenesCluster = endpoint->outputCluster<ZigbeeClusterScenes>(ZigbeeClusterLibrary::ClusterIdScenes);
        if (!scenesCluster) {
            qCWarning(dcZigbeeTradfri()) << "Could not find scenes client cluster on" << thing << endpoint;
        } else {
            connect(scenesCluster, &ZigbeeClusterScenes::commandReceived, thing, [=](ZigbeeClusterScenes::Command command, quint16 groupId, quint8 sceneId, quint8 transactionSequenceNumber){
                if (isDuplicate(transactionSequenceNumber)) {
                    return;
                }

                qCDebug(dcZigbeeTradfri()) << thing << "scene command received" << command << groupId << sceneId;

                // Note: these comands are not in the specs
                if (command == 0x07) {
                    if (groupId == 256) {
                        qCDebug(dcZigbeeTradfri()) << thing << "button pressed: Right";
                        emit emitEvent(Event(remotePressedEventTypeId, thing->id(), ParamList() << Param(remotePressedEventButtonNameParamTypeId, "Right")));
                    } else if (groupId == 257) {
                        qCDebug(dcZigbeeTradfri()) << thing << "button pressed: Left";
                        emit emitEvent(Event(remotePressedEventTypeId, thing->id(), ParamList() << Param(remotePressedEventButtonNameParamTypeId, "Left")));
                    }
                } else if (command == 0x08) {
                    if (groupId == 3328) {
                        qCDebug(dcZigbeeTradfri()) << thing << "button pressed: Right";
                        emit emitEvent(Event(remoteLongPressedEventTypeId, thing->id(), ParamList() << Param(remoteLongPressedEventButtonNameParamTypeId, "Right")));
                    } else if (groupId == 3329) {
                        qCDebug(dcZigbeeTradfri()) << thing << "button pressed: Left";
                        emit emitEvent(Event(remoteLongPressedEventTypeId, thing->id(), ParamList() << Param(remoteLongPressedEventButtonNameParamTypeId, "Left")));
                    }
                }
            });
        }
    }

    if (thing->thingClassId() == soundRemoteThingClassId) {
        connectToPowerConfigurationInputCluster(thing, endpoint);
        connectToOtaOutputCluster(thing, endpoint);

        QTimer *moveTimer = new QTimer(thing);
        moveTimer->setInterval(500);
        m_soundRemoteMoveTimers.insert(thing, moveTimer);
        connect(moveTimer, &QTimer::timeout, thing, [=](){
            soundRemoteMove(thing, static_cast<ZigbeeClusterLevelControl::MoveMode>(moveTimer->property("direction").toInt()));
        });

        // Receive on/off commands
        ZigbeeClusterOnOff *onOffCluster = endpoint->outputCluster<ZigbeeClusterOnOff>(ZigbeeClusterLibrary::ClusterIdOnOff);
        if (!onOffCluster) {
            qCWarning(dcZigbeeTradfri()) << "Could not find on/off client cluster on" << thing << endpoint;
        } else {
            connect(onOffCluster, &ZigbeeClusterOnOff::commandReceived, thing, [=](ZigbeeClusterOnOff::Command command, const QByteArray &payload, quint8 transactionSequenceNumber){
                if (isDuplicate(transactionSequenceNumber)) {
                    return;
                }
                qCDebug(dcZigbeeTradfri()) << thing << "button pressed" << command << payload;
                if (command == ZigbeeClusterOnOff::CommandToggle) {
                    qCDebug(dcZigbeeTradfri()) << thing << "pressed power";
                    emit emitEvent(Event(soundRemotePressedEventTypeId, thing->id()));
                }
            });
        }

        // Receive level control commands
        ZigbeeClusterLevelControl *levelCluster = endpoint->outputCluster<ZigbeeClusterLevelControl>(ZigbeeClusterLibrary::ClusterIdLevelControl);
        if (!levelCluster) {
            qCWarning(dcZigbeeTradfri()) << "Could not find level client cluster on" << thing << endpoint;
        } else {
            connect(levelCluster, &ZigbeeClusterLevelControl::commandMoveReceived, thing, [=](bool withOnOff, ZigbeeClusterLevelControl::MoveMode moveMode, quint8 rate, quint8 transactionSequenceNumber){
                Q_UNUSED(withOnOff)
                Q_UNUSED(rate)
                if (isDuplicate(transactionSequenceNumber)) {
                    return;
                }
                qCDebug(dcZigbeeTradfri()) << thing->name() << "starting move timer";
                soundRemoteMove(thing, moveMode);
                m_soundRemoteMoveTimers.value(thing)->setProperty("direction", moveMode);
                m_soundRemoteMoveTimers.value(thing)->start();
            });

            connect(levelCluster, &ZigbeeClusterLevelControl::commandReceived, thing, [=](ZigbeeClusterLevelControl::Command command, const QByteArray &payload){
                Q_UNUSED(payload)
                if (command == ZigbeeClusterLevelControl::CommandStop) {
                    qCDebug(dcZigbeeTradfri()) << thing->name() << "stopping move timer";
                    m_soundRemoteMoveTimers.value(thing)->stop();
                }
            });
        }
    }

    if (thing->thingClassId() == airPurifierThingClassId) {
        connectToOtaOutputCluster(thing, endpoint);

        ZigbeeCluster *airPurifierCluster = endpoint->getInputCluster((ZigbeeClusterLibrary::ClusterId)AIR_PURIFIER_CLUSTER_ID);
        if (!airPurifierCluster) {
            qCWarning(dcZigbeeTradfri()) << "Air purifier cluster not foud on thing" << thing->name();
        } else {
            airPurifierCluster->readAttributes({AIR_PURIFIER_CLUSTER_ATTRIBUTE_FILTER_RUNTIME,
                                               AIR_PURIFIER_CLUSTER_ATTRIBUTE_REPLACE_FILTER,
                                               AIR_PURIFIER_CLUSTER_ATTRIBUTE_CONTROL_PANEL_LIGHT,
                                               AIR_PURIFIER_CLUSTER_ATTRIBUTE_PM25_MEASUREMENT,
                                               AIR_PURIFIER_CLUSTER_ATTRIBUTE_CHILD_LOCK,
                                               AIR_PURIFIER_CLUSTER_ATTRIBUTE_FAN_MODE,
                                               AIR_PURIFIER_CLUSTER_ATTRIBUTE_FAN_SPEED},
                                               Zigbee::Ikea);
            connect(airPurifierCluster, &ZigbeeCluster::attributeChanged, thing, [thing](const ZigbeeClusterAttribute &attribute){
                qCDebug(dcZigbeeTradfri()) << "Air Purifier Attribute changed:" << attribute;
                switch (attribute.id()) {
                case AIR_PURIFIER_CLUSTER_ATTRIBUTE_FILTER_RUNTIME:
                    thing->setStateValue(airPurifierFilterRuntimeStateTypeId, attribute.dataType().toUInt32());
                    return;
                case AIR_PURIFIER_CLUSTER_ATTRIBUTE_REPLACE_FILTER:
                    thing->setStateValue(airPurifierReplaceFilterStateTypeId, attribute.dataType().toBool());
                    return;
                case AIR_PURIFIER_CLUSTER_ATTRIBUTE_CONTROL_PANEL_LIGHT:
                    thing->setStateValue(airPurifierLightPowerStateTypeId, !attribute.dataType().toBool());
                    return;
                case AIR_PURIFIER_CLUSTER_ATTRIBUTE_PM25_MEASUREMENT: {
                    quint16 pm25 = attribute.dataType().toUInt16();
                    if (pm25 < 0xFFFF) {
                        thing->setStateValue(airPurifierPm25StateTypeId, attribute.dataType().toUInt16());
                    }
                    return;
                }
                case AIR_PURIFIER_CLUSTER_ATTRIBUTE_CHILD_LOCK:
                    thing->setStateValue(airPurifierChildLockStateTypeId, attribute.dataType().toBool());
                    return;
                case AIR_PURIFIER_CLUSTER_ATTRIBUTE_FAN_MODE: {
                    quint8 fanMode = attribute.dataType().toUInt8();
                    thing->setStateValue(airPurifierPowerStateTypeId, fanMode > 0);
                    thing->setStateValue(airPurifierAutoStateTypeId, fanMode == 1);
                    return;
                }
                case AIR_PURIFIER_CLUSTER_ATTRIBUTE_FAN_SPEED: {
                    quint8 fanSpeed = attribute.dataType().toUInt8();
                    thing->setStateValue(airPurifierPowerStateTypeId, fanSpeed > 0);
                    thing->setStateValue(airPurifierFlowRateStateTypeId, fanSpeed / 10);
                    return;
                }
                }
            });
        }

    }
    if (thing->thingClassId() == signalRepeaterThingClassId) {
        connectToOtaOutputCluster(thing, endpoint);
    }
    info->finish(Thing::ThingErrorNoError);
}

void IntegrationPluginZigbeeTradfri::executeAction(ThingActionInfo *info)
{
    ZigbeeNode *node = nodeForThing(info->thing());

    ZigbeeNodeEndpoint *endpoint = node->getEndpoint(1);

    ActionType actionType = info->thing()->thingClass().actionTypes().findById(info->action().actionTypeId());

    if (info->thing()->thingClassId() == airPurifierThingClassId) {
        ZigbeeCluster *airPurifierCluster = endpoint->getInputCluster((ZigbeeClusterLibrary::ClusterId)AIR_PURIFIER_CLUSTER_ID);
        if (!airPurifierCluster) {
            qCWarning(dcZigbeeTradfri()) << "Could not find air purifier cluster for" << info->thing()->name();
            info->finish(Thing::ThingErrorHardwareFailure);
            return;
        }

        ZigbeeClusterLibrary::WriteAttributeRecord record;
        if (actionType.id() == airPurifierPowerActionTypeId) {
            record.attributeId = AIR_PURIFIER_CLUSTER_ATTRIBUTE_FAN_MODE;
            record.dataType = Zigbee::Uint8;
            bool power = info->action().paramValue(airPurifierPowerActionPowerParamTypeId).toBool();
            record.data = ZigbeeDataType(static_cast<quint8>(power ? 1 : 0)).data();
        }
        if (actionType.id() == airPurifierAutoActionTypeId) {
            record.attributeId = AIR_PURIFIER_CLUSTER_ATTRIBUTE_FAN_MODE;
            record.dataType = Zigbee::Uint8;
            bool autoMode = info->action().paramValue(airPurifierAutoActionAutoParamTypeId).toBool();
            record.data = ZigbeeDataType(static_cast<quint8>(autoMode ? 1 : info->thing()->stateValue(airPurifierFlowRateStateTypeId).toUInt() * 10)).data();
        }
        if (actionType.id() == airPurifierFlowRateActionTypeId) {
            record.attributeId = AIR_PURIFIER_CLUSTER_ATTRIBUTE_FAN_MODE;
            record.dataType = Zigbee::Uint8;
            quint8 value =  info->action().paramValue(airPurifierFlowRateActionFlowRateParamTypeId).toUInt() * 10;
            record.data = ZigbeeDataType(value).data();
        }
        if (actionType.id() == airPurifierLightPowerActionTypeId) {
            record.attributeId = AIR_PURIFIER_CLUSTER_ATTRIBUTE_CONTROL_PANEL_LIGHT;
            record.dataType = Zigbee::Bool;
            bool power = info->action().paramValue(airPurifierLightPowerActionLightPowerParamTypeId).toBool();
            record.data = ZigbeeDataType(!power).data();
        }
        if (actionType.id() == airPurifierChildLockActionTypeId) {
            record.attributeId = AIR_PURIFIER_CLUSTER_ATTRIBUTE_CHILD_LOCK;
            record.dataType = Zigbee::Bool;
            bool power = info->action().paramValue(airPurifierChildLockActionChildLockParamTypeId).toBool();
            record.data = ZigbeeDataType(power).data();
        }

        ZigbeeClusterReply *reply = airPurifierCluster->writeAttributes({record}, Zigbee::Ikea);
        connect(reply, &ZigbeeClusterReply::finished, this, [reply, info](){
            if (reply->error() != ZigbeeClusterReply::ErrorNoError) {
                info->finish(Thing::ThingErrorHardwareFailure);
            } else {
                info->finish(Thing::ThingErrorNoError);
            }
        });
        return;
    }

    if (actionType.name() == "power") {
        executePowerOnOffInputCluster(info, endpoint);
        return;
    }

    if (actionType.name() == "brightness") {
        executeBrightnessLevelControlInputCluster(info, endpoint);
        return;
    }

    if (actionType.name() == "colorTemperature") {
        executeColorTemperatureColorControlInputCluster(info, endpoint);
        return;
    }

    if (actionType.name() == "color") {
        executeColorColorControlInputCluster(info, endpoint);
        return;
    }

    if (actionType.name() == "alert") {
        executeIdentifyIdentifyInputCluster(info, endpoint);
        return;
    }
    if (actionType.name() == "performUpdate") {
        enableFirmwareUpdate(info->thing());
        executeImageNotifyOtaOutputCluster(info, endpoint);
        return;
    }

    info->finish(Thing::ThingErrorUnsupportedFeature);
}

void IntegrationPluginZigbeeTradfri::thingRemoved(Thing *thing)
{
    ZigbeeIntegrationPlugin::thingRemoved(thing);

    if (thing->thingClassId() == soundRemoteThingClassId) {
        delete m_soundRemoteMoveTimers.take(thing);
    }
}

QList<ZigbeeIntegrationPlugin::FirmwareIndexEntry> IntegrationPluginZigbeeTradfri::firmwareIndexFromJson(const QByteArray &data) const
{
    qCDebug(dcZigbeeTradfri()) << "Fetched firmware index:" << qUtf8Printable(data);
    QList<FirmwareIndexEntry> ret;

    QJsonParseError error;
    QJsonDocument jsonDoc = QJsonDocument::fromJson(data, &error);
    if (error.error != QJsonParseError::NoError) {
        qCWarning(dcZigbeeTradfri()) << "Failed to parse firmware index" << error.errorString() << "\n" << qUtf8Printable(data);
        return ret;
    }
    foreach (const QVariant &entryVariant, jsonDoc.toVariant().toList()) {
        QVariantMap map = entryVariant.toMap();
        FirmwareIndexEntry entry;
        entry.fileVersion = (map.value("fw_file_version_MSB").toUInt() << 16) | map.value("fw_file_version_LSB").toUInt();
        entry.fileSize = map.value("fw_filesize").toUInt();
        entry.imageType = map.value("fw_image_type").toUInt();
        entry.manufacturerCode = map.value("fw_manufacturer_id").toUInt();
        entry.url = map.value("fw_binary_url").toUrl();
        ret.append(entry);
    }

    return ret;
}

void IntegrationPluginZigbeeTradfri::soundRemoteMove(Thing *thing, ZigbeeClusterLevelControl::MoveMode mode)
{
    int currentLevel = thing->stateValue(soundRemoteLevelStateTypeId).toUInt();
    int stepSize = thing->setting(soundRemoteSettingsStepSizeParamTypeId).toUInt();
    switch (mode) {
    case ZigbeeClusterLevelControl::MoveModeUp: {
        qCDebug(dcZigbeeTradfri()) << "sound remote move up!";
        thing->setStateValue(soundRemoteLevelStateTypeId, qMin(100, currentLevel + stepSize));
        emitEvent(Event(soundRemoteIncreaseEventTypeId, thing->id()));
        break;
    }
    case ZigbeeClusterLevelControl::MoveModeDown: {
        qCDebug(dcZigbeeTradfri()) << "sound remote move down!";
        thing->setStateValue(soundRemoteLevelStateTypeId, qMax(0, currentLevel - stepSize));
        emitEvent(Event(soundRemoteDecreaseEventTypeId, thing->id()));
        break;
    }
    }
}

bool IntegrationPluginZigbeeTradfri::isDuplicate(quint8 transactionSequenceNumber)
{
    if (m_lastReceivedTransactionSequenceNumber == transactionSequenceNumber) {
        qCDebug(dcZigbeeTradfri()) << "Duplicate packet detected. TSN:" << transactionSequenceNumber;
        return true;
    }
    m_lastReceivedTransactionSequenceNumber = transactionSequenceNumber;
    return false;
}

void IntegrationPluginZigbeeTradfri::configureAirPurifierAttributeReporting(ZigbeeNodeEndpoint *endpoint)
{
    ZigbeeCluster *airPurifierCluster = endpoint->getInputCluster((ZigbeeClusterLibrary::ClusterId)AIR_PURIFIER_CLUSTER_ID);
    if (!airPurifierCluster) {
        qCWarning(dcZigbeeTradfri()) << "Air purifier cluster not found on this endpoint";
        return;
    }

    ZigbeeClusterLibrary::AttributeReportingConfiguration filterRuntimeConfig;
    filterRuntimeConfig.attributeId = AIR_PURIFIER_CLUSTER_ATTRIBUTE_FILTER_RUNTIME;
    filterRuntimeConfig.dataType = Zigbee::Uint32;
    filterRuntimeConfig.minReportingInterval = 60;
    filterRuntimeConfig.maxReportingInterval = 1200;
    filterRuntimeConfig.reportableChange = ZigbeeDataType(static_cast<quint32>(0)).data();

    ZigbeeClusterLibrary::AttributeReportingConfiguration replaceFilterConfig;
    replaceFilterConfig.attributeId = AIR_PURIFIER_CLUSTER_ATTRIBUTE_REPLACE_FILTER;
    replaceFilterConfig.dataType = Zigbee::Uint8;
    replaceFilterConfig.reportableChange = ZigbeeDataType(static_cast<quint8>(0)).data();

//    ZigbeeClusterLibrary::AttributeReportingConfiguration filterLifetimeConfig;
//    filterLifetimeConfig.attributeId = AIR_PURIFIER_CLUSTER_ATTRIBUTE_FILTER_LIFETIME;
//    filterLifetimeConfig.dataType = Zigbee::Uint32;
//    filterLifetimeConfig.minReportingInterval = 60;
//    filterLifetimeConfig.maxReportingInterval = 1200;
//    replaceFilterConfig.reportableChange = ZigbeeDataType(static_cast<quint32>(0)).data();


    ZigbeeClusterLibrary::AttributeReportingConfiguration lightConfig;
    lightConfig.attributeId = AIR_PURIFIER_CLUSTER_ATTRIBUTE_CONTROL_PANEL_LIGHT;
    lightConfig.dataType = Zigbee::Bool;

    ZigbeeClusterLibrary::AttributeReportingConfiguration pm25Config;
    pm25Config.attributeId = AIR_PURIFIER_CLUSTER_ATTRIBUTE_PM25_MEASUREMENT;
    pm25Config.dataType = Zigbee::Uint16;
    pm25Config.minReportingInterval = 60;
    pm25Config.maxReportingInterval = 1200;
    pm25Config.reportableChange = ZigbeeDataType(static_cast<quint16>(1)).data();

    ZigbeeClusterLibrary::AttributeReportingConfiguration childLockConfig;
    childLockConfig.attributeId = AIR_PURIFIER_CLUSTER_ATTRIBUTE_CHILD_LOCK;
    childLockConfig.dataType = Zigbee::Bool;

    ZigbeeClusterLibrary::AttributeReportingConfiguration fanModeConfig;
    fanModeConfig.attributeId = AIR_PURIFIER_CLUSTER_ATTRIBUTE_FAN_MODE;
    fanModeConfig.dataType = Zigbee::Uint8;
    fanModeConfig.minReportingInterval = 0;
    fanModeConfig.maxReportingInterval = 1200;
    fanModeConfig.reportableChange = ZigbeeDataType(static_cast<quint8>(1)).data();

    ZigbeeClusterLibrary::AttributeReportingConfiguration fanSpeedConfig;
    fanSpeedConfig.attributeId = AIR_PURIFIER_CLUSTER_ATTRIBUTE_FAN_SPEED;
    fanSpeedConfig.dataType = Zigbee::Uint8;
    fanSpeedConfig.minReportingInterval = 0;
    fanSpeedConfig.maxReportingInterval = 1200;
    fanSpeedConfig.reportableChange = ZigbeeDataType(static_cast<quint8>(1)).data();


    ZigbeeClusterReply *reportingReply = airPurifierCluster->configureReporting({filterRuntimeConfig, replaceFilterConfig, lightConfig, pm25Config, childLockConfig, fanModeConfig, fanSpeedConfig}, Zigbee::Ikea);
    connect(reportingReply, &ZigbeeClusterReply::finished, this, [=](){
        if (reportingReply->error() != ZigbeeClusterReply::ErrorNoError) {
            qCWarning(dcZigbeeTradfri()) << "Failed to configure fan control attribute reporting" << reportingReply->error();
        }
    });
}
