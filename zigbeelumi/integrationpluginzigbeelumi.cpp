/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * *
*
*
* Copyright 2013 - 2020, nymea GmbH
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

#include "integrationpluginzigbeelumi.h"
#include "plugininfo.h"

#include <zigbeenodeendpoint.h>
#include <hardware/zigbee/zigbeehardwareresource.h>

#include <zcl/general/zigbeeclusteronoff.h>
#include <zcl/general/zigbeeclustermultistateinput.h>
#include <zcl/general/zigbeeclusteranalogoutput.h>
#include <zcl/general/zigbeeclusteranaloginput.h>
#include <zcl/measurement/zigbeeclusteroccupancysensing.h>
#include <zcl/measurement/zigbeeclusterilluminancemeasurement.h>
#include <zcl/measurement/zigbeeclustertemperaturemeasurement.h>
#include <zcl/measurement/zigbeeclusterrelativehumiditymeasurement.h>
#include <zcl/measurement/zigbeeclusterpressuremeasurement.h>
#include <zcl/security/zigbeeclusteriaszone.h>

#include <QDebug>

IntegrationPluginZigbeeLumi::IntegrationPluginZigbeeLumi():
    ZigbeeIntegrationPlugin(ZigbeeHardwareResource::HandlerTypeVendor, dcZigbeeLumi())
{
    m_networkUuidParamTypeIds[lumiHTSensorThingClassId] = lumiHTSensorThingNetworkUuidParamTypeId;
    m_networkUuidParamTypeIds[lumiButtonSensorThingClassId] = lumiButtonSensorThingNetworkUuidParamTypeId;
    m_networkUuidParamTypeIds[lumiLongpressButtonSensorThingClassId] = lumiLongpressButtonSensorThingNetworkUuidParamTypeId;
    m_networkUuidParamTypeIds[lumiMagnetSensorThingClassId] = lumiMagnetSensorThingNetworkUuidParamTypeId;
    m_networkUuidParamTypeIds[lumiMotionSensorThingClassId] = lumiMotionSensorThingNetworkUuidParamTypeId;
    m_networkUuidParamTypeIds[lumiWaterSensorThingClassId] = lumiWaterSensorThingNetworkUuidParamTypeId;
    m_networkUuidParamTypeIds[lumiWeatherSensorThingClassId] = lumiWeatherSensorThingNetworkUuidParamTypeId;
    m_networkUuidParamTypeIds[lumiVibrationSensorThingClassId] = lumiVibrationSensorThingNetworkUuidParamTypeId;
    m_networkUuidParamTypeIds[lumiPowerSocketThingClassId] = lumiPowerSocketThingNetworkUuidParamTypeId;
    m_networkUuidParamTypeIds[lumiRelayThingClassId] = lumiRelayThingNetworkUuidParamTypeId;
    m_networkUuidParamTypeIds[lumiRemoteThingClassId] = lumiRemoteThingNetworkUuidParamTypeId;

    m_zigbeeAddressParamTypeIds[lumiHTSensorThingClassId] = lumiHTSensorThingIeeeAddressParamTypeId;
    m_zigbeeAddressParamTypeIds[lumiButtonSensorThingClassId] = lumiButtonSensorThingIeeeAddressParamTypeId;
    m_zigbeeAddressParamTypeIds[lumiLongpressButtonSensorThingClassId] = lumiLongpressButtonSensorThingIeeeAddressParamTypeId;
    m_zigbeeAddressParamTypeIds[lumiMagnetSensorThingClassId] = lumiMagnetSensorThingIeeeAddressParamTypeId;
    m_zigbeeAddressParamTypeIds[lumiMotionSensorThingClassId] = lumiMotionSensorThingIeeeAddressParamTypeId;
    m_zigbeeAddressParamTypeIds[xiaomiMotionSensorThingClassId] = xiaomiMotionSensorThingIeeeAddressParamTypeId;
    m_zigbeeAddressParamTypeIds[lumiWaterSensorThingClassId] = lumiWaterSensorThingIeeeAddressParamTypeId;
    m_zigbeeAddressParamTypeIds[lumiWeatherSensorThingClassId] = lumiWeatherSensorThingIeeeAddressParamTypeId;
    m_zigbeeAddressParamTypeIds[lumiVibrationSensorThingClassId] = lumiVibrationSensorThingIeeeAddressParamTypeId;
    m_zigbeeAddressParamTypeIds[lumiPowerSocketThingClassId] = lumiPowerSocketThingIeeeAddressParamTypeId;
    m_zigbeeAddressParamTypeIds[lumiRelayThingClassId] = lumiRelayThingIeeeAddressParamTypeId;
    m_zigbeeAddressParamTypeIds[lumiRemoteThingClassId] = lumiRemoteThingIeeeAddressParamTypeId;


    // Known model identifier
    m_knownLumiDevices.insert("lumi.sensor_ht", lumiHTSensorThingClassId);
    m_knownLumiDevices.insert("lumi.sensor_magnet", lumiMagnetSensorThingClassId);
    m_knownLumiDevices.insert("lumi.sensor_switch", lumiButtonSensorThingClassId);
    // Check sensor_motion separate since the have the same name but different features
    //m_knownLumiDevices.insert("lumi.sensor_motion", lumiMotionSensorThingClassId);
    m_knownLumiDevices.insert("lumi.sensor_wleak", lumiWaterSensorThingClassId);
    m_knownLumiDevices.insert("lumi.weather", lumiWeatherSensorThingClassId);
    m_knownLumiDevices.insert("lumi.vibration", lumiVibrationSensorThingClassId);
    m_knownLumiDevices.insert("lumi.plug", lumiPowerSocketThingClassId);
    m_knownLumiDevices.insert("lumi.relay", lumiRelayThingClassId);
    m_knownLumiDevices.insert("lumi.remote", lumiRemoteThingClassId);
}

