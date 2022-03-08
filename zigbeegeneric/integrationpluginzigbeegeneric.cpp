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

#include "integrationpluginzigbeegeneric.h"
#include "plugininfo.h"
#include "hardware/zigbee/zigbeehardwareresource.h"

#include "zcl/hvac/zigbeeclusterthermostat.h"

#include <QDebug>

IntegrationPluginZigbeeGeneric::IntegrationPluginZigbeeGeneric()
{
}

QString IntegrationPluginZigbeeGeneric::name() const
{
    return "Generic";
}

bool IntegrationPluginZigbeeGeneric::handleNode(ZigbeeNode *node, const QUuid &/*networkUuid*/)
{
    bool handled = false;
    foreach (ZigbeeNodeEndpoint *endpoint, node->endpoints()) {
        qCDebug(dcZigbeeGeneric()) << "Checking node endpoint:" << endpoint->endpointId() << endpoint->deviceId();

        // Check thermostat
        if (endpoint->profile() == Zigbee::ZigbeeProfile::ZigbeeProfileHomeAutomation &&
                endpoint->deviceId() == Zigbee::HomeAutomationDeviceThermostat) {
            qCDebug(dcZigbeeGeneric()) << "Handling thermostat endpoint for" << node << endpoint;
            createThing(thermostatThingClassId, node, {Param(thermostatThingEndpointIdParamTypeId, endpoint->endpointId())});
            bindPowerConfigurationCluster(node, endpoint);
            bindThermostatCluster(node, endpoint);
            handled = true;
        }

        // Check on/off thing
        if ((endpoint->profile() == Zigbee::ZigbeeProfile::ZigbeeProfileLightLink && endpoint->deviceId() == Zigbee::LightLinkDevice::LightLinkDeviceOnOffPlugin) ||
                (endpoint->profile() == Zigbee::ZigbeeProfile::ZigbeeProfileHomeAutomation && endpoint->deviceId() == Zigbee::HomeAutomationDeviceOnOffPlugin) ||
                (endpoint->profile() == Zigbee::ZigbeeProfile::ZigbeeProfileHomeAutomation && endpoint->deviceId() == Zigbee::HomeAutomationDeviceMainPowerOutlet) ||
                (endpoint->profile() == Zigbee::ZigbeeProfile::ZigbeeProfileHomeAutomation && endpoint->deviceId() == Zigbee::HomeAutomationDeviceSmartPlug)) {

            // Simple on/off device
            if (endpoint->hasInputCluster(ZigbeeClusterLibrary::ClusterIdOnOff)) {

                if (endpoint->hasInputCluster(ZigbeeClusterLibrary::ClusterIdMetering)) {
                    qCDebug(dcZigbeeGeneric()) << "Handling power socket with energy metering for" << node << endpoint;
                    createThing(powerMeterSocketThingClassId, node, {Param(powerMeterSocketThingEndpointIdParamTypeId, endpoint->endpointId())});
                    bindMeteringCluster(endpoint);

                } else {
                    qCDebug(dcZigbeeGeneric()) << "Handling power socket endpoint for" << node << endpoint;
                    createThing(powerSocketThingClassId, node, {Param(powerSocketThingEndpointIdParamTypeId, endpoint->endpointId())});
                }

                bindOnOffCluster(node, endpoint);
                handled = true;
            }
        }

        // Check door lock
        if (endpoint->profile() == Zigbee::ZigbeeProfile::ZigbeeProfileHomeAutomation && endpoint->deviceId() == Zigbee::HomeAutomationDeviceDoorLock) {
            if (!endpoint->hasInputCluster(ZigbeeClusterLibrary::ClusterIdPowerConfiguration) ||
                    !endpoint->hasInputCluster(ZigbeeClusterLibrary::ClusterIdDoorLock)) {
                qCWarning(dcZigbeeGeneric()) << "Endpoint claims to be a door lock, but the appropriate input clusters could not be found" << node << endpoint;
            } else {
                qCDebug(dcZigbeeGeneric()) << "Handling door lock endpoint for" << node << endpoint;
                createThing(doorLockThingClassId, node, {Param(doorLockThingEndpointIdParamTypeId, endpoint->endpointId())});
                // Initialize bindings and cluster attributes
                initDoorLock(node, endpoint);
                handled = true;
            }
        }

        // Security sensors
        if (endpoint->profile() == Zigbee::ZigbeeProfile::ZigbeeProfileHomeAutomation && endpoint->deviceId() == Zigbee::HomeAutomationDeviceIasZone) {
            qCInfo(dcZigbeeGeneric()) << "IAS Zone device found!";
            // We need to read the Type cluster to determine what this actually is...
            ZigbeeClusterIasZone *iasZoneCluster = endpoint->inputCluster<ZigbeeClusterIasZone>(ZigbeeClusterLibrary::ClusterIdIasZone);
            ZigbeeClusterReply *reply = iasZoneCluster->readAttributes({ZigbeeClusterIasZone::AttributeZoneType});
            connect(reply, &ZigbeeClusterReply::finished, this, [=](){
                if (reply->error() != ZigbeeClusterReply::ErrorNoError) {
                    qCWarning(dcZigbeeGeneric()) << "Reading IAS Zone type attribute finished with error" << reply->error();
                    return;
                }

                QList<ZigbeeClusterLibrary::ReadAttributeStatusRecord> attributeStatusRecords = ZigbeeClusterLibrary::parseAttributeStatusRecords(reply->responseFrame().payload);
                if (attributeStatusRecords.count() != 1 || attributeStatusRecords.first().attributeId != ZigbeeClusterIasZone::AttributeZoneType) {
                    qCWarning(dcZigbeeGeneric()) << "Unexpected reply in reading IAS Zone device type:" << attributeStatusRecords;
                    return;
                }

                initIASSensor(node, endpoint);

                ZigbeeClusterLibrary::ReadAttributeStatusRecord iasZoneTypeRecord = attributeStatusRecords.first();
                qCDebug(dcZigbeeGeneric()) << "IAS Zone device type:" << iasZoneTypeRecord.dataType.toUInt16();
                switch (iasZoneTypeRecord.dataType.toUInt16()) {
                case ZigbeeClusterIasZone::ZoneTypeContactSwitch:
                    qCInfo(dcZigbeeGeneric()) << "Creating contact switch thing";
                    createThing(doorSensorThingClassId, node, {Param(doorSensorThingEndpointIdParamTypeId, endpoint->endpointId())});
                    break;
                case ZigbeeClusterIasZone::ZoneTypeMotionSensor:
                    qCInfo(dcZigbeeGeneric()) << "Creating motion sensor thing";
                    createThing(motionSensorThingClassId, node, {Param(motionSensorThingEndpointIdParamTypeId, endpoint->endpointId())});
                    break;
                default:
                    qCWarning(dcZigbeeGeneric()) << "Unhandled IAS Zone device type:" << "0x" + QString::number(iasZoneTypeRecord.dataType.toUInt16(), 16);

                }

            });

            handled = true;
        }
    }

    return handled;
}

