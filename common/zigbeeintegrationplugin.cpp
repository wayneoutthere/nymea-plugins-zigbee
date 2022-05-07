/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * *
*
* Copyright 2013 - 2022, nymea GmbH
* Contact: contact@nymea.io
*
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

#include "zigbeeintegrationplugin.h"

#include <hardware/zigbee/zigbeehardwareresource.h>

#include <zcl/general/zigbeeclusterpowerconfiguration.h>
#include <zcl/general/zigbeeclusteronoff.h>
#include <zcl/hvac/zigbeeclusterthermostat.h>
#include <zcl/smartenergy/zigbeeclustermetering.h>
#include <zcl/measurement/zigbeeclusterelectricalmeasurement.h>
#include <zcl/measurement/zigbeeclustertemperaturemeasurement.h>
#include <zcl/security/zigbeeclusteriaszone.h>

ZigbeeIntegrationPlugin::ZigbeeIntegrationPlugin(ZigbeeHardwareResource::HandlerType handlerType, const QLoggingCategory &loggingCategory):
    m_handlerType(handlerType),
    m_dc(loggingCategory.categoryName())
{

}

ZigbeeIntegrationPlugin::~ZigbeeIntegrationPlugin()
{

}

void ZigbeeIntegrationPlugin::init()
{
    hardwareManager()->zigbeeResource()->registerHandler(this, m_handlerType);
}

void ZigbeeIntegrationPlugin::handleRemoveNode(ZigbeeNode *node, const QUuid &networkUuid)
{
    Q_UNUSED(networkUuid)
    foreach (Thing *thing, m_thingNodes.keys(node)) {
        emit autoThingDisappeared(thing->id());

        // Removing it from our map to prevent a loop that would ask the zigbee network to remove this node (see thingRemoved())
        m_thingNodes.remove(thing);
    }
}

void ZigbeeIntegrationPlugin::thingRemoved(Thing *thing)
{
    ZigbeeNode *node = m_thingNodes.take(thing);
    if (node) {
        QUuid networkUuid = thing->paramValue(thing->thingClass().paramTypes().findByName("networkUuid").id()).toUuid();
        hardwareManager()->zigbeeResource()->removeNodeFromNetwork(networkUuid, node);
    }
}

bool ZigbeeIntegrationPlugin::manageNode(Thing *thing)
{
    QUuid networkUuid = thing->paramValue(thing->thingClass().paramTypes().findByName("networkUuid").id()).toUuid();
    ZigbeeAddress zigbeeAddress = ZigbeeAddress(thing->paramValue(thing->thingClass().paramTypes().findByName("ieeeAddress").id()).toString());

    ZigbeeNode *node = m_thingNodes.value(thing);
    if (!node) {
        node = hardwareManager()->zigbeeResource()->claimNode(this, networkUuid, zigbeeAddress);
    }

    if (!node) {
        return false;
    }

    m_thingNodes.insert(thing, node);

    // Update connected state
    thing->setStateValue("connected", node->reachable());
    connect(node, &ZigbeeNode::reachableChanged, thing, [thing](bool reachable){
        thing->setStateValue("connected", reachable);
    });

    // Update signal strength
    thing->setStateValue("signalStrength", qRound(node->lqi() * 100.0 / 255.0));
    connect(node, &ZigbeeNode::lqiChanged, thing, [thing](quint8 lqi){
        uint signalStrength = qRound(lqi * 100.0 / 255.0);
        thing->setStateValue("signalStrength", signalStrength);
    });

    return true;
}

Thing *ZigbeeIntegrationPlugin::thingForNode(ZigbeeNode *node)
{
    return m_thingNodes.key(node);
}

ZigbeeNode *ZigbeeIntegrationPlugin::nodeForThing(Thing *thing)
{
    return m_thingNodes.value(thing);
}

void ZigbeeIntegrationPlugin::createThing(const ThingClassId &thingClassId, ZigbeeNode *node, const ParamList &additionalParams)
{
    ThingDescriptor descriptor(thingClassId);
    QString deviceClassName = supportedThings().findById(thingClassId).displayName();
    descriptor.setTitle(QString("%1 (%2 - %3)").arg(deviceClassName).arg(node->manufacturerName()).arg(node->modelName()));

    ParamList params;
    ThingClass tc = supportedThings().findById(thingClassId);
    params.append(Param(tc.paramTypes().findByName("networkUuid").id(), node->networkUuid().toString()));
    params.append(Param(tc.paramTypes().findByName("ieeeAddress").id(), node->extendedAddress().toString()));
    params.append(additionalParams);
    descriptor.setParams(params);
    emit autoThingsAppeared({descriptor});
}

