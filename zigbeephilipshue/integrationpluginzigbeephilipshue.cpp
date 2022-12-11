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

#include "integrationpluginzigbeephilipshue.h"
#include "plugininfo.h"

#include <hardware/zigbee/zigbeehardwareresource.h>
#include <zcl/general/zigbeeclusteronoff.h>
#include <zcl/general/zigbeeclusterpowerconfiguration.h>
#include <zcl/general/zigbeeclusterlevelcontrol.h>
#include <zcl/measurement/zigbeeclusteroccupancysensing.h>
#include <zcl/measurement/zigbeeclustertemperaturemeasurement.h>
#include <zcl/measurement/zigbeeclusterilluminancemeasurement.h>
#include <zcl/manufacturerspecific/philips/zigbeeclustermanufacturerspecificphilips.h>


#include <math.h>

IntegrationPluginZigbeePhilipsHue::IntegrationPluginZigbeePhilipsHue():
    ZigbeeIntegrationPlugin(ZigbeeHardwareResource::HandlerTypeVendor, dcZigbeePhilipsHue())
{
}

QString IntegrationPluginZigbeePhilipsHue::name() const
{
    return "Philips Hue";
}

bool IntegrationPluginZigbeePhilipsHue::handleNode(ZigbeeNode *node, const QUuid &/*networkUuid*/)
{
    // Make sure this is from Philips 0x100b
    if (node->nodeDescriptor().manufacturerCode != Zigbee::Philips) {
        qCDebug(dcZigbeePhilipsHue()) << "Manufacturer code not matching. Ignoring node." << node->nodeDescriptor().manufacturerCode;
        return false;
    }

    if (node->hasEndpoint(0x0b)) {
        ZigbeeNodeEndpoint *endpoint = node->getEndpoint(0x0b);

        // Dimmable light
        if ((endpoint->profile() == Zigbee::ZigbeeProfile::ZigbeeProfileLightLink &&
             endpoint->deviceId() == Zigbee::LightLinkDevice::LightLinkDeviceDimmableLight) ||
                (endpoint->profile() == Zigbee::ZigbeeProfile::ZigbeeProfileHomeAutomation &&
                 endpoint->deviceId() == Zigbee::HomeAutomationDeviceDimmableLight)) {

            qCDebug(dcZigbeePhilipsHue()) << "Handling dimmable light for" << node << endpoint;
            createThing(dimmableLightThingClassId, node);
            return true;
        }

        // CT light
        if ((endpoint->profile() == Zigbee::ZigbeeProfileLightLink &&
             endpoint->deviceId() == Zigbee::LightLinkDeviceColourTemperatureLight) ||
                (endpoint->profile() == Zigbee::ZigbeeProfileHomeAutomation &&
                 endpoint->deviceId() == Zigbee::HomeAutomationDeviceColourTemperatureLight)) {

            qCDebug(dcZigbeePhilipsHue()) << "Handling color temperature light for" << node << endpoint;
            createThing(colorTemperatureLightThingClassId, node);
            return true;
        }

        // Color light
        if ((endpoint->profile() == Zigbee::ZigbeeProfileLightLink && endpoint->deviceId() == Zigbee::LightLinkDeviceColourLight) ||
            (endpoint->profile() == Zigbee::ZigbeeProfileLightLink && endpoint->deviceId() == Zigbee::LightLinkDeviceExtendedColourLight) ||
            (endpoint->profile() == Zigbee::ZigbeeProfileHomeAutomation && endpoint->deviceId() == Zigbee::HomeAutomationDeviceExtendedColourLight) ||
            (endpoint->profile() == Zigbee::ZigbeeProfileHomeAutomation && endpoint->deviceId() == Zigbee::HomeAutomationDeviceDimmableColorLight)) {

            qCDebug(dcZigbeePhilipsHue()) << "Handling color light for" << node << endpoint;
            createThing(colorLightThingClassId, node);
            return true;
        }
    }

    if (node->endpoints().count() == 2 && node->hasEndpoint(0x01) && node->hasEndpoint(0x02)) {
        ZigbeeNodeEndpoint *endpointOne = node->getEndpoint(0x01);
        ZigbeeNodeEndpoint *endpointTwo = node->getEndpoint(0x02);

        // Dimmer switch
        if (endpointOne->profile() == Zigbee::ZigbeeProfileLightLink &&
                endpointOne->deviceId() == Zigbee::LightLinkDeviceNonColourSceneController &&
                endpointTwo->profile() == Zigbee::ZigbeeProfileHomeAutomation &&
                endpointTwo->deviceId() == Zigbee::HomeAutomationDeviceSimpleSensor) {

            qCDebug(dcZigbeePhilipsHue()) << "Handling Hue dimmer switch" << node << endpointOne << endpointTwo;
            createThing(dimmerSwitchThingClassId, node);
            bindPowerConfigurationCluster(endpointTwo);
            configurePowerConfigurationInputClusterAttributeReporting(endpointTwo);
            bindManufacturerSpecificPhilipsCluster(endpointTwo);
            return true;
        }

        // Motion sensor (Indoor and outdoor)
        if (endpointOne->profile() == Zigbee::ZigbeeProfileLightLink &&
                endpointOne->deviceId() == Zigbee::LightLinkDeviceOnOffSensor &&
                endpointTwo->profile() == Zigbee::ZigbeeProfileHomeAutomation &&
                endpointTwo->deviceId() == Zigbee::HomeAutomationDeviceOccupacySensor) {

            qCDebug(dcZigbeePhilipsHue()) << "Handling Hue motion sensor" << node << endpointOne << endpointTwo;

            createThing(motionSensorThingClassId, node);
            bindPowerConfigurationCluster(endpointTwo);
            configurePowerConfigurationInputClusterAttributeReporting(endpointTwo);
            bindOccupancySensingCluster(endpointTwo);
            configureOccupancySensingInputClusterAttributeReporting(endpointTwo);
            bindTemperatureMeasurementCluster(endpointTwo);
            configureTemperatureMeasurementInputClusterAttributeReporting(endpointTwo);
            bindIlluminanceMeasurementCluster(endpointTwo);
            configureIlluminanceMeasurementInputClusterAttributeReporting(endpointTwo);
            return true;
        }
    }

    ZigbeeNodeEndpoint *endpointOne = node->getEndpoint(0x01);
    if (endpointOne) {

        if (endpointOne->modelIdentifier() == "RWL022") {
            createThing(dimmerSwitch2ThingClassId, node);
            bindPowerConfigurationCluster(endpointOne);
            configurePowerConfigurationInputClusterAttributeReporting(endpointOne);
            bindManufacturerSpecificPhilipsCluster(endpointOne);
            return true;
        }

        // Smart buttton
        if (endpointOne->profile() == Zigbee::ZigbeeProfileHomeAutomation &&
                endpointOne->deviceId() == Zigbee::HomeAutomationDeviceNonColourSceneController) {
            qCDebug(dcZigbeePhilipsHue()) << "Handling Hue Smart button" << node << endpointOne;
            createThing(smartButtonThingClassId, node);
            bindPowerConfigurationCluster(endpointOne);
            configurePowerConfigurationInputClusterAttributeReporting(endpointOne);
            bindOnOffCluster(endpointOne);
            bindLevelControlCluster(endpointOne);
            return true;
        }

        // Wall switch module
        if (endpointOne->profile() == Zigbee::ZigbeeProfileHomeAutomation &&
                endpointOne->deviceId() == Zigbee::HomeAutomationDeviceNonColourController) {
            createThing(wallSwitchModuleThingClassId, node);
            bindManufacturerSpecificPhilipsCluster(endpointOne);
            bindPowerConfigurationCluster(endpointOne);
            configurePowerConfigurationInputClusterAttributeReporting(endpointOne);
            bindOnOffCluster(endpointOne);
            bindLevelControlCluster(endpointOne);
            return true;
        }
    }

    qCWarning(dcZigbeePhilipsHue()) << "Device manufacturer code matches Philips/Signify, but node not handled by plugin:" << node->modelName();

    return false;
}