void IntegrationPluginZigbeeGeneric::init()
{
    hardwareManager()->zigbeeResource()->registerHandler(this, ZigbeeHardwareResource::HandlerTypeCatchAll);
}

void IntegrationPluginZigbeeGeneric::setupThing(ThingSetupInfo *info)
{
    Thing *thing = info->thing();

    if (!manageNode(thing)) {
        qCWarning(dcZigbeeGeneric()) << "Failed to claim node during setup.";
        info->finish(Thing::ThingErrorHardwareNotAvailable);
        return;
    }

    ZigbeeNodeEndpoint *endpoint = findEndpoint(thing);
    if (!endpoint) {
        qCWarning(dcZigbeeGeneric()) << "Could not find endpoint for" << thing;
        info->finish(Thing::ThingErrorSetupFailed);
        return;
    }


    // Set the version
    thing->setStateValue("version", endpoint->softwareBuildId());

    if (thing->hasState("batteryLevel")) {
        connectToPowerConfigurationCluster(thing, endpoint);
    }

    // Type specific setup
    if (thing->thingClassId() == thermostatThingClassId) {
        connectToThermostatCluster(thing, endpoint);
    }

    if (thing->thingClassId() == powerSocketThingClassId) {
        connectToOnOffCluster(thing, endpoint);
    }

    if (thing->thingClassId() == powerMeterSocketThingClassId) {
        connectToOnOffCluster(thing, endpoint);
        connectToMeteringCluster(thing, endpoint);
    }

    if (thing->thingClassId() == doorLockThingClassId) {

        // Get door state changes
        ZigbeeClusterDoorLock *doorLockCluster = endpoint->inputCluster<ZigbeeClusterDoorLock>(ZigbeeClusterLibrary::ClusterIdDoorLock);
        if (!doorLockCluster) {
            qCWarning(dcZigbeeGeneric()) << "Could not find door lock cluster on" << thing << endpoint;
        } else {
            // Only set the initial state if the attribute already exists
            if (doorLockCluster->hasAttribute(ZigbeeClusterDoorLock::AttributeDoorState)) {
                qCDebug(dcZigbeeGeneric()) << thing << doorLockCluster->doorState();
                // TODO: check if we can use smart lock and set appropriate state
            }

            connect(doorLockCluster, &ZigbeeClusterDoorLock::lockStateChanged, thing, [=](ZigbeeClusterDoorLock::LockState lockState){
                qCDebug(dcZigbeeGeneric()) << thing << "lock state changed" << lockState;
                // TODO: check if we can use smart lock and set appropriate state
            });
        }
    }

    if (thing->thingClassId() == doorSensorThingClassId) {
        ZigbeeClusterIasZone *iasZoneCluster = endpoint->inputCluster<ZigbeeClusterIasZone>(ZigbeeClusterLibrary::ClusterIdIasZone);
        if (!iasZoneCluster) {
            qCWarning(dcZigbeeGeneric()) << "Could not find IAS zone cluster on" << thing << endpoint;
        } else {
            if (iasZoneCluster->hasAttribute(ZigbeeClusterIasZone::AttributeZoneStatus)) {
                qCDebug(dcZigbeeGeneric()) << thing << iasZoneCluster->zoneStatus();
                ZigbeeClusterIasZone::ZoneStatusFlags zoneStatus = iasZoneCluster->zoneStatus();
                thing->setStateValue(doorSensorClosedStateTypeId, !zoneStatus.testFlag(ZigbeeClusterIasZone::ZoneStatusAlarm1) && !zoneStatus.testFlag(ZigbeeClusterIasZone::ZoneStatusAlarm2));
            }
            connect(iasZoneCluster, &ZigbeeClusterIasZone::zoneStatusChanged, thing, [=](ZigbeeClusterIasZone::ZoneStatusFlags zoneStatus, quint8 extendedStatus, quint8 zoneId, quint16 delays) {
                qCDebug(dcZigbeeGeneric()) << "Zone status changed to:" << zoneStatus << extendedStatus << zoneId << delays;
                thing->setStateValue(doorSensorClosedStateTypeId, !zoneStatus.testFlag(ZigbeeClusterIasZone::ZoneStatusAlarm1) && !zoneStatus.testFlag(ZigbeeClusterIasZone::ZoneStatusAlarm2));
            });
        }
    }

    if (thing->thingClassId() == motionSensorThingClassId) {
        qCDebug(dcZigbeeGeneric()) << "Setting up motion sensor" << endpoint->endpointId();;
        ZigbeeClusterIasZone *iasZoneCluster = endpoint->inputCluster<ZigbeeClusterIasZone>(ZigbeeClusterLibrary::ClusterIdIasZone);
        if (!iasZoneCluster) {
            qCWarning(dcZigbeeGeneric()) << "Could not find IAS zone cluster on" << thing << endpoint;
        } else {
            qCDebug(dcZigbeeGeneric()) << "Cluster attributes:" << iasZoneCluster->attributes();
            qCDebug(dcZigbeeGeneric()) << "Zone state:" << thing->name() << iasZoneCluster->zoneState();
            qCDebug(dcZigbeeGeneric()) << "Zone type:" << thing->name() << iasZoneCluster->zoneType();
            qCDebug(dcZigbeeGeneric()) << "Zone status:" << thing->name() << iasZoneCluster->zoneStatus();
            if (iasZoneCluster->hasAttribute(ZigbeeClusterIasZone::AttributeZoneStatus)) {
                ZigbeeClusterIasZone::ZoneStatusFlags zoneStatus = iasZoneCluster->zoneStatus();
                thing->setStateValue(motionSensorIsPresentStateTypeId, zoneStatus.testFlag(ZigbeeClusterIasZone::ZoneStatusAlarm1) || zoneStatus.testFlag(ZigbeeClusterIasZone::ZoneStatusAlarm2));
                thing->setStateValue(motionSensorTamperedStateTypeId, zoneStatus.testFlag(ZigbeeClusterIasZone::ZoneStatusTamper));
            }
            connect(iasZoneCluster, &ZigbeeClusterIasZone::zoneStatusChanged, thing, [=](ZigbeeClusterIasZone::ZoneStatusFlags zoneStatus, quint8 extendedStatus, quint8 zoneId, quint16 delays) {
                qCDebug(dcZigbeeGeneric()) << "Zone status changed to:" << zoneStatus << extendedStatus << zoneId << delays;
                thing->setStateValue(motionSensorIsPresentStateTypeId, zoneStatus.testFlag(ZigbeeClusterIasZone::ZoneStatusAlarm1) || zoneStatus.testFlag(ZigbeeClusterIasZone::ZoneStatusAlarm2));
                thing->setStateValue(motionSensorTamperedStateTypeId, zoneStatus.testFlag(ZigbeeClusterIasZone::ZoneStatusTamper));
            });
        }
    }

    info->finish(Thing::ThingErrorNoError);
}

