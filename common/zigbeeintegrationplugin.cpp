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
#include <zcl/measurement/zigbeeclusterelectricalmeasurement.h>
#include <zcl/smartenergy/zigbeeclustermetering.h>

Q_DECLARE_LOGGING_CATEGORY(dcZigbeeCluster)

ZigbeeIntegrationPlugin::ZigbeeIntegrationPlugin(ZigbeeHardwareResource::HandlerType handlerType):
    m_handlerType(handlerType)
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

void ZigbeeIntegrationPlugin::bindPowerConfigurationCluster(ZigbeeNode *node, ZigbeeNodeEndpoint *endpoint)
{
    ZigbeeDeviceObjectReply *bindPowerReply = node->deviceObject()->requestBindIeeeAddress(endpoint->endpointId(), ZigbeeClusterLibrary::ClusterIdPowerConfiguration,
                                                                                           hardwareManager()->zigbeeResource()->coordinatorAddress(node->networkUuid()), 0x01);
    connect(bindPowerReply, &ZigbeeDeviceObjectReply::finished, endpoint, [=](){
        if (bindPowerReply->error() != ZigbeeDeviceObjectReply::ErrorNoError) {
            qCWarning(dcZigbeeCluster()) << "Failed to bind power configuration cluster" << bindPowerReply->error();
        }

        ZigbeeClusterLibrary::AttributeReportingConfiguration batteryPercentageConfig;
        batteryPercentageConfig.attributeId = ZigbeeClusterPowerConfiguration::AttributeBatteryPercentageRemaining;
        batteryPercentageConfig.dataType = Zigbee::Uint8;
        batteryPercentageConfig.minReportingInterval = 60;
        batteryPercentageConfig.maxReportingInterval = 120;
        batteryPercentageConfig.reportableChange = ZigbeeDataType(static_cast<quint8>(1)).data();

        ZigbeeClusterReply *reportingReply = endpoint->getInputCluster(ZigbeeClusterLibrary::ClusterIdPowerConfiguration)->configureReporting({batteryPercentageConfig});
        connect(reportingReply, &ZigbeeClusterReply::finished, this, [=](){
            if (reportingReply->error() != ZigbeeClusterReply::ErrorNoError) {
                qCWarning(dcZigbeeCluster()) << "Failed to configure power configuration cluster attribute reporting" << reportingReply->error();
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
            qCWarning(dcZigbeeCluster()) << "Failed to bind thermostat cluster" << bindThermostatReply->error();
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
                qCWarning(dcZigbeeCluster()) << "Failed to configure thermostat configuration cluster attribute reporting" << reportingReply->error();
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
            qCWarning(dcZigbeeCluster()) << "Failed to bind OnOff cluster" << bindOnOffClusterReply->error();
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
                qCWarning(dcZigbeeCluster()) << "Failed to configure OnOff cluster attribute reporting" << reportingReply->error();
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
            qCWarning(dcZigbeeCluster()) << "Failed to bind OnOff cluster" << bindOnOffClusterReply->error();
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
            qCWarning(dcZigbeeCluster()) << "Failed to bind electrical measurement cluster" << bindElectricalMeasurementClusterReply->error();
        } else {
            qCDebug(dcZigbeeCluster()) << "Bound electrical measurement cluster successfully";
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
                qCWarning(dcZigbeeCluster()) << "Failed to configure electrical measurement cluster attribute reporting" << reportingReply->error();
            } else {
                qCDebug(dcZigbeeCluster()) << "Enabled attribute reporting successfully";
            }
        });
    });
}

void ZigbeeIntegrationPlugin::bindMeteringCluster(ZigbeeNodeEndpoint *endpoint)
{
    ZigbeeNode *node = endpoint->node();

    ZigbeeClusterMetering* meteringCluster = endpoint->inputCluster<ZigbeeClusterMetering>(ZigbeeClusterLibrary::ClusterIdMetering);
    if (!meteringCluster) {
        qCWarning(dcZigbeeCluster()) << "No metering cluster on this endpoint";
        return;
    }
    meteringCluster->readFormatting();

    ZigbeeDeviceObjectReply *bindMeteringClusterReply = node->deviceObject()->requestBindIeeeAddress(endpoint->endpointId(), ZigbeeClusterLibrary::ClusterIdMetering,
                                                                                           hardwareManager()->zigbeeResource()->coordinatorAddress(node->networkUuid()), 0x01);
    connect(bindMeteringClusterReply, &ZigbeeDeviceObjectReply::finished, node, [=](){
        if (bindMeteringClusterReply->error() != ZigbeeDeviceObjectReply::ErrorNoError) {
            qCWarning(dcZigbeeCluster()) << "Failed to bind metering cluster" << bindMeteringClusterReply->error();
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
                qCWarning(dcZigbeeCluster()) << "Failed to configure OnOff cluster attribute reporting" << reportingReply->error();
            }
        });
    });
}

void ZigbeeIntegrationPlugin::connectToPowerConfigurationCluster(Thing *thing, ZigbeeNodeEndpoint *endpoint)
{
    ZigbeeClusterPowerConfiguration *powerCluster = endpoint->inputCluster<ZigbeeClusterPowerConfiguration>(ZigbeeClusterLibrary::ClusterIdPowerConfiguration);
    if (powerCluster) {
        // If the power cluster attributes are already available, read values now
        if (powerCluster->hasAttribute(ZigbeeClusterPowerConfiguration::AttributeBatteryPercentageRemaining)) {
            thing->setStateValue("batteryLevel", powerCluster->batteryPercentage());
            thing->setStateValue("batteryCritical", (powerCluster->batteryPercentage() < 10.0));
        }
        // Refresh power cluster attributes in any case
        ZigbeeClusterReply *reply = powerCluster->readAttributes({ZigbeeClusterPowerConfiguration::AttributeBatteryPercentageRemaining});
        connect(reply, &ZigbeeClusterReply::finished, thing, [=](){
            if (reply->error() != ZigbeeClusterReply::ErrorNoError) {
                qCWarning(dcZigbeeCluster()) << thing->name() << "Reading power configuration cluster attributes finished with error" << reply->error();
                return;
            }
            thing->setStateValue("batteryLevel", powerCluster->batteryPercentage());
            thing->setStateValue("batteryCritical", (powerCluster->batteryPercentage() < 10.0));
        });

        // Connect to battery level changes
        connect(powerCluster, &ZigbeeClusterPowerConfiguration::batteryPercentageChanged, thing, [=](double percentage){
            thing->setStateValue("batteryLevel", percentage);
            thing->setStateValue("batteryCritical", (percentage < 10.0));
        });
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