void IntegrationPluginZigbeePhilipsHue::setupThing(ThingSetupInfo *info)
{
    Thing *thing = info->thing();

    if (!manageNode(thing)) {
        qCWarning(dcZigbeePhilipsHue()) << "Failed to claim node during setup.";
        info->finish(Thing::ThingErrorHardwareNotAvailable);
        return;
    }

    ZigbeeNode *node = nodeForThing(thing);


    if (thing->thingClassId() == dimmableLightThingClassId
            || thing->thingClassId() == colorTemperatureLightThingClassId
            || thing->thingClassId() == colorLightThingClassId) {

        ZigbeeNodeEndpoint *endpoint = node->getEndpoint(11);
        connectToOnOffInputCluster(thing, endpoint);
        connectToLevelControlInputCluster(thing, endpoint, "brightness");
        connectToColorControlInputCluster(thing, endpoint);
        connectToOtaOutputCluster(thing, endpoint);

        // Attribute reporting does not work for Hue bulbs, so we'll poll if wanted by settings
        if (thing->setting("pollInterval").toUInt() > 0) {
            PluginTimer *timer = m_pollTimers.value(thing);
            timer = hardwareManager()->pluginTimerManager()->registerTimer(thing->setting("pollInterval").toUInt());
            m_pollTimers.insert(thing, timer);
            connect(timer, &PluginTimer::timeout, thing,[this, thing]() {
                pollLight(thing);
            });
        }
        connect(thing, &Thing::settingChanged, this, [this, thing](const ParamTypeId &, const QVariant &value){
            PluginTimer *timer = m_pollTimers.take(thing);
            if (timer) {
                hardwareManager()->pluginTimerManager()->unregisterTimer(timer);
            }
            if (value.toUInt() > 0) {
                timer = hardwareManager()->pluginTimerManager()->registerTimer(value.toUInt());
                m_pollTimers.insert(thing, timer);
                connect(timer, &PluginTimer::timeout, thing,[this, thing]() {
                    pollLight(thing);
                });
            }
        });
    }

    if (thing->thingClassId() == dimmerSwitchThingClassId) {
        ZigbeeNodeEndpoint *endpointHa = node->getEndpoint(0x02);

        connectToPowerConfigurationInputCluster(thing, endpointHa);
        connectToOtaOutputCluster(thing, endpointHa);

        ZigbeeClusterManufacturerSpecificPhilips *philipsCluster = endpointHa->inputCluster<ZigbeeClusterManufacturerSpecificPhilips>(ZigbeeClusterLibrary::ClusterIdManufacturerSpecificPhilips);
        if (!philipsCluster) {
            qCWarning(dcZigbeePhilipsHue()) << "Could not find Manufacturer Specific (Philips) cluster on" << thing << endpointHa;
        } else {
            connect(philipsCluster, &ZigbeeClusterManufacturerSpecificPhilips::buttonPressed, thing, [=](quint8 button, ZigbeeClusterManufacturerSpecificPhilips::Operation operation) {
                qCDebug(dcZigbeePhilipsHue()) << "Button" << button << operation;
                QHash<quint8, QString> buttonMap = {
                    {1, "ON"},
                    {2, "DIM UP"},
                    {3, "DIM DOWN"},
                    {4, "OFF"}
                };
                switch (operation) {
                case ZigbeeClusterManufacturerSpecificPhilips::OperationButtonPress:
                    // This doesn't appear on very quick press/release. But we always get the short release, so let's use that instead
                    break;
                case ZigbeeClusterManufacturerSpecificPhilips::OperationButtonShortRelease:
                    thing->emitEvent(dimmerSwitchPressedEventTypeId, ParamList() << Param(dimmerSwitchPressedEventButtonNameParamTypeId, buttonMap.value(button)));
                    break;
                case ZigbeeClusterManufacturerSpecificPhilips::OperationButtonHold:
                    thing->emitEvent(dimmerSwitchLongPressedEventTypeId, ParamList() << Param(dimmerSwitchLongPressedEventButtonNameParamTypeId, buttonMap.value(button)));
                    break;
                case ZigbeeClusterManufacturerSpecificPhilips::OperationButtonLongRelease:
                    // Release after a longpress. But for longpresses we always seem to get the Hold before, so we'll use that.
                    break;
                }
            });
        }
    }

    if (thing->thingClassId() == dimmerSwitch2ThingClassId) {
        ZigbeeNodeEndpoint *endpoint = node->getEndpoint(0x01);

        connectToPowerConfigurationInputCluster(thing, endpoint);
        connectToOtaOutputCluster(thing, endpoint);

        ZigbeeClusterManufacturerSpecificPhilips *philipsCluster = endpoint->inputCluster<ZigbeeClusterManufacturerSpecificPhilips>(ZigbeeClusterLibrary::ClusterIdManufacturerSpecificPhilips);
        if (!philipsCluster) {
            qCWarning(dcZigbeePhilipsHue()) << "Could not find Manufacturer Specific (Philips) cluster on" << thing << endpoint;
        } else {
            connect(philipsCluster, &ZigbeeClusterManufacturerSpecificPhilips::buttonPressed, thing, [=](quint8 button, ZigbeeClusterManufacturerSpecificPhilips::Operation operation) {
                qCDebug(dcZigbeePhilipsHue()) << "Button" << button << operation;
                QHash<quint8, QString> buttonMap = {
                    {1, "POWER"},
                    {2, "DIM UP"},
                    {3, "DIM DOWN"},
                    {4, "HUE"}
                };
                switch (operation) {
                case ZigbeeClusterManufacturerSpecificPhilips::OperationButtonPress:
                    // This doesn't appear on very quick press/release. But we always get the short release, so let's use that instead
                    break;
                case ZigbeeClusterManufacturerSpecificPhilips::OperationButtonShortRelease:
                    thing->emitEvent(dimmerSwitch2PressedEventTypeId, ParamList() << Param(dimmerSwitch2PressedEventButtonNameParamTypeId, buttonMap.value(button)));
                    break;
                case ZigbeeClusterManufacturerSpecificPhilips::OperationButtonHold:
                    thing->emitEvent(dimmerSwitch2LongPressedEventTypeId, ParamList() << Param(dimmerSwitch2LongPressedEventButtonNameParamTypeId, buttonMap.value(button)));
                    break;
                case ZigbeeClusterManufacturerSpecificPhilips::OperationButtonLongRelease:
                    // Release after a longpress. But for longpresses we always seem to get the Hold before, so we'll use that.
                    break;
                }
            });
        }
    }

    if (thing->thingClassId() == smartButtonThingClassId) {
        ZigbeeNodeEndpoint *endpointHa = node->getEndpoint(0x01);

        connectToPowerConfigurationInputCluster(thing, endpointHa);
        connectToOtaOutputCluster(thing, endpointHa);

        // Connect to button presses
        ZigbeeClusterOnOff *onOffCluster = endpointHa->outputCluster<ZigbeeClusterOnOff>(ZigbeeClusterLibrary::ClusterIdOnOff);
        if (!onOffCluster) {
            qCWarning(dcZigbeePhilipsHue()) << "Could not find on/off client cluster on" << thing << endpointHa;
        } else {
            // The smart button toggles between command(On) and commandOffWithEffect() for short presses...
            connect(onOffCluster, &ZigbeeClusterOnOff::commandSent, thing, [=](ZigbeeClusterOnOff::Command command){
                if (command == ZigbeeClusterOnOff::CommandOn) {
                    qCDebug(dcZigbeePhilipsHue()) << thing << "pressed";
                    emit emitEvent(Event(smartButtonPressedEventTypeId, thing->id()));
                } else {
                    qCWarning(dcZigbeePhilipsHue()) << thing << "unhandled command received" << command;
                }
            });
            connect(onOffCluster, &ZigbeeClusterOnOff::commandOffWithEffectSent, thing, [=](ZigbeeClusterOnOff::Effect effect, quint8 effectVariant){
                qCDebug(dcZigbeePhilipsHue()) << thing << "pressed" << effect << effectVariant;
                emit emitEvent(Event(smartButtonPressedEventTypeId, thing->id()));
            });

            // ...and toggless between level up/down for long presses
            ZigbeeClusterLevelControl *levelCluster = endpointHa->outputCluster<ZigbeeClusterLevelControl>(ZigbeeClusterLibrary::ClusterIdLevelControl);
            if (!levelCluster) {
                qCWarning(dcZigbeePhilipsHue()) << "Could not find level client cluster on" << thing << endpointHa;
            } else {
                connect(levelCluster, &ZigbeeClusterLevelControl::commandStepSent, thing, [=](bool withOnOff, ZigbeeClusterLevelControl::StepMode stepMode, quint8 stepSize, quint16 transitionTime){
                    qCDebug(dcZigbeePhilipsHue()) << thing << "level button pressed" << withOnOff << stepMode << stepSize << transitionTime;
                    switch (stepMode) {
                    case ZigbeeClusterLevelControl::StepModeUp:
                        qCDebug(dcZigbeePhilipsHue()) << thing << "DIM UP pressed";
                        emit emitEvent(Event(smartButtonLongPressedEventTypeId, thing->id()));
                        break;
                    case ZigbeeClusterLevelControl::StepModeDown:
                        qCDebug(dcZigbeePhilipsHue()) << thing << "DIM DOWN pressed";
                        emit emitEvent(Event(smartButtonLongPressedEventTypeId, thing->id()));
                        break;
                    }
                });
            }
        }
    }

    if (thing->thingClassId() == wallSwitchModuleThingClassId) {
        ZigbeeNodeEndpoint *endpointHa = node->getEndpoint(0x01);

        connectToPowerConfigurationInputCluster(thing, endpointHa);
        connectToOtaOutputCluster(thing, endpointHa);

        // Connect to the manufactuer specific cluster
        ZigbeeClusterManufacturerSpecificPhilips *philipsCluster = endpointHa->inputCluster<ZigbeeClusterManufacturerSpecificPhilips>(ZigbeeClusterLibrary::ClusterIdManufacturerSpecificPhilips);
        if (!philipsCluster) {
            qCWarning(dcZigbeePhilipsHue()) << "Could not find Manufacturer Specific (Philips) cluster on" << thing << endpointHa;
        } else {
            qCDebug(dcZigbeePhilipsHue()) << "Connecting to manufacturer specific cluster";
            connect(philipsCluster, &ZigbeeClusterManufacturerSpecificPhilips::buttonPressed, thing, [=](quint8 button, ZigbeeClusterManufacturerSpecificPhilips::Operation operation) {
                qCDebug(dcZigbeePhilipsHue()) << "Button" << button << operation;
                switch (operation) {
                case ZigbeeClusterManufacturerSpecificPhilips::OperationButtonPress:
                    // Unused (could be used for a bool "pressed" state type)
                    break;
                case ZigbeeClusterManufacturerSpecificPhilips::OperationButtonShortRelease:
                    thing->emitEvent(wallSwitchModulePressedEventTypeId, ParamList() << Param(wallSwitchModulePressedEventButtonNameParamTypeId, QString::number(button)));
                    break;
                case ZigbeeClusterManufacturerSpecificPhilips::OperationButtonHold:
                    thing->emitEvent(wallSwitchModuleLongPressedEventTypeId, ParamList() << Param(wallSwitchModuleLongPressedEventButtonNameParamTypeId, QString::number(button)));
                    break;
                case ZigbeeClusterManufacturerSpecificPhilips::OperationButtonLongRelease:
                    // Release after a longpress. But for longpresses we always seem to get the Hold before, so we'll use that.
                    break;
                }
            });
        }

        // Button mode settings
        ZigbeeClusterBasic *basicCluster = endpointHa->inputCluster<ZigbeeClusterBasic>(ZigbeeClusterLibrary::ClusterIdBasic);
        if (basicCluster) {
            QHash<quint8, QString> modeMap = {
                {0x00, "Single rocker"},
                {0x01, "Single push button"},
                {0x02, "Dual rocker"},
                {0x03, "Dual push button"}
            };

            readAttributesDelayed(basicCluster, {0x0034}, Zigbee::Philips);
            connect(basicCluster, &ZigbeeClusterBasic::attributeChanged, thing, [=](const ZigbeeClusterAttribute &attribute){
                if (attribute.id() == 0x0034) {
                    qCDebug(dcZigbeePhilipsHue()) << "Wall switch module button mode changed:" << attribute.dataType().data().toHex();
                    thing->setSettingValue(wallSwitchModuleSettingsButtonModeParamTypeId, modeMap.value(attribute.dataType().toUInt8()));
                }
            });

            connect(thing, &Thing::settingChanged, this, [=](const ParamTypeId &settingTypeId, const QVariant &value){
                if (settingTypeId == wallSwitchModuleSettingsButtonModeParamTypeId) {
                    quint8 buttonMode = modeMap.key(value.toString());
                    ZigbeeClusterLibrary::WriteAttributeRecord deviceModeAttribute;
                    deviceModeAttribute.attributeId = 0x0034;
                    deviceModeAttribute.dataType = Zigbee::Enum8;
                    deviceModeAttribute.data = ZigbeeDataType(buttonMode).data();

                    qCDebug(dcZigbeePhilipsHue()) << "Setting device mode config to" << value.toString();
                    writeAttributesDelayed(endpointHa->getInputCluster(ZigbeeClusterLibrary::ClusterIdBasic), {deviceModeAttribute}, Zigbee::Philips);
                }
            });
        }
    }

    if (thing->thingClassId() == motionSensorThingClassId) {
//        ZigbeeNodeEndpoint *endpoint1 = node->getEndpoint(0x01);
        ZigbeeNodeEndpoint *endpoint2 = node->getEndpoint(0x02);

        connectToPowerConfigurationInputCluster(thing, endpoint2);
        connectToOccupancySensingInputCluster(thing, endpoint2);
        connectToTemperatureMeasurementInputCluster(thing, endpoint2);
        connectToIlluminanceMeasurementInputCluster(thing, endpoint2);
        connectToOtaOutputCluster(thing, endpoint2);

        ZigbeeClusterBasic *basicCluster = endpoint2->inputCluster<ZigbeeClusterBasic>(ZigbeeClusterLibrary::ClusterIdBasic);
        if (basicCluster) {
            qCDebug(dcZigbeePhilipsHue()) << "Requestung led indicator setting";
            readAttributesDelayed(basicCluster, {0x0033}, Zigbee::Philips);
            connect(basicCluster, &ZigbeeClusterBasic::attributeChanged, thing, [thing](const ZigbeeClusterAttribute &attribute){
                if (attribute.id() == 0x0033) {
                    qCDebug(dcZigbeePhilipsHue()) << "Led indication setting changed:" << attribute.dataType().toBool();
                    thing->setSettingValue(motionSensorSettingsLedIndicatorParamTypeId, attribute.dataType().toBool());
                }
            });
        }
        ZigbeeClusterOccupancySensing *occupancySensingCluster = endpoint2->inputCluster<ZigbeeClusterOccupancySensing>(ZigbeeClusterLibrary::ClusterIdOccupancySensing);
        if (occupancySensingCluster) {
            if (occupancySensingCluster->hasAttribute(ZigbeeClusterOccupancySensing::AttributePirOccupiedToUnoccupiedDelay)) {
                thing->setSettingValue(motionSensorSettingsTimeoutParamTypeId, occupancySensingCluster->pirOccupiedToUnoccupiedDelay());
            }
            readAttributesDelayed(occupancySensingCluster, {ZigbeeClusterOccupancySensing::AttributePirOccupiedToUnoccupiedDelay});
            connect(occupancySensingCluster, &ZigbeeClusterOccupancySensing::pirOccupiedToUnoccupiedDelayChanged, thing, [thing](quint16 pirOccupiedToUnoccupiedDelay) {
                qCDebug(dcZigbeePhilipsHue()) << "Occupancy sensing timeout changed:" << pirOccupiedToUnoccupiedDelay;
                thing->setSettingValue(motionSensorSettingsTimeoutParamTypeId, pirOccupiedToUnoccupiedDelay);
            });

            readAttributesDelayed(occupancySensingCluster, {0x0030}, Zigbee::Philips);
            connect(occupancySensingCluster, &ZigbeeClusterOccupancySensing::attributeChanged, thing, [thing](const ZigbeeClusterAttribute &attribute) {
                if (attribute.id() == 0x0030) {
                    qCDebug(dcZigbeePhilipsHue()) << "Occupancy sensing sensitivity changed:" << attribute.dataType().data().toHex();
                    thing->setSettingValue(motionSensorSettingsSensitivityParamTypeId, attribute.dataType().toUInt8());
                }
            });
        }

        connect(thing, &Thing::settingChanged, node, [=](const ParamTypeId &settingsTypeId, const QVariant &value){
            if (settingsTypeId == motionSensorSettingsLedIndicatorParamTypeId) {
                ZigbeeClusterLibrary::WriteAttributeRecord record;
                record.attributeId = 0x0033;
                record.dataType = Zigbee::Bool;
                record.data = ZigbeeDataType(value.toBool()).data();
                writeAttributesDelayed(endpoint2->getInputCluster(ZigbeeClusterLibrary::ClusterIdBasic), {record}, Zigbee::Philips);
            }
            if (settingsTypeId == motionSensorSettingsTimeoutParamTypeId) {
                ZigbeeClusterLibrary::WriteAttributeRecord record;
                record.attributeId = ZigbeeClusterOccupancySensing::AttributePirOccupiedToUnoccupiedDelay;
                record.dataType = Zigbee::Uint16;
                record.data = ZigbeeDataType(static_cast<quint16>(value.toUInt())).data();
                writeAttributesDelayed(endpoint2->getInputCluster(ZigbeeClusterLibrary::ClusterIdOccupancySensing), {record});
            }
            if (settingsTypeId == motionSensorSettingsSensitivityParamTypeId) {
                ZigbeeClusterLibrary::WriteAttributeRecord record;
                record.attributeId = 0x0030;
                record.dataType = Zigbee::Uint8;
                record.data = ZigbeeDataType(static_cast<quint8>(value.toUInt())).data();
                writeAttributesDelayed(endpoint2->getInputCluster(ZigbeeClusterLibrary::ClusterIdOccupancySensing), {record}, Zigbee::Philips);
            }
        });
    }

    info->finish(Thing::ThingErrorNoError);
}