void IntegrationPluginZigbeeGeneric::executeAction(ThingActionInfo *info)
{
    if (!hardwareManager()->zigbeeResource()->available()) {
        info->finish(Thing::ThingErrorHardwareNotAvailable);
        return;
    }

    // Get the node
    Thing *thing = info->thing();
    ZigbeeNode *node = nodeForThing(info->thing());
    if (!node->reachable()) {
        info->finish(Thing::ThingErrorHardwareNotAvailable);
        return;
    }

    // Get the endpoint
    ZigbeeNodeEndpoint *endpoint = findEndpoint(thing);
    if (!endpoint) {
        info->finish(Thing::ThingErrorHardwareNotAvailable);
        return;
    }

    if (thing->thingClassId() == thermostatThingClassId) {
        if (info->action().actionTypeId() == thermostatTargetTemperatureActionTypeId) {
            ZigbeeClusterThermostat *thermostatCluster = endpoint->inputCluster<ZigbeeClusterThermostat>(ZigbeeClusterLibrary::ClusterIdThermostat);
            if (!thermostatCluster) {
                qCWarning(dcZigbeeGeneric()) << "Thermostat cluster not found on thing" << thing->name();
                info->finish(Thing::ThingErrorHardwareFailure);
                return;
            }
            qint16 targetTemp = qRound(info->action().paramValue(thermostatTargetTemperatureStateTypeId).toDouble() * 10) * 10;
            ZigbeeClusterReply *reply = thermostatCluster->setOccupiedHeatingSetpoint(targetTemp);
            connect(reply, &ZigbeeClusterReply::finished, info, [info, reply](){
                if (reply->error() != ZigbeeClusterReply::ErrorNoError) {
                    qCWarning(dcZigbeeGeneric()) << "Error setting target temperture:" << reply->error();
                    info->finish(Thing::ThingErrorHardwareFailure);
                    return;
                }
                info->thing()->setStateValue(thermostatTargetTemperatureStateTypeId, info->action().paramValue(thermostatTargetTemperatureActionTargetTemperatureParamTypeId));
                info->finish(Thing::ThingErrorNoError);
            });
            return;
        }
    }

    if (thing->thingClassId() == powerSocketThingClassId) {
        if (info->action().actionTypeId() == powerSocketAlertActionTypeId) {
            ZigbeeClusterIdentify *identifyCluster = endpoint->inputCluster<ZigbeeClusterIdentify>(ZigbeeClusterLibrary::ClusterIdIdentify);
            if (!identifyCluster) {
                qCWarning(dcZigbeeGeneric()) << "Could not find identify cluster for" << thing << "in" << node;
                info->finish(Thing::ThingErrorHardwareFailure);
                return;
            }

            ZigbeeClusterReply *reply = identifyCluster->identify(2);
            connect(reply, &ZigbeeClusterReply::finished, info, [reply, info](){
                info->finish(reply->error() == ZigbeeClusterReply::ErrorNoError ? Thing::ThingErrorNoError : Thing::ThingErrorHardwareFailure);
            });
            return;
        }

        if (info->action().actionTypeId() == powerSocketPowerActionTypeId) {
            ZigbeeClusterOnOff *onOffCluster = endpoint->inputCluster<ZigbeeClusterOnOff>(ZigbeeClusterLibrary::ClusterIdOnOff);
            if (!onOffCluster) {
                qCWarning(dcZigbeeGeneric()) << "Could not find on/off cluster for" << thing << "in" << endpoint;
                info->finish(Thing::ThingErrorHardwareFailure);
                return;
            }

            bool power = info->action().param(powerSocketPowerActionPowerParamTypeId).value().toBool();
            ZigbeeClusterReply *reply = (power ? onOffCluster->commandOn() : onOffCluster->commandOff());
            connect(reply, &ZigbeeClusterReply::finished, info, [=](){
                if (reply->error() == ZigbeeClusterReply::ErrorNoError) {
                    thing->setStateValue(powerSocketPowerStateTypeId, power);
                }
                info->finish(reply->error() == ZigbeeClusterReply::ErrorNoError ? Thing::ThingErrorNoError : Thing::ThingErrorHardwareFailure);
            });
            return;
        }
    }

    if (thing->thingClassId() == powerMeterSocketThingClassId) {
        if (info->action().actionTypeId() == powerMeterSocketAlertActionTypeId) {
            ZigbeeClusterIdentify *identifyCluster = endpoint->inputCluster<ZigbeeClusterIdentify>(ZigbeeClusterLibrary::ClusterIdIdentify);
            if (!identifyCluster) {
                qCWarning(dcZigbeeGeneric()) << "Could not find identify cluster for" << thing << "in" << node;
                info->finish(Thing::ThingErrorHardwareFailure);
                return;
            }

            ZigbeeClusterReply *reply = identifyCluster->identify(2);
            connect(reply, &ZigbeeClusterReply::finished, info, [reply, info](){
                info->finish(reply->error() == ZigbeeClusterReply::ErrorNoError ? Thing::ThingErrorNoError : Thing::ThingErrorHardwareFailure);
            });
            return;
        }

        if (info->action().actionTypeId() == powerMeterSocketPowerActionTypeId) {
            ZigbeeClusterOnOff *onOffCluster = endpoint->inputCluster<ZigbeeClusterOnOff>(ZigbeeClusterLibrary::ClusterIdOnOff);
            if (!onOffCluster) {
                qCWarning(dcZigbeeGeneric()) << "Could not find on/off cluster for" << thing << "in" << endpoint;
                info->finish(Thing::ThingErrorHardwareFailure);
                return;
            }

            bool power = info->action().param(powerMeterSocketPowerActionPowerParamTypeId).value().toBool();
            ZigbeeClusterReply *reply = (power ? onOffCluster->commandOn() : onOffCluster->commandOff());
            connect(reply, &ZigbeeClusterReply::finished, info, [=](){
                info->finish(reply->error() == ZigbeeClusterReply::ErrorNoError ? Thing::ThingErrorNoError : Thing::ThingErrorHardwareFailure);
            });
            connect(reply, &ZigbeeClusterReply::finished, thing, [=](){
                if (reply->error() == ZigbeeClusterReply::ErrorNoError) {
                    thing->setStateValue(powerMeterSocketPowerStateTypeId, power);
                }
            });
            return;
        }
    }

    if (thing->thingClassId() == doorLockThingClassId) {
        if (info->action().actionTypeId() == doorLockOpenActionTypeId) {
            ZigbeeClusterDoorLock *doorLockCluster = endpoint->inputCluster<ZigbeeClusterDoorLock>(ZigbeeClusterLibrary::ClusterIdDoorLock);
            if (!doorLockCluster) {
                qCWarning(dcZigbeeGeneric()) << "Could not find door lock cluster for" << thing << "in" << node;
                info->finish(Thing::ThingErrorHardwareFailure);
                return;
            }

            // Send the command trough the network
            ZigbeeClusterReply *reply = doorLockCluster->unlockDoor();
            connect(reply, &ZigbeeClusterReply::finished, this, [reply, info](){
                // Note: reply will be deleted automatically
                if (reply->error() != ZigbeeClusterReply::ErrorNoError) {
                    info->finish(Thing::ThingErrorHardwareFailure);
                } else {
                    info->finish(Thing::ThingErrorNoError);
                }
            });
            return;
        }

        if (info->action().actionTypeId() == doorLockCloseActionTypeId) {
            ZigbeeClusterDoorLock *doorLockCluster = endpoint->inputCluster<ZigbeeClusterDoorLock>(ZigbeeClusterLibrary::ClusterIdDoorLock);
            if (!doorLockCluster) {
                qCWarning(dcZigbeeGeneric()) << "Could not find door lock cluster for" << thing << "in" << node;
                info->finish(Thing::ThingErrorHardwareFailure);
                return;
            }

            // Send the command trough the network
            ZigbeeClusterReply *reply = doorLockCluster->lockDoor();
            connect(reply, &ZigbeeClusterReply::finished, this, [reply, info](){
                // Note: reply will be deleted automatically
                if (reply->error() != ZigbeeClusterReply::ErrorNoError) {
                    info->finish(Thing::ThingErrorHardwareFailure);
                } else {
                    info->finish(Thing::ThingErrorNoError);
                }
            });
            return;
        }
    }

    info->finish(Thing::ThingErrorUnsupportedFeature);
}