void ZigbeeIntegrationPlugin::bindPowerConfigurationCluster(ZigbeeNodeEndpoint *endpoint)
{
    ZigbeeNode *node = endpoint->node();
    ZigbeeDeviceObjectReply *bindPowerReply = node->deviceObject()->requestBindIeeeAddress(endpoint->endpointId(), ZigbeeClusterLibrary::ClusterIdPowerConfiguration,
                                                                                           hardwareManager()->zigbeeResource()->coordinatorAddress(node->networkUuid()), 0x01);
    connect(bindPowerReply, &ZigbeeDeviceObjectReply::finished, endpoint, [=](){
        if (bindPowerReply->error() != ZigbeeDeviceObjectReply::ErrorNoError) {
            qCWarning(m_dc) << "Failed to bind power configuration cluster" << bindPowerReply->error();
        }

        ZigbeeClusterLibrary::AttributeReportingConfiguration batteryPercentageConfig;
        batteryPercentageConfig.attributeId = ZigbeeClusterPowerConfiguration::AttributeBatteryPercentageRemaining;
        batteryPercentageConfig.dataType = Zigbee::Uint8;
        batteryPercentageConfig.minReportingInterval = 60;
        batteryPercentageConfig.maxReportingInterval = 120;
        batteryPercentageConfig.reportableChange = ZigbeeDataType(static_cast<quint8>(1)).data();

        ZigbeeClusterLibrary::AttributeReportingConfiguration batteryAlarmStateConfig;
        batteryAlarmStateConfig.attributeId = ZigbeeClusterPowerConfiguration::AttributeBatteryAlarmState;
        batteryAlarmStateConfig.dataType = Zigbee::Uint8;
        batteryAlarmStateConfig.minReportingInterval = 60;
        batteryAlarmStateConfig.maxReportingInterval = 120;
        batteryAlarmStateConfig.reportableChange = ZigbeeDataType(static_cast<quint8>(1)).data();

        ZigbeeClusterPowerConfiguration *powerConfigurationCluster = endpoint->inputCluster<ZigbeeClusterPowerConfiguration>(ZigbeeClusterLibrary::ClusterIdPowerConfiguration);
        if (!powerConfigurationCluster) {
            qCWarning(m_dc) << "No power configuation cluster found. Cannot configure attribute reporting for" << thingForNode(node)->name() << endpoint;
            return;
        }
        ZigbeeClusterReply *reportingReply = powerConfigurationCluster->configureReporting({batteryPercentageConfig, batteryAlarmStateConfig});
        connect(reportingReply, &ZigbeeClusterReply::finished, this, [=](){
            if (reportingReply->error() != ZigbeeClusterReply::ErrorNoError) {
                qCWarning(m_dc) << "Failed to configure power configuration cluster attribute reporting" << reportingReply->error();
            }
        });
    });
}

void ZigbeeIntegrationPlugin::bindThermostatCluster(ZigbeeNode *node, ZigbeeNodeEndpoint *endpoint)
{
    ZigbeeDeviceObjectReply *bindThermostatReply = node->deviceObject()->requestBindIeeeAddress(endpoint->endpointId(), ZigbeeClusterLibrary::ClusterIdThermostat,
                                                                                           hardwareManager()->zigbeeResource()->coordinatorAddress(node->networkUuid()), 0x01);
    connect(bindThermostatReply, &ZigbeeDeviceObjectReply::finished, node, [=](){
        if (bindThermostatReply->error() != ZigbeeDeviceObjectReply::ErrorNoError) {
            qCWarning(m_dc) << "Failed to bind thermostat cluster" << bindThermostatReply->error();
        }

        ZigbeeClusterLibrary::AttributeReportingConfiguration batteryPercentageConfig;
        batteryPercentageConfig.attributeId = ZigbeeClusterThermostat::AttributeOccupiedHeatingSetpoint;
        batteryPercentageConfig.dataType = Zigbee::Uint8;
        batteryPercentageConfig.minReportingInterval = 60;
        batteryPercentageConfig.maxReportingInterval = 120;
        batteryPercentageConfig.reportableChange = ZigbeeDataType(static_cast<quint8>(1)).data();

        ZigbeeClusterReply *reportingReply = endpoint->getInputCluster(ZigbeeClusterLibrary::ClusterIdThermostat)->configureReporting({batteryPercentageConfig});
        connect(reportingReply, &ZigbeeClusterReply::finished, this, [=](){
            if (reportingReply->error() != ZigbeeClusterReply::ErrorNoError) {
                qCWarning(m_dc) << "Failed to configure thermostat configuration cluster attribute reporting" << reportingReply->error();
            }
        });
    });
}