QString IntegrationPluginZigbeeLumi::name() const
{
    return "Lumi";
}

bool IntegrationPluginZigbeeLumi::handleNode(ZigbeeNode *node, const QUuid &networkUuid)
{
    // Check if this is Lumi
    // Note: Lumi / Xiaomi / Aquara devices are not in the specs, some older models do not
    // send the node descriptor or use a inconsistent manufacturer code. We use the model identifier
    // for verification since they seem to start always with lumi.
    foreach (ZigbeeNodeEndpoint *endpoint, node->endpoints()) {
        // Get the model identifier if present from the first endpoint. Also this is out of spec
        if (!endpoint->hasInputCluster(ZigbeeClusterLibrary::ClusterIdBasic)) {
            qCDebug(dcZigbeeLumi()) << "This lumi endpoint does not have the basic input cluster, so we skipp it" << endpoint;
            continue;
        }

        // Basic cluster exists, so we should have the model name
        if (!endpoint->modelIdentifier().toLower().startsWith("lumi.")) {
            return false;
        }

        ThingClassId thingClassId;
        if (endpoint->modelIdentifier().startsWith("lumi.sensor_motion")) {
            // Check if this is a xiaomi or aquara motion sensor
            if (endpoint->hasInputCluster(ZigbeeClusterLibrary::ClusterIdIlluminanceMeasurement)) {
                thingClassId = lumiMotionSensorThingClassId;
            } else {
                thingClassId = xiaomiMotionSensorThingClassId;
            }

        } if (endpoint->modelIdentifier() == "lumi.remote.b1acn01") {
            // This is the only lumi.remote.* which is actually a button (the same case as lumi.sensor_switch.aq2)
            // All the other lumi.remote.* are on/off rocker switches. So let's leave m_knownLumiDevices to map
            // lumi.remote to switches, just special handle it here. Once we have more of such special cases
            // which will sure arise, the m_knownLumiDevices mechanism probably needs a rework and use regular
            // expressions or force listing every single supported device one by one for precise mapping.. We'll see...
            // Also, this one supports longpress, unlike the regular lumi.button_sensor.aq2 and implements the multistate
            // input cluster like the remotes.. Seems this is a chip for a on/off switch remote shoved into the button case
            // and only connected one of the buttons...
            thingClassId = lumiLongpressButtonSensorThingClassId;

        } else {
            foreach (const QString &knownLumi, m_knownLumiDevices.keys()) {
                if (endpoint->modelIdentifier().startsWith(knownLumi)) {
                    thingClassId = m_knownLumiDevices.value(knownLumi);
                    break;
                }
            }
        }
        if (thingClassId.isNull()) {
            qCWarning(dcZigbeeLumi()) << "Unhandled Lumi device:" << endpoint->modelIdentifier();
            return false;
        }

        ThingDescriptor descriptor(thingClassId, supportedThings().findById(thingClassId).displayName());
        ParamList params;
        params << Param(m_networkUuidParamTypeIds.value(thingClassId), networkUuid.toString());
        params << Param(m_zigbeeAddressParamTypeIds.value(thingClassId), node->extendedAddress().toString());
        descriptor.setParams(params);
        emit autoThingsAppeared({descriptor});
        return true;
    }

    return false;
}