ZigbeeNodeEndpoint *IntegrationPluginZigbeeGeneric::findEndpoint(Thing *thing)
{
    ZigbeeNode *node = nodeForThing(thing);
    if (!node) {
        qCWarning(dcZigbeeGeneric()) << "Could not find the node for" << thing;
        return nullptr;
    }

    quint8 endpointId = thing->paramValue("endpointId").toUInt();
    return node->getEndpoint(endpointId);
}

void IntegrationPluginZigbeeGeneric::initSimplePowerSocket(ZigbeeNode *node, ZigbeeNodeEndpoint *endpoint)
{
    // Get the on/off server cluster from the endpoint
    ZigbeeClusterOnOff *onOffCluster = endpoint->inputCluster<ZigbeeClusterOnOff>(ZigbeeClusterLibrary::ClusterIdOnOff);
    if (!onOffCluster)
        return;

    qCDebug(dcZigbeeGeneric()) << "Reading on/off power value for" << node << endpoint;
    ZigbeeClusterReply *reply = onOffCluster->readAttributes({ZigbeeClusterOnOff::AttributeOnOff});
    connect(reply, &ZigbeeClusterReply::finished, node, [=](){
        if (reply->error() != ZigbeeClusterReply::ErrorNoError) {
            qCWarning(dcZigbeeGeneric()) << "Failed to read on/off cluster attribute from" << node << endpoint << reply->error();
            return;
        }
    });

    ZigbeeDeviceObjectReply *bindPowerReply = node->deviceObject()->requestBindIeeeAddress(endpoint->endpointId(), ZigbeeClusterLibrary::ClusterIdOnOff,
                                                                                           hardwareManager()->zigbeeResource()->coordinatorAddress(node->networkUuid()), 0x01);
    connect(bindPowerReply, &ZigbeeDeviceObjectReply::finished, node, [=](){
        if (bindPowerReply->error() != ZigbeeDeviceObjectReply::ErrorNoError) {
            qCWarning(dcZigbeeGeneric()) << "Failed to bind power configuration cluster" << bindPowerReply->error();
        } else {
            qCDebug(dcZigbeeGeneric()) << "Binding power configuration cluster finished successfully";
        }

        ZigbeeClusterLibrary::AttributeReportingConfiguration batteryPercentageConfig;
        batteryPercentageConfig.attributeId = ZigbeeClusterOnOff::AttributeOnOff;
        batteryPercentageConfig.dataType = Zigbee::Uint8;
        batteryPercentageConfig.minReportingInterval = 60;
        batteryPercentageConfig.maxReportingInterval = 120;
        batteryPercentageConfig.reportableChange = ZigbeeDataType(static_cast<quint8>(1)).data();

        qCDebug(dcZigbeeGeneric()) << "Configuring attribute reporting for OnOff cluster";
        ZigbeeClusterReply *reportingReply = onOffCluster->configureReporting({batteryPercentageConfig});
        connect(reportingReply, &ZigbeeClusterReply::finished, this, [=](){
            if (reportingReply->error() != ZigbeeClusterReply::ErrorNoError) {
                qCWarning(dcZigbeeGeneric()) << "Failed to configure OnOff cluster attribute reporting" << reportingReply->error();
            } else {
                qCDebug(dcZigbeeGeneric()) << "Attribute reporting configuration finished for OnOff cluster" << ZigbeeClusterLibrary::parseAttributeReportingStatusRecords(reportingReply->responseFrame().payload);
            }
        });
    });
}