void ZigbeeIntegrationPlugin::bindOnOffCluster(ZigbeeNode *node, ZigbeeNodeEndpoint *endpoint)
{
    ZigbeeDeviceObjectReply *bindOnOffClusterReply = node->deviceObject()->requestBindIeeeAddress(endpoint->endpointId(), ZigbeeClusterLibrary::ClusterIdOnOff,
                                                                                           hardwareManager()->zigbeeResource()->coordinatorAddress(node->networkUuid()), 0x01);
    connect(bindOnOffClusterReply, &ZigbeeDeviceObjectReply::finished, node, [=](){
        if (bindOnOffClusterReply->error() != ZigbeeDeviceObjectReply::ErrorNoError) {
            qCWarning(m_dc) << "Failed to bind OnOff cluster" << bindOnOffClusterReply->error();
        }

        ZigbeeClusterLibrary::AttributeReportingConfiguration onOffConfig;
        onOffConfig.attributeId = ZigbeeClusterOnOff::AttributeOnOff;
        onOffConfig.dataType = Zigbee::Uint8;
        onOffConfig.minReportingInterval = 60;
        onOffConfig.maxReportingInterval = 120;
        onOffConfig.reportableChange = ZigbeeDataType(static_cast<quint8>(1)).data();

        ZigbeeClusterReply *reportingReply = endpoint->getInputCluster(ZigbeeClusterLibrary::ClusterIdOnOff)->configureReporting({onOffConfig});
        connect(reportingReply, &ZigbeeClusterReply::finished, this, [=](){
            if (reportingReply->error() != ZigbeeClusterReply::ErrorNoError) {
                qCWarning(m_dc) << "Failed to configure OnOff cluster attribute reporting" << reportingReply->error();
            }
        });
    });
}

void ZigbeeIntegrationPlugin::bindOnOffOutputCluster(ZigbeeNode *node, ZigbeeNodeEndpoint *endpoint, int retries)
{
    ZigbeeDeviceObjectReply *bindOnOffClusterReply = node->deviceObject()->requestBindIeeeAddress(endpoint->endpointId(), ZigbeeClusterLibrary::ClusterIdOnOff,
                                                                                           hardwareManager()->zigbeeResource()->coordinatorAddress(node->networkUuid()), 0x01);
    connect(bindOnOffClusterReply, &ZigbeeDeviceObjectReply::finished, endpoint, [=](){
        if (bindOnOffClusterReply->error() != ZigbeeDeviceObjectReply::ErrorNoError) {
            qCWarning(m_dc) << "Failed to bind OnOff cluster" << bindOnOffClusterReply->error();
            if (retries > 0) {
                bindOnOffOutputCluster(node, endpoint, retries - 1);
            }
        }
    });
}