void IntegrationPluginZigbeeLumi::setupThing(ThingSetupInfo *info)
{
    Thing *thing = info->thing();
    if (!manageNode(thing)) {
        qCWarning(dcZigbeeLumi()) << "Failed to claim node during setup.";
        info->finish(Thing::ThingErrorHardwareNotAvailable);
        return;
    }

    ZigbeeNode *node = nodeForThing(thing);

    // Get the endpoint of interest (0x01) for this device
    ZigbeeNodeEndpoint *endpoint = node->getEndpoint(0x01);
    if (!endpoint) {
        qCWarning(dcZigbeeLumi()) << "Zigbee endpoint 1 not found on" << thing;
        info->finish(Thing::ThingErrorSetupFailed);
        return;
    }

    // Set the version
    thing->setStateValue("version", endpoint->softwareBuildId());

    // Thing specific setup
    if (thing->thingClassId() == lumiMagnetSensorThingClassId) {
        ZigbeeClusterOnOff *onOffCluster = endpoint->inputCluster<ZigbeeClusterOnOff>(ZigbeeClusterLibrary::ClusterIdOnOff);
        if (onOffCluster) {
            // Only set the state if the cluster actually has the attribute
            if (onOffCluster->hasAttribute(ZigbeeClusterOnOff::AttributeOnOff)) {
                thing->setStateValue(lumiMagnetSensorClosedStateTypeId, !onOffCluster->power());
            }

            connect(onOffCluster, &ZigbeeClusterOnOff::powerChanged, thing, [thing](bool power){
                qCDebug(dcZigbeeLumi()) << thing << "state changed" << (power ? "closed" : "open");
                thing->setStateValue(lumiMagnetSensorClosedStateTypeId, !power);
            });
        } else {
            qCDebug(dcZigbeeLumi()) << "Could not find the OnOff input cluster on" << thing->name() << endpoint;
            // The lumi.sensor_magnet does not reply to endpoint introspection so the OnOff cluster may not exist directly after
            // pairing yet. Once the sensor is actually opened/closed, it will create the cluster and we can connect to it.
            connect(endpoint, &ZigbeeNodeEndpoint::inputClusterAdded, thing, [thing](ZigbeeCluster *cluster){
                if (cluster->clusterId() == ZigbeeClusterLibrary::ClusterIdOnOff) {
                    qCDebug(dcZigbeeLumi()) << "OnOff cluster appeared on" << thing->name();
                    ZigbeeClusterOnOff *onOffCluster = qobject_cast<ZigbeeClusterOnOff*>(cluster);
                    if (onOffCluster->hasAttribute(ZigbeeClusterOnOff::AttributeOnOff)) {
                        thing->setStateValue(lumiMagnetSensorClosedStateTypeId, !onOffCluster->power());
                    }
                    connect(onOffCluster, &ZigbeeClusterOnOff::powerChanged, thing, [thing](bool power){
                        qCDebug(dcZigbeeLumi()) << thing << "state changed" << (power ? "closed" : "open");
                        thing->setStateValue(lumiMagnetSensorClosedStateTypeId, !power);
                    });
                }
            });
        }
    }

    if (thing->thingClassId() == lumiMotionSensorThingClassId) {
        ZigbeeClusterOccupancySensing *occupancyCluster = endpoint->inputCluster<ZigbeeClusterOccupancySensing>(ZigbeeClusterLibrary::ClusterIdOccupancySensing);
        if (occupancyCluster) {
            if (occupancyCluster->hasAttribute(ZigbeeClusterOccupancySensing::AttributeOccupancy)) {
                thing->setStateValue(lumiMotionSensorIsPresentStateTypeId, occupancyCluster->occupied());
                thing->setStateValue(lumiMotionSensorLastSeenTimeStateTypeId, QDateTime::currentMSecsSinceEpoch() / 1000);
            }

            connect(occupancyCluster, &ZigbeeClusterOccupancySensing::occupancyChanged, thing, [this, thing](bool occupancy){
                qCDebug(dcZigbeeLumi()) << "occupancy changed" << occupancy;
                // Only change the state if the it changed to true, it will be disabled by the timer
                if (occupancy) {
                    thing->setStateValue(lumiMotionSensorIsPresentStateTypeId, occupancy);
                    m_presenceTimer->start();
                }

                thing->setStateValue(lumiMotionSensorLastSeenTimeStateTypeId, QDateTime::currentMSecsSinceEpoch() / 1000);
            });

            if (!m_presenceTimer) {
                m_presenceTimer = hardwareManager()->pluginTimerManager()->registerTimer(1);
            }

            connect(m_presenceTimer, &PluginTimer::timeout, thing, [thing](){
                if (thing->stateValue(lumiMotionSensorIsPresentStateTypeId).toBool()) {
                    int timeout = thing->setting(lumiMotionSensorSettingsTimeoutParamTypeId).toInt();
                    QDateTime lastSeenTime = QDateTime::fromMSecsSinceEpoch(thing->stateValue(lumiMotionSensorLastSeenTimeStateTypeId).toULongLong() * 1000);
                    if (lastSeenTime.addSecs(timeout) < QDateTime::currentDateTime()) {
                        thing->setStateValue(lumiMotionSensorIsPresentStateTypeId, false);
                    }
                }
            });
        } else {
            qCWarning(dcZigbeeLumi()) << "Occupancy cluster not found on" << thing->name();
        }

        ZigbeeClusterIlluminanceMeasurement *illuminanceCluster = endpoint->inputCluster<ZigbeeClusterIlluminanceMeasurement>(ZigbeeClusterLibrary::ClusterIdIlluminanceMeasurement);
        if (illuminanceCluster) {
            // Only set the state if the cluster actually has the attribute
            if (illuminanceCluster->hasAttribute(ZigbeeClusterIlluminanceMeasurement::AttributeMeasuredValue)) {
                thing->setStateValue(lumiMotionSensorLightIntensityStateTypeId, illuminanceCluster->illuminance());
            }

            connect(illuminanceCluster, &ZigbeeClusterIlluminanceMeasurement::illuminanceChanged, thing, [thing](quint16 illuminance){
                qCDebug(dcZigbeeLumi()) << thing << "light intensity changed" << illuminance << "lux";
                thing->setStateValue(lumiMotionSensorLightIntensityStateTypeId, illuminance);
            });
        } else {
            qCWarning(dcZigbeeLumi()) << "Illuminance cluster not found on" << thing->name();
        }
    }

    if (thing->thingClassId() == xiaomiMotionSensorThingClassId) {
        ZigbeeClusterOccupancySensing *occupancyCluster = endpoint->inputCluster<ZigbeeClusterOccupancySensing>(ZigbeeClusterLibrary::ClusterIdOccupancySensing);
        if (occupancyCluster) {
            if (occupancyCluster->hasAttribute(ZigbeeClusterOccupancySensing::AttributeOccupancy)) {
                thing->setStateValue(xiaomiMotionSensorIsPresentStateTypeId, occupancyCluster->occupied());
                thing->setStateValue(xiaomiMotionSensorLastSeenTimeStateTypeId, QDateTime::currentMSecsSinceEpoch() / 1000);
            }

            connect(occupancyCluster, &ZigbeeClusterOccupancySensing::occupancyChanged, thing, [this, thing](bool occupancy){
                qCDebug(dcZigbeeLumi()) << "occupancy changed" << occupancy;
                // Only change the state if the it changed to true, it will be disabled by the timer
                if (occupancy) {
                    thing->setStateValue(xiaomiMotionSensorIsPresentStateTypeId, occupancy);
                    m_presenceTimer->start();
                }

                thing->setStateValue(xiaomiMotionSensorLastSeenTimeStateTypeId, QDateTime::currentMSecsSinceEpoch() / 1000);
            });

            if (!m_presenceTimer) {
                m_presenceTimer = hardwareManager()->pluginTimerManager()->registerTimer(1);
            }

            connect(m_presenceTimer, &PluginTimer::timeout, thing, [thing](){
                if (thing->stateValue(xiaomiMotionSensorIsPresentStateTypeId).toBool()) {
                    int timeout = thing->setting(xiaomiMotionSensorSettingsTimeoutParamTypeId).toInt();
                    QDateTime lastSeenTime = QDateTime::fromMSecsSinceEpoch(thing->stateValue(xiaomiMotionSensorLastSeenTimeStateTypeId).toULongLong() * 1000);
                    if (lastSeenTime.addSecs(timeout) < QDateTime::currentDateTime()) {
                        thing->setStateValue(xiaomiMotionSensorIsPresentStateTypeId, false);
                    }
                }
            });
        } else {
            qCWarning(dcZigbeeLumi()) << "Occupancy cluster not found on" << thing->name();
        }
    }

    if (thing->thingClassId() == lumiHTSensorThingClassId) {
        connectToTemperatureMeasurementInputCluster(thing, endpoint);
        connectToRelativeHumidityMeasurementInputCluster(thing, endpoint);
    }

    if (thing->thingClassId() == lumiWeatherSensorThingClassId) {
        connectToTemperatureMeasurementInputCluster(thing, endpoint);
        connectToRelativeHumidityMeasurementInputCluster(thing, endpoint);

        ZigbeeClusterPressureMeasurement *pressureCluster = endpoint->inputCluster<ZigbeeClusterPressureMeasurement>(ZigbeeClusterLibrary::ClusterIdPressureMeasurement);
        if (pressureCluster) {
            // Only set the state if the cluster actually has the attribute
            if (pressureCluster->hasAttribute(ZigbeeClusterPressureMeasurement::AttributeMeasuredValue)) {
                thing->setStateValue(lumiWeatherSensorPressureStateTypeId, pressureCluster->pressure() * 10);
            }

            connect(pressureCluster, &ZigbeeClusterPressureMeasurement::pressureChanged, thing, [thing](double pressure){
                thing->setStateValue(lumiWeatherSensorPressureStateTypeId, pressure * 10);
            });
        } else {
            qCWarning(dcZigbeeLumi()) << "Could not find the pressure measurement server cluster on" << thing << endpoint;
        }
    }

    if (thing->thingClassId() == lumiWaterSensorThingClassId) {
        connect(endpoint, &ZigbeeNodeEndpoint::clusterAttributeChanged, this, [thing](ZigbeeCluster *cluster, const ZigbeeClusterAttribute &attribute){
            if (cluster->clusterId() == ZigbeeClusterLibrary::ClusterIdIasZone) {
                if (attribute.id() == ZigbeeClusterIasZone::AttributeZoneState) {
                    bool valueOk = false;
                    ZigbeeClusterIasZone::ZoneStatusFlags zoneStatus = static_cast<ZigbeeClusterIasZone::ZoneStatusFlags>(attribute.dataType().toUInt16(&valueOk));
                    if (!valueOk) {
                        qCWarning(dcZigbeeLumi()) << thing << "failed to convert attribute data to uint16 flag. Not updating the states from" << attribute;
                    } else {
                        qCDebug(dcZigbeeLumi()) << thing << "zone status changed" << zoneStatus;

                        // Water detected gets indicated in the Alarm1 flag
                        if (zoneStatus.testFlag(ZigbeeClusterIasZone::ZoneStatusAlarm1)) {
                            thing->setStateValue(lumiWaterSensorWaterDetectedStateTypeId, true);
                        } else {
                            thing->setStateValue(lumiWaterSensorWaterDetectedStateTypeId, false);
                        }

                        // Battery alarm
                        if (zoneStatus.testFlag(ZigbeeClusterIasZone::ZoneStatusBattery)) {
                            thing->setStateValue(lumiWaterSensorBatteryCriticalStateTypeId, true);
                        } else {
                            thing->setStateValue(lumiWaterSensorBatteryCriticalStateTypeId, false);
                        }
                    }
                }
            }
        });
    }

    if (thing->thingClassId() == lumiVibrationSensorThingClassId) {
        connect(endpoint, &ZigbeeNodeEndpoint::clusterAttributeChanged, this, [this, thing](ZigbeeCluster *cluster, const ZigbeeClusterAttribute &attribute){
            if (cluster->clusterId() == ZigbeeClusterLibrary::ClusterIdDoorLock) {
                // Note: shoehow the vibration sensor is using the door lock cluster, with undocumented attribitues.
                // This device is completly out of spec, so we just recognize the vibration trough tests and it looks like
                // attribute id 85 is the indicator for vibration. The payload contains an unsigned int, but not sure what it indicates yet

                if (attribute.id() == 85) {
                    bool valueOk = false;
                    quint16 value = attribute.dataType().toUInt16(&valueOk);
                    if (!valueOk) {
                        qCWarning(dcZigbeeLumi()) << thing << "failed to convert attribute data to uint16." << attribute;
                    } else {
                        qCDebug(dcZigbeeLumi()) << thing << "vibration attribute changed" << value;
                        emitEvent(Event(lumiVibrationSensorVibrationDetectedEventTypeId, thing->id()));
                    }
                }
            }
        });
    }

    if (thing->thingClassId() == lumiButtonSensorThingClassId) {
        ZigbeeClusterOnOff *onOffCluster = endpoint->inputCluster<ZigbeeClusterOnOff>(ZigbeeClusterLibrary::ClusterIdOnOff);
        if (onOffCluster) {
            connect(onOffCluster, &ZigbeeClusterOnOff::attributeChanged, thing, [thing](const ZigbeeClusterAttribute &attribute){
                qCDebug(dcZigbeeLumi()) << thing->name() << "Attribute changed:" << attribute;
                if (attribute.id() == ZigbeeClusterOnOff::AttributeOnOff && attribute.dataType().toUInt8() == 0x01) {
                    thing->emitEvent(lumiButtonSensorPressedEventTypeId, {Param(lumiButtonSensorPressedEventButtonNameParamTypeId, 1)});
                } else if (attribute.id() == 0x8000) {
                    quint8 count = attribute.dataType().toUInt8();
                    thing->emitEvent(lumiButtonSensorPressedEventTypeId, {Param(lumiButtonSensorPressedEventButtonNameParamTypeId, count)});
                }
            });
        } else {
            qCWarning(dcZigbeeLumi()) << "Could not find the OnOff input cluster on" << thing << endpoint;
        }
    }

    if (thing->thingClassId() == lumiLongpressButtonSensorThingClassId) {
        ZigbeeClusterMultistateInput *multistateInputCluster = endpoint->inputCluster<ZigbeeClusterMultistateInput>(ZigbeeClusterLibrary::ClusterIdMultistateInput);
        connect(multistateInputCluster, &ZigbeeClusterMultistateInput::attributeChanged, thing, [thing](const ZigbeeClusterAttribute &attribute){
            qCDebug(dcZigbeeLumi()) << thing->name() << "Attribute changed:" << attribute;
            if (attribute.id() == ZigbeeClusterMultistateInput::AttributePresentValue) {
                quint16 value = attribute.dataType().toUInt16();
                switch (value) {
                case 0:
                    thing->emitEvent("longPressed", {Param(lumiLongpressButtonSensorLongPressedEventButtonNameParamTypeId, "1")});
                    break;
                case 1:
                    thing->emitEvent("pressed", {Param(lumiLongpressButtonSensorPressedEventButtonNameParamTypeId, "1")});
                    break;
                case 2:
                    thing->emitEvent("pressed", {Param(lumiLongpressButtonSensorPressedEventButtonNameParamTypeId, "2")});
                    break;
                // 0xff would be released, but nymeas longpressbutton interface doesn't use that
                }
            }
        });
    }

    if (thing->thingClassId() == lumiPowerSocketThingClassId) {

        connectToOtaOutputCluster(thing, endpoint);
        connectToOnOffInputCluster(thing, endpoint);

        if (node->hasEndpoint(0x02)) {
            connectToAnalogInputCluster(thing, node->getEndpoint(0x02), "currentPower");
        }
        if (node->hasEndpoint(0x03)) {
            connectToAnalogInputCluster(thing, node->getEndpoint(0x03), "totalEnergyConsumed");
        }
    }

    if (thing->thingClassId() == lumiRemoteThingClassId) {
        // Since we are here again out of spec, we just can react on cluster and endpoint signals
        connect(node, &ZigbeeNode::endpointClusterAttributeChanged, thing, [this, thing](ZigbeeNodeEndpoint *endpoint, ZigbeeCluster *cluster, const ZigbeeClusterAttribute &attribute){
            switch (endpoint->endpointId()) {
            case 0x01:
                if (cluster->clusterId() == ZigbeeClusterLibrary::ClusterIdMultistateInput && attribute.id() == ZigbeeClusterMultistateInput::AttributePresentValue) {
                    quint16 value = attribute.dataType().toUInt16();
                    if (value == 1) {
                        emit emitEvent(Event(lumiRemotePressedEventTypeId, thing->id(), ParamList() << Param(lumiRemotePressedEventButtonNameParamTypeId, "1")));
                    } else {
                        emit emitEvent(Event(lumiRemoteLongPressedEventTypeId, thing->id(), ParamList() << Param(lumiRemoteLongPressedEventButtonNameParamTypeId, "1")));
                    }
                }
                break;
            case 0x02:
                if (cluster->clusterId() == ZigbeeClusterLibrary::ClusterIdMultistateInput && attribute.id() == ZigbeeClusterMultistateInput::AttributePresentValue) {
                    quint16 value = attribute.dataType().toUInt16();
                    if (value == 1) {
                        emit emitEvent(Event(lumiRemotePressedEventTypeId, thing->id(), ParamList() << Param(lumiRemotePressedEventButtonNameParamTypeId, "2")));
                    } else {
                        emit emitEvent(Event(lumiRemoteLongPressedEventTypeId, thing->id(), ParamList() << Param(lumiRemoteLongPressedEventButtonNameParamTypeId, "2")));
                    }
                }
                break;
            case 0x03:
                if (cluster->clusterId() == ZigbeeClusterLibrary::ClusterIdMultistateInput && attribute.id() == ZigbeeClusterMultistateInput::AttributePresentValue) {
                    quint16 value = attribute.dataType().toUInt16();
                    if (value == 1) {
                        emit emitEvent(Event(lumiRemotePressedEventTypeId, thing->id(), ParamList() << Param(lumiRemotePressedEventButtonNameParamTypeId, "1+2")));
                    } else {
                        emit emitEvent(Event(lumiRemoteLongPressedEventTypeId, thing->id(), ParamList() << Param(lumiRemoteLongPressedEventButtonNameParamTypeId, "1+2")));
                    }
                }
                break;
            default:
                qCWarning(dcZigbeeLumi()) << "Received attribute changed signal from unhandled endpoint" << thing << endpoint << cluster << attribute;
                break;
            }
        });
    }

    if (thing->thingClassId() == lumiRelayThingClassId) {
        // Get the 2 endpoints
        ZigbeeNodeEndpoint *endpoint1 = node->getEndpoint(0x01);
        if (endpoint1) {
            connectToOnOffInputCluster(thing, endpoint1, "relay1");
        } else {
            qCWarning(dcZigbeeLumi()) << "Could not find endpoint 1 on" << thing << node;
        }

        ZigbeeNodeEndpoint *endpoint2 = node->getEndpoint(0x02);
        if (endpoint2) {
            connectToOnOffInputCluster(thing, endpoint2, "relay2");
        } else {
            qCWarning(dcZigbeeLumi()) << "Could not find endpoint 2 on" << thing << node;
        }
    }

    info->finish(Thing::ThingErrorNoError);
}