void IntegrationPluginZigbeeGeneric::initDoorLock(ZigbeeNode *node, ZigbeeNodeEndpoint *endpoint)
{
    bindPowerConfigurationCluster(node, endpoint);

    qCDebug(dcZigbeeGeneric()) << "Binding door lock cluster ";
    ZigbeeDeviceObjectReply * zdoReply = node->deviceObject()->requestBindIeeeAddress(endpoint->endpointId(), ZigbeeClusterLibrary::ClusterIdDoorLock, hardwareManager()->zigbeeResource()->coordinatorAddress(node->networkUuid()), 0x01);
    connect(zdoReply, &ZigbeeDeviceObjectReply::finished, node, [=](){
        if (zdoReply->error() != ZigbeeDeviceObjectReply::ErrorNoError) {
            qCWarning(dcZigbeeGeneric()) << "Failed to door lock cluster to coordinator" << zdoReply->error();
        } else {
            qCDebug(dcZigbeeGeneric()) << "Bind door lock cluster to coordinator finished successfully";
        }

        // Configure attribute reporting for lock state
        ZigbeeClusterLibrary::AttributeReportingConfiguration reportingConfig;
        reportingConfig.attributeId = ZigbeeClusterDoorLock::AttributeLockState;
        reportingConfig.dataType = Zigbee::Enum8;
        reportingConfig.minReportingInterval = 60;
        reportingConfig.maxReportingInterval = 120;
        reportingConfig.reportableChange = ZigbeeDataType(static_cast<quint8>(1)).data();

        qCDebug(dcZigbeeGeneric()) << "Configure attribute reporting for door lock cluster to coordinator";
        ZigbeeClusterReply *reportingReply = endpoint->getInputCluster(ZigbeeClusterLibrary::ClusterIdDoorLock)->configureReporting({reportingConfig});
        connect(reportingReply, &ZigbeeClusterReply::finished, this, [=](){
            if (reportingReply->error() != ZigbeeClusterReply::ErrorNoError) {
                qCWarning(dcZigbeeGeneric()) << "Failed to door lock cluster attribute reporting" << reportingReply->error();
            } else {
                qCDebug(dcZigbeeGeneric()) << "Attribute reporting configuration finished for door lock cluster" << ZigbeeClusterLibrary::parseAttributeReportingStatusRecords(reportingReply->responseFrame().payload);
            }
        });
    });
}