void ZigbeeIntegrationPlugin::bindElectricalMeasurementCluster(ZigbeeNodeEndpoint *endpoint)
{
    ZigbeeNode *node = endpoint->node();
    ZigbeeDeviceObjectReply *bindElectricalMeasurementClusterReply = node->deviceObject()->requestBindGroupAddress(endpoint->endpointId(), ZigbeeClusterLibrary::ClusterIdElectricalMeasurement, 0x0000);
    connect(bindElectricalMeasurementClusterReply, &ZigbeeDeviceObjectReply::finished, node, [=](){
        if (bindElectricalMeasurementClusterReply->error() != ZigbeeDeviceObjectReply::ErrorNoError) {
            qCWarning(m_dc) << "Failed to bind electrical measurement cluster" << bindElectricalMeasurementClusterReply->error();
        } else {
            qCDebug(m_dc) << "Bound electrical measurement cluster successfully";
        }

        ZigbeeClusterLibrary::AttributeReportingConfiguration acTotalPowerConfig;
        acTotalPowerConfig.attributeId = ZigbeeClusterElectricalMeasurement::AttributeACPhaseAMeasurementActivePower;
        acTotalPowerConfig.dataType = Zigbee::Int16;
        acTotalPowerConfig.minReportingInterval = 1; // we want currentPower asap
        acTotalPowerConfig.maxReportingInterval = 30;
        acTotalPowerConfig.reportableChange = ZigbeeDataType(static_cast<quint8>(1)).data();

        ZigbeeClusterLibrary::AttributeReportingConfiguration rmsVoltageConfig;
        rmsVoltageConfig.attributeId = ZigbeeClusterElectricalMeasurement::AttributeACPhaseAMeasurementRMSVoltage;
        rmsVoltageConfig.dataType = Zigbee::Int16;
        rmsVoltageConfig.minReportingInterval = 50;
        rmsVoltageConfig.maxReportingInterval = 120;
        rmsVoltageConfig.reportableChange = ZigbeeDataType(static_cast<quint8>(1)).data();

        ZigbeeClusterLibrary::AttributeReportingConfiguration rmsCurrentConfig;
        rmsCurrentConfig.attributeId = ZigbeeClusterElectricalMeasurement::AttributeACPhaseAMeasurementRMSCurrent;
        rmsCurrentConfig.dataType = Zigbee::Int16;
        rmsCurrentConfig.minReportingInterval = 10;
        rmsCurrentConfig.maxReportingInterval = 120;
        rmsCurrentConfig.reportableChange = ZigbeeDataType(static_cast<quint8>(1)).data();

        ZigbeeClusterReply *reportingReply = endpoint->getInputCluster(ZigbeeClusterLibrary::ClusterIdElectricalMeasurement)->configureReporting({acTotalPowerConfig, rmsVoltageConfig, rmsCurrentConfig});
        connect(reportingReply, &ZigbeeClusterReply::finished, this, [=](){
            if (reportingReply->error() != ZigbeeClusterReply::ErrorNoError) {
                qCWarning(m_dc) << "Failed to configure electrical measurement cluster attribute reporting" << reportingReply->error();
            } else {
                qCDebug(m_dc) << "Enabled attribute reporting successfully";
            }
        });
    });
}

void ZigbeeIntegrationPlugin::bindMeteringCluster(ZigbeeNodeEndpoint *endpoint)
{
    ZigbeeNode *node = endpoint->node();

    ZigbeeClusterMetering* meteringCluster = endpoint->inputCluster<ZigbeeClusterMetering>(ZigbeeClusterLibrary::ClusterIdMetering);
    if (!meteringCluster) {
        qCWarning(m_dc) << "No metering cluster on this endpoint";
        return;
    }
    meteringCluster->readFormatting();

    ZigbeeDeviceObjectReply *bindMeteringClusterReply = node->deviceObject()->requestBindIeeeAddress(endpoint->endpointId(), ZigbeeClusterLibrary::ClusterIdMetering,
                                                                                           hardwareManager()->zigbeeResource()->coordinatorAddress(node->networkUuid()), 0x01);
    connect(bindMeteringClusterReply, &ZigbeeDeviceObjectReply::finished, node, [=](){
        if (bindMeteringClusterReply->error() != ZigbeeDeviceObjectReply::ErrorNoError) {
            qCWarning(m_dc) << "Failed to bind metering cluster" << bindMeteringClusterReply->error();
        }

        ZigbeeClusterLibrary::AttributeReportingConfiguration instantaneousDemandConfig;
        instantaneousDemandConfig.attributeId = ZigbeeClusterMetering::AttributeInstantaneousDemand;
        instantaneousDemandConfig.dataType = Zigbee::Int24;
        instantaneousDemandConfig.minReportingInterval = 1; // We want currentPower asap
        instantaneousDemandConfig.maxReportingInterval = 120;
        instantaneousDemandConfig.reportableChange = ZigbeeDataType(static_cast<quint8>(1)).data();

        ZigbeeClusterLibrary::AttributeReportingConfiguration currentSummationConfig;
        currentSummationConfig.attributeId = ZigbeeClusterMetering::AttributeCurrentSummationDelivered;
        currentSummationConfig.dataType = Zigbee::Uint48;
        currentSummationConfig.minReportingInterval = 5;
        currentSummationConfig.maxReportingInterval = 120;
        currentSummationConfig.reportableChange = ZigbeeDataType(static_cast<quint8>(1)).data();

        ZigbeeClusterReply *reportingReply = meteringCluster->configureReporting({instantaneousDemandConfig, currentSummationConfig});
        connect(reportingReply, &ZigbeeClusterReply::finished, this, [=](){
            if (reportingReply->error() != ZigbeeClusterReply::ErrorNoError) {
                qCWarning(m_dc) << "Failed to configure metering cluster attribute reporting" << reportingReply->error();
            }
        });
    });
}