void IntegrationPluginZigbeeLumi::executeAction(ThingActionInfo *info)
{
    Thing *thing = info->thing();
    ZigbeeNode *node = nodeForThing(info->thing());

    if (thing->thingClassId() == lumiPowerSocketThingClassId) {
        ZigbeeNodeEndpoint *endpoint = node->getEndpoint(0x01);
        if (!endpoint) {
            qCWarning(dcZigbeeLumi()) << "Unable to get the endpoint from node" << node << "for" << thing;
            info->finish(Thing::ThingErrorSetupFailed);
            return;
        }

        if (info->action().actionTypeId() == lumiPowerSocketPerformUpdateActionTypeId) {
            enableFirmwareUpdate(thing);
            executeImageNotifyOtaOutputCluster(info, endpoint);
            return;
        }

        if (info->action().actionTypeId() == lumiPowerSocketPowerActionTypeId) {
            executePowerOnOffInputCluster(info, endpoint);
            return;
        }

        if (info->action().actionTypeId() == lumiPowerSocketAlertActionTypeId) {
            executeIdentifyIdentifyInputCluster(info, endpoint);
            return;
        }
    }

    if (thing->thingClassId() == lumiRelayThingClassId) {

        if (info->action().actionTypeId() == lumiRelayRelay1ActionTypeId) {
            ZigbeeNodeEndpoint *endpoint = node->getEndpoint(0x01);
            if (!endpoint) {
                qCWarning(dcZigbeeLumi()) << "Unable to get the endpoint from node" << node << "for" << thing;
                info->finish(Thing::ThingErrorSetupFailed);
                return;
            }

            executePowerOnOffInputCluster(info, endpoint);
            return;
        }

        if (info->action().actionTypeId() == lumiRelayRelay2ActionTypeId) {
            ZigbeeNodeEndpoint *endpoint = node->getEndpoint(0x02);
            if (!endpoint) {
                qCWarning(dcZigbeeLumi()) << "Unable to get the endpoint from node" << node << "for" << thing;
                info->finish(Thing::ThingErrorSetupFailed);
                return;
            }

            executePowerOnOffInputCluster(info, endpoint);
        }
    }

    info->finish(Thing::ThingErrorUnsupportedFeature);
}