void IntegrationPluginZigbeeGeneric::initIASSensor(ZigbeeNode *node, ZigbeeNodeEndpoint *endpoint)
{
    bindPowerConfigurationCluster(node, endpoint);

    // First, bind the IAS cluster in a regular manner, for devices that don't fully implement the enrollment process:
    qCDebug(dcZigbeeGeneric()) << "Binding IAS Zone cluster";
    ZigbeeDeviceObjectReply *bindIasClusterReply = node->deviceObject()->requestBindIeeeAddress(endpoint->endpointId(), ZigbeeClusterLibrary::ClusterIdIasZone,
                                                                                     hardwareManager()->zigbeeResource()->coordinatorAddress(node->networkUuid()), 0x01);
    connect(bindIasClusterReply, &ZigbeeDeviceObjectReply::finished, node, [=](){
        if (bindIasClusterReply->error() != ZigbeeDeviceObjectReply::ErrorNoError) {
            qCWarning(dcZigbeeGeneric()) << "Failed to bind IAS zone cluster" << bindIasClusterReply->error();
        } else {
            qCDebug(dcZigbeeGeneric()) << "Binding IAS zone cluster finished successfully";
        }

        ZigbeeClusterLibrary::AttributeReportingConfiguration reportingStatusConfig;
        reportingStatusConfig.attributeId = ZigbeeClusterIasZone::AttributeZoneStatus;
        reportingStatusConfig.dataType = Zigbee::BitMap16;
        reportingStatusConfig.minReportingInterval = 300;
        reportingStatusConfig.maxReportingInterval = 2700;
        reportingStatusConfig.reportableChange = ZigbeeDataType(static_cast<quint8>(1)).data();

        qCDebug(dcZigbeeGeneric()) << "Configuring attribute reporting for IAS Zone cluster";
        ZigbeeClusterReply *reportingReply = endpoint->getInputCluster(ZigbeeClusterLibrary::ClusterIdIasZone)->configureReporting({reportingStatusConfig});
        connect(reportingReply, &ZigbeeClusterReply::finished, this, [=](){
            if (reportingReply->error() != ZigbeeClusterReply::ErrorNoError) {
                qCWarning(dcZigbeeGeneric()) << "Failed to configure IAS Zone cluster status attribute reporting" << reportingReply->error();
            } else {
                qCDebug(dcZigbeeGeneric()) << "Attribute reporting configuration finished for IAS Zone cluster" << ZigbeeClusterLibrary::parseAttributeReportingStatusRecords(reportingReply->responseFrame().payload);
            }


            // OK, now we've bound regularly, devices that require zone enrollment may still not send us anything, so let's try to enroll a zone
            // For that we need to write our own IEEE address as the CIE (security zone master)
            ZigbeeDataType dataType(hardwareManager()->zigbeeResource()->coordinatorAddress(node->networkUuid()).toUInt64());
            ZigbeeClusterLibrary::WriteAttributeRecord record;
            record.attributeId = ZigbeeClusterIasZone::AttributeCieAddress;
            record.dataType = Zigbee::IeeeAddress;
            record.data = dataType.data();
            qCDebug(dcZigbeeGeneric()) << "Setting CIE address" << hardwareManager()->zigbeeResource()->coordinatorAddress(node->networkUuid()) << record.data;
            ZigbeeClusterIasZone *iasZoneCluster = dynamic_cast<ZigbeeClusterIasZone*>(endpoint->getInputCluster(ZigbeeClusterLibrary::ClusterIdIasZone));
            ZigbeeClusterReply *writeCIEreply = iasZoneCluster->writeAttributes({record});
            connect(writeCIEreply, &ZigbeeClusterReply::finished, this, [=](){
                if (writeCIEreply->error() != ZigbeeClusterReply::ErrorNoError) {
                    qCWarning(dcZigbeeGeneric()) << "Failed to write CIE address to IAS server:" << writeCIEreply->error();
                    return;
                }

                qCDebug(dcZigbeeGeneric()) << "Wrote CIE address to IAS server:" << ZigbeeClusterLibrary::parseAttributeReportingStatusRecords(writeCIEreply->responseFrame().payload);

                // Auto-Enroll-Response mechanism: We'll be sending an enroll response right away (without request) to try and enroll a zone
                qCDebug(dcZigbeeGeneric()) << "Enrolling zone 0x42 to IAS server.";
                ZigbeeClusterReply *enrollReply = iasZoneCluster->sendZoneEnrollResponse(0x42);
                connect(enrollReply, &ZigbeeClusterReply::finished, this, [=](){
                    // Interestingly some devices stop regular conversation as soon as a zone is enrolled, so we might never get this reply...
                    qCDebug(dcZigbeeGeneric()) << "Zone enrollment reply:" << enrollReply->error() << enrollReply->responseData() << enrollReply->responseFrame();
                });

                // According to the spec, if Auto-Enroll-Response is implemented, also Trip-to-Pair is to be handled
                connect(iasZoneCluster, &ZigbeeClusterIasZone::zoneEnrollRequest, this, [=](ZigbeeClusterIasZone::ZoneType zoneType, quint16 manufacturerCode){
                    // Accepting any zoneZype/manufacturercode
                    Q_UNUSED(zoneType)
                    Q_UNUSED(manufacturerCode)
                    iasZoneCluster->sendZoneEnrollResponse(0x42);
                });
            });
        });
    });
}