void ZigbeeIntegrationPlugin::bindTemperatureSensorInputCluster(ZigbeeNodeEndpoint *endpoint, int retries)
{
    ZigbeeNode *node = endpoint->node();

    ZigbeeClusterTemperatureMeasurement* temperatureMeasurementCluster = endpoint->inputCluster<ZigbeeClusterTemperatureMeasurement>(ZigbeeClusterLibrary::ClusterIdTemperatureMeasurement);
    if (!temperatureMeasurementCluster) {
        qCWarning(m_dc) << "No metering cluster on this endpoint";
        return;
    }

    temperatureMeasurementCluster->readAttributes({ZigbeeClusterTemperatureMeasurement::AttributeMeasuredValue});

    ZigbeeDeviceObjectReply *bindTemperatureMeasurementClusterReply = node->deviceObject()->requestBindIeeeAddress(endpoint->endpointId(), ZigbeeClusterLibrary::ClusterIdTemperatureMeasurement,
                                                                                           hardwareManager()->zigbeeResource()->coordinatorAddress(node->networkUuid()), 0x01);
    connect(bindTemperatureMeasurementClusterReply, &ZigbeeDeviceObjectReply::finished, node, [=](){
        if (bindTemperatureMeasurementClusterReply->error() != ZigbeeDeviceObjectReply::ErrorNoError) {
            qCWarning(m_dc) << "Failed to bind temperature measurement cluster" << bindTemperatureMeasurementClusterReply->error();
            if (retries > 0) {
                bindTemperatureSensorInputCluster(endpoint, retries - 1);
                return;
            }
            // Intentionally falling through... Still trying to configure attribute reporting, just in case
        }

        ZigbeeClusterLibrary::AttributeReportingConfiguration measuredValueReportingConfig;
        measuredValueReportingConfig.attributeId = ZigbeeClusterTemperatureMeasurement::AttributeMeasuredValue;
        measuredValueReportingConfig.dataType = Zigbee::Int16;
        measuredValueReportingConfig.minReportingInterval = 60; // We want currentPower asap
        measuredValueReportingConfig.maxReportingInterval = 1200;
        measuredValueReportingConfig.reportableChange = ZigbeeDataType(static_cast<quint8>(1)).data();

        ZigbeeClusterReply *reportingReply = temperatureMeasurementCluster->configureReporting({measuredValueReportingConfig});
        connect(reportingReply, &ZigbeeClusterReply::finished, this, [=](){
            if (reportingReply->error() != ZigbeeClusterReply::ErrorNoError) {
                qCWarning(m_dc) << "Failed to configure temperature measurement cluster attribute reporting" << reportingReply->error();
            }
        });
    });
}