void IntegrationPluginZigbeePhilipsHue::executeAction(ThingActionInfo *info)
{
    ZigbeeNode *node = nodeForThing(info->thing());

    ActionType actionType = info->thing()->thingClass().actionTypes().findById(info->action().actionTypeId());

    ZigbeeNodeEndpoint *endpoint = nullptr;
    if (info->thing()->thingClassId() == dimmableLightThingClassId
            || info->thing()->thingClassId() == colorTemperatureLightThingClassId
            || info->thing()->thingClassId() == colorLightThingClassId) {
        endpoint = node->getEndpoint(11);
    } else if (info->thing()->thingClassId() == dimmerSwitchThingClassId
               || info->thing()->thingClassId() == motionSensorThingClassId) {
        endpoint = node->getEndpoint(2);
    } else if (info->thing()->thingClassId() == smartButtonThingClassId
               || info->thing()->thingClassId() == dimmerSwitch2ThingClassId
               || info->thing()->thingClassId() == wallSwitchModuleThingClassId){
        endpoint = node->getEndpoint(1);
    } else {
        info->finish(Thing::ThingErrorUnsupportedFeature);
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

void IntegrationPluginZigbeePhilipsHue::pollLight(Thing *thing)
{
    ZigbeeNode *node = nodeForThing(thing);
    if (!node) {
        qCWarning(dcZigbeePhilipsHue()) << "Unable to find zigbee node for" << thing->name();
        return;
    }
    ZigbeeNodeEndpoint *endpoint = node->getEndpoint(11);
    if (!endpoint) {
        qCWarning(dcZigbeePhilipsHue()) << "Unable to find endpoint 11 on zigbee node for" << thing->name();
        return;
    }
    qCDebug(dcZigbeePhilipsHue()) << "Polling" << thing->name();
    ZigbeeClusterOnOff *onOffCluster = endpoint->inputCluster<ZigbeeClusterOnOff>(ZigbeeClusterLibrary::ClusterIdOnOff);
    if (onOffCluster) {
        onOffCluster->readAttributes({ZigbeeClusterOnOff::AttributeOnOff});
    }
    ZigbeeClusterLevelControl *levelControlCluster = endpoint->inputCluster<ZigbeeClusterLevelControl>(ZigbeeClusterLibrary::ClusterIdLevelControl);
    if (levelControlCluster) {
        levelControlCluster->readAttributes({ZigbeeClusterLevelControl::AttributeCurrentLevel});
    }
    ZigbeeClusterColorControl *colorControlCluster = endpoint->inputCluster<ZigbeeClusterColorControl>(ZigbeeClusterLibrary::ClusterIdColorControl);
    if (colorControlCluster) {
        colorControlCluster->readAttributes({ZigbeeClusterColorControl::AttributeColorTemperatureMireds, ZigbeeClusterColorControl::AttributeCurrentX, ZigbeeClusterColorControl::AttributeCurrentY});
    }
}

void IntegrationPluginZigbeePhilipsHue::bindManufacturerSpecificPhilipsCluster(ZigbeeNodeEndpoint *endpoint)
{
    qCDebug(dcZigbeePhilipsHue()) << "Binding Manufacturer specific cluster to coordinator";
    ZigbeeDeviceObjectReply *zdoReply = endpoint->node()->deviceObject()->requestBindIeeeAddress(endpoint->endpointId(), ZigbeeClusterLibrary::ClusterIdManufacturerSpecificPhilips, hardwareManager()->zigbeeResource()->coordinatorAddress(endpoint->node()->networkUuid()), 0x01);
    connect(zdoReply, &ZigbeeDeviceObjectReply::finished, endpoint->node(), [=](){
        if (zdoReply->error() != ZigbeeDeviceObjectReply::ErrorNoError) {
            qCWarning(dcZigbeePhilipsHue()) << "Failed to bind manufacturer specific cluster to coordinator" << zdoReply->error();
        } else {
            qCDebug(dcZigbeePhilipsHue()) << "Binding manufacturer specific cluster to coordinator finished successfully";
        }
    });
}

