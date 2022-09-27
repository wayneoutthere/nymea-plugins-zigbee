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
            connect(onOffCluster, &ZigbeeClusterOnOff::commandSent, thing, [=](ZigbeeClusterOnOff::Command command, const QByteArray &/*payload*/, quint8 transactionSequenceNumber){
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
            connect(levelCluster, &ZigbeeClusterLevelControl::commandSent, thing, [=](ZigbeeClusterLevelControl::Command command, const QByteArray &payload, quint8 transactionSequenceNumber){
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
            connect(onOffCluster, &ZigbeeClusterOnOff::commandSent, thing, [=](ZigbeeClusterOnOff::Command command, const QByteArray &/*payload*/, quint8 transactionSequenceNumber){
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
            connect(levelCluster, &ZigbeeClusterLevelControl::commandSent, thing, [=](ZigbeeClusterLevelControl::Command command, const QByteArray &payload, quint8 transactionSequenceNumber){
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
            connect(onOffCluster, &ZigbeeClusterOnOff::commandOnWithTimedOffSent, thing, [=](bool acceptOnlyWhenOn, quint16 onTime, quint16 offTime){
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
            connect(onOffCluster, &ZigbeeClusterOnOff::commandSent, thing, [=](ZigbeeClusterOnOff::Command command, const QByteArray &/*parameters*/, quint8 transactionSequenceNumber){
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
            connect(levelCluster, &ZigbeeClusterLevelControl::commandMoveSent, thing, [=](bool withOnOff, ZigbeeClusterLevelControl::MoveMode moveMode, quint8 rate, quint8 transactionSequenceNumber){
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

            connect(levelCluster, &ZigbeeClusterLevelControl::commandStepSent, thing, [=](bool withOnOff, ZigbeeClusterLevelControl::StepMode stepMode, quint8 stepSize, quint16 transitionTime, quint8 transactionSequenceNumber){
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
            connect(scenesCluster, &ZigbeeClusterScenes::commandSent, thing, [=](ZigbeeClusterScenes::Command command, quint16 groupId, quint8 sceneId, quint8 transactionSequenceNumber){
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
            connect(onOffCluster, &ZigbeeClusterOnOff::commandSent, thing, [=](ZigbeeClusterOnOff::Command command, const QByteArray &payload, quint8 transactionSequenceNumber){
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
            connect(levelCluster, &ZigbeeClusterLevelControl::commandMoveSent, thing, [=](bool withOnOff, ZigbeeClusterLevelControl::MoveMode moveMode, quint8 rate, quint8 transactionSequenceNumber){
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

            connect(levelCluster, &ZigbeeClusterLevelControl::commandSent, thing, [=](ZigbeeClusterLevelControl::Command command, const QByteArray &payload){
                Q_UNUSED(payload)
                if (command == ZigbeeClusterLevelControl::CommandStop) {
                    qCDebug(dcZigbeeTradfri()) << thing->name() << "stopping move timer";
                    m_soundRemoteMoveTimers.value(thing)->stop();
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