void ZigbeeIntegrationPlugin::bindIasZoneInputCluster(ZigbeeNodeEndpoint *endpoint)
{
    // First, bind the IAS cluster in a regular manner, for devices that don't fully implement the enrollment process:
    qCDebug(m_dc) << "Binding IAS Zone cluster";
    ZigbeeNode *node = endpoint->node();
    ZigbeeDeviceObjectReply *bindIasClusterReply = node->deviceObject()->requestBindIeeeAddress(endpoint->endpointId(), ZigbeeClusterLibrary::ClusterIdIasZone,
                                                                                     hardwareManager()->zigbeeResource()->coordinatorAddress(node->networkUuid()), 0x01);
    connect(bindIasClusterReply, &ZigbeeDeviceObjectReply::finished, node, [=](){
        if (bindIasClusterReply->error() != ZigbeeDeviceObjectReply::ErrorNoError) {
            qCWarning(m_dc) << "Failed to bind IAS zone cluster" << bindIasClusterReply->error();
        } else {
            qCDebug(m_dc) << "Binding IAS zone cluster finished successfully";
        }

        ZigbeeClusterLibrary::AttributeReportingConfiguration reportingStatusConfig;
        reportingStatusConfig.attributeId = ZigbeeClusterIasZone::AttributeZoneStatus;
        reportingStatusConfig.dataType = Zigbee::BitMap16;
        reportingStatusConfig.minReportingInterval = 300;
        reportingStatusConfig.maxReportingInterval = 2700;
        reportingStatusConfig.reportableChange = ZigbeeDataType(static_cast<quint8>(1)).data();

        qCDebug(m_dc) << "Configuring attribute reporting for IAS Zone cluster";
        ZigbeeClusterReply *reportingReply = endpoint->getInputCluster(ZigbeeClusterLibrary::ClusterIdIasZone)->configureReporting({reportingStatusConfig});
        connect(reportingReply, &ZigbeeClusterReply::finished, this, [=](){
            if (reportingReply->error() != ZigbeeClusterReply::ErrorNoError) {
                qCWarning(m_dc) << "Failed to configure IAS Zone cluster status attribute reporting" << reportingReply->error();
            } else {
                qCDebug(m_dc) << "Attribute reporting configuration finished for IAS Zone cluster" << ZigbeeClusterLibrary::parseAttributeReportingStatusRecords(reportingReply->responseFrame().payload);
            }


            // OK, now we've bound regularly, devices that require zone enrollment may still not send us anything, so let's try to enroll a zone
            // For that we need to write our own IEEE address as the CIE (security zone master)
            ZigbeeDataType dataType(hardwareManager()->zigbeeResource()->coordinatorAddress(node->networkUuid()).toUInt64());
            ZigbeeClusterLibrary::WriteAttributeRecord record;
            record.attributeId = ZigbeeClusterIasZone::AttributeCieAddress;
            record.dataType = Zigbee::IeeeAddress;
            record.data = dataType.data();
            qCDebug(m_dc) << "Setting CIE address" << hardwareManager()->zigbeeResource()->coordinatorAddress(node->networkUuid()) << record.data;
            ZigbeeClusterIasZone *iasZoneCluster = dynamic_cast<ZigbeeClusterIasZone*>(endpoint->getInputCluster(ZigbeeClusterLibrary::ClusterIdIasZone));
            ZigbeeClusterReply *writeCIEreply = iasZoneCluster->writeAttributes({record});
            connect(writeCIEreply, &ZigbeeClusterReply::finished, this, [=](){
                if (writeCIEreply->error() != ZigbeeClusterReply::ErrorNoError) {
                    qCWarning(m_dc) << "Failed to write CIE address to IAS server:" << writeCIEreply->error();
                    return;
                }

                qCDebug(m_dc) << "Wrote CIE address to IAS server:" << ZigbeeClusterLibrary::parseAttributeReportingStatusRecords(writeCIEreply->responseFrame().payload);

                // Auto-Enroll-Response mechanism: We'll be sending an enroll response right away (without request) to try and enroll a zone
                qCDebug(m_dc) << "Enrolling zone 0x42 to IAS server.";
                ZigbeeClusterReply *enrollReply = iasZoneCluster->sendZoneEnrollResponse(0x42);
                connect(enrollReply, &ZigbeeClusterReply::finished, this, [=](){
                    // Interestingly some devices stop regular conversation as soon as a zone is enrolled, so we might never get this reply...
                    qCDebug(m_dc) << "Zone enrollment reply:" << enrollReply->error() << enrollReply->responseData() << enrollReply->responseFrame();
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

void ZigbeeIntegrationPlugin::connectToPowerConfigurationCluster(Thing *thing, ZigbeeNodeEndpoint *endpoint)
{
    ZigbeeClusterPowerConfiguration *powerCluster = endpoint->inputCluster<ZigbeeClusterPowerConfiguration>(ZigbeeClusterLibrary::ClusterIdPowerConfiguration);
    if (powerCluster) {
        // If the power cluster attributes are already available, read values now
        if (thing->thingClass().hasStateType("batteryLevel") && powerCluster->hasAttribute(ZigbeeClusterPowerConfiguration::AttributeBatteryPercentageRemaining)) {
            thing->setStateValue("batteryLevel", powerCluster->batteryPercentage());
        }
        if (powerCluster->hasAttribute(ZigbeeClusterPowerConfiguration::AttributeBatteryAlarmState)) {
            thing->setStateValue("batteryCritical", powerCluster->batteryAlarmState() > 0);
        } else {
            thing->setStateValue("batteryCritical", thing->stateValue("batteryLevel").toInt() < 10);
        }

        // Connect to changes
        connect(powerCluster, &ZigbeeClusterPowerConfiguration::batteryPercentageChanged, thing, [=](double percentage){
            if (thing->thingClass().hasStateType("batteryLevel")) {
                thing->setStateValue("batteryLevel", percentage);
            }
            if (!powerCluster->hasAttribute(ZigbeeClusterPowerConfiguration::AttributeBatteryAlarmState)) {
                thing->setStateValue("batteryCritical", (percentage < 10.0));
            }
        });
        connect(powerCluster, &ZigbeeClusterPowerConfiguration::batteryAlarmStateChanged, thing, [=](ZigbeeClusterPowerConfiguration::BatteryAlarmMask alarmState){
            thing->setStateValue("batteryCritical", alarmState > 0);
        });

        // Refresh power cluster attributes in any case. Response will be processed in signal handlers
        powerCluster->readAttributes({
                                     ZigbeeClusterPowerConfiguration::AttributeBatteryPercentageRemaining,
                                     ZigbeeClusterPowerConfiguration::AttributeBatteryAlarmState
                                 });

    } else {
        qCWarning(m_dc) << "No power configuration cluster on" << thing->name() << "and endpoint" << endpoint->endpointId();
    }
}

void ZigbeeIntegrationPlugin::connectToThermostatCluster(Thing *thing, ZigbeeNodeEndpoint *endpoint)
{
    ZigbeeClusterThermostat *thermostatCluster = endpoint->inputCluster<ZigbeeClusterThermostat>(ZigbeeClusterLibrary::ClusterIdThermostat);
    if (thermostatCluster) {
        thermostatCluster->readAttributes({ZigbeeClusterThermostat::AttributeLocalTemperature,
                                           ZigbeeClusterThermostat::AttributeOccupiedHeatingSetpoint,
                                           ZigbeeClusterThermostat::AttributeMinHeatSetpointLimit,
                                           ZigbeeClusterThermostat::AttributeMaxHeatSetpointLimit,
                                           ZigbeeClusterThermostat::AttributePIHeatingDemand,
                                           ZigbeeClusterThermostat::AttributePICoolingDemand});

        connect(thermostatCluster, &ZigbeeClusterThermostat::attributeChanged, thing, [thing](const ZigbeeClusterAttribute &attribute){
            if (attribute.id() == ZigbeeClusterThermostat::AttributeOccupiedHeatingSetpoint) {
                thing->setStateValue("targetTemperature", attribute.dataType().toUInt16() * 0.01);
            }
            if (attribute.id() == ZigbeeClusterThermostat::AttributeLocalTemperature) {
                thing->setStateValue("temperature", attribute.dataType().toUInt16() * 0.01);
            }
            if (attribute.id() == ZigbeeClusterThermostat::AttributePIHeatingDemand) {
                thing->setStateValue("heatingOn", attribute.dataType().toUInt8() > 0);
            }
            if (attribute.id() == ZigbeeClusterThermostat::AttributePICoolingDemand) {
                thing->setStateValue("coolingOn", attribute.dataType().toUInt8() > 0);
            }
            if (attribute.id() == ZigbeeClusterThermostat::AttributeMinHeatSetpointLimit) {
                thing->setStateMinValue("targetTemperature", attribute.dataType().toUInt16() * 0.01);
            }
            if (attribute.id() == ZigbeeClusterThermostat::AttributeMaxHeatSetpointLimit) {
                thing->setStateMaxValue("targetTemperature", attribute.dataType().toUInt16() * 0.01);
            }
        });
    }
}

void ZigbeeIntegrationPlugin::connectToOnOffCluster(Thing *thing, ZigbeeNodeEndpoint *endpoint, const QString &stateName)
{
    ZigbeeClusterOnOff *onOffCluster = endpoint->inputCluster<ZigbeeClusterOnOff>(ZigbeeClusterLibrary::ClusterIdOnOff);
    if (onOffCluster) {
        if (onOffCluster->hasAttribute(ZigbeeClusterOnOff::AttributeOnOff)) {
            thing->setStateValue(stateName, onOffCluster->power());
        } else {
            onOffCluster->readAttributes({ZigbeeClusterOnOff::AttributeOnOff});
        }
        connect(onOffCluster, &ZigbeeClusterOnOff::powerChanged, thing, [thing, stateName](bool power){
            thing->setStateValue(stateName, power);
        });
    }
}

void ZigbeeIntegrationPlugin::connectToElectricalMeasurementCluster(Thing *thing, ZigbeeNodeEndpoint *endpoint)
{
    ZigbeeClusterElectricalMeasurement *electricalMeasurementCluster = endpoint->inputCluster<ZigbeeClusterElectricalMeasurement>(ZigbeeClusterLibrary::ClusterIdElectricalMeasurement);
    if (electricalMeasurementCluster) {
        connect(electricalMeasurementCluster, &ZigbeeClusterElectricalMeasurement::activePowerPhaseAChanged, thing, [thing](qint16 activePowerPhaseA){
            thing->setStateValue("currentPower", activePowerPhaseA);
        });
    }
}

void ZigbeeIntegrationPlugin::connectToMeteringCluster(Thing *thing, ZigbeeNodeEndpoint *endpoint)
{
    ZigbeeClusterMetering *meteringCluster = endpoint->inputCluster<ZigbeeClusterMetering>(ZigbeeClusterLibrary::ClusterIdMetering);
    if (meteringCluster) {
        meteringCluster->readFormatting();

        connect(meteringCluster, &ZigbeeClusterMetering::currentSummationDeliveredChanged, thing, [=](quint64 currentSummationDelivered){
            thing->setStateValue("totalEnergyConsumed", 1.0 * currentSummationDelivered * meteringCluster->multiplier() / meteringCluster->divisor());
        });

        connect(meteringCluster, &ZigbeeClusterMetering::instantaneousDemandChanged, thing, [=](qint32 instantaneousDemand){
            thing->setStateValue("currentPower", instantaneousDemand);
        });
    }
}

void ZigbeeIntegrationPlugin::connectToTemperatureMeasurementInputCluster(Thing *thing, ZigbeeNodeEndpoint *endpoint)
{
    ZigbeeClusterTemperatureMeasurement *temperatureMeasurementCluster = endpoint->inputCluster<ZigbeeClusterTemperatureMeasurement>(ZigbeeClusterLibrary::ClusterIdTemperatureMeasurement);
    if (!temperatureMeasurementCluster) {
        qCWarning(m_dc) << "Could not find temperature measurement cluster on" << thing->name() << endpoint;
    } else {
        if (temperatureMeasurementCluster->hasAttribute(ZigbeeClusterTemperatureMeasurement::AttributeMaxMeasuredValue)) {
            thing->setStateValue("temperature", temperatureMeasurementCluster->temperature());
        }
        temperatureMeasurementCluster->readAttributes({ZigbeeClusterTemperatureMeasurement::AttributeMeasuredValue});
        connect(temperatureMeasurementCluster, &ZigbeeClusterTemperatureMeasurement::temperatureChanged, thing, [=](double temperature) {
            qCDebug(m_dc) << "Temperature for" << thing->name() << "changed to:" << temperature;
            thing->setStateValue("temperature", temperature);
        });
    }
}

void ZigbeeIntegrationPlugin::connectToIasZoneInputCluster(Thing *thing, ZigbeeNodeEndpoint *endpoint, const QString &alarmStateName, bool inverted)
{
    ZigbeeClusterIasZone *iasZoneCluster = endpoint->inputCluster<ZigbeeClusterIasZone>(ZigbeeClusterLibrary::ClusterIdIasZone);
    if (!iasZoneCluster) {
        qCWarning(m_dc) << "Could not find IAS zone cluster on" << thing << endpoint;
    } else {
        qCDebug(m_dc) << "Cluster attributes:" << iasZoneCluster->attributes();
        qCDebug(m_dc) << "Zone state:" << thing->name() << iasZoneCluster->zoneState();
        qCDebug(m_dc) << "Zone type:" << thing->name() << iasZoneCluster->zoneType();
        qCDebug(m_dc) << "Zone status:" << thing->name() << iasZoneCluster->zoneStatus();
        if (iasZoneCluster->hasAttribute(ZigbeeClusterIasZone::AttributeZoneStatus)) {
            ZigbeeClusterIasZone::ZoneStatusFlags zoneStatus = iasZoneCluster->zoneStatus();
            bool zoneAlarm = zoneStatus.testFlag(ZigbeeClusterIasZone::ZoneStatusAlarm1) || zoneStatus.testFlag(ZigbeeClusterIasZone::ZoneStatusAlarm2);
            thing->setStateValue(alarmStateName, inverted ? !zoneAlarm : zoneAlarm);
            if (thing->thingClass().hasStateType("tampered")) {
                thing->setStateValue("tampered", zoneStatus.testFlag(ZigbeeClusterIasZone::ZoneStatusTamper));
            }
        }
        connect(iasZoneCluster, &ZigbeeClusterIasZone::zoneStatusChanged, thing, [=](ZigbeeClusterIasZone::ZoneStatusFlags zoneStatus, quint8 extendedStatus, quint8 zoneId, quint16 delays) {
            qCDebug(m_dc) << "Zone status changed to:" << zoneStatus << extendedStatus << zoneId << delays;
            bool zoneAlarm = zoneStatus.testFlag(ZigbeeClusterIasZone::ZoneStatusAlarm1) || zoneStatus.testFlag(ZigbeeClusterIasZone::ZoneStatusAlarm2);
            thing->setStateValue(alarmStateName, inverted ? !zoneAlarm : zoneAlarm);
            if (thing->thingClass().hasStateType("tampered")) {
                thing->setStateValue("tampered", zoneStatus.testFlag(ZigbeeClusterIasZone::ZoneStatusTamper));
            }
        });
    }
}

