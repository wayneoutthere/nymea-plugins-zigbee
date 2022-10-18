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
#include <network/networkaccessmanager.h>

#include <zigbeeutils.h>

#include <zcl/general/zigbeeclusterpowerconfiguration.h>
#include <zcl/general/zigbeeclusteronoff.h>
#include <zcl/general/zigbeeclusterlevelcontrol.h>
#include <zcl/general/zigbeeclusteranaloginput.h>
#include <zcl/hvac/zigbeeclusterthermostat.h>
#include <zcl/hvac/zigbeeclusterfancontrol.h>
#include <zcl/smartenergy/zigbeeclustermetering.h>
#include <zcl/measurement/zigbeeclusterelectricalmeasurement.h>
#include <zcl/measurement/zigbeeclustertemperaturemeasurement.h>
#include <zcl/measurement/zigbeeclusterilluminancemeasurement.h>
#include <zcl/measurement/zigbeeclusterrelativehumiditymeasurement.h>
#include <zcl/security/zigbeeclusteriaszone.h>
#include <zcl/general/zigbeeclusteridentify.h>
#include <zcl/measurement/zigbeeclusteroccupancysensing.h>
#include <zcl/ota/zigbeeclusterota.h>

#include <QColor>
#include <QNetworkRequest>
#include <QJsonDocument>
#include <QStandardPaths>
#include <QFile>
#include <QDataStream>
#include <qmath.h>

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

    updateFirmwareIndex();
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

    connect(node, &ZigbeeNode::lastSeenChanged, this, [=](){
        while (!m_delayedWriteRequests.value(node).isEmpty()) {
            DelayedAttributeWriteRequest request = m_delayedWriteRequests[node].takeFirst();
            request.cluster->writeAttributes(request.records, request.manufacturerCode);
        }
        while (!m_delayedReadRequests.value(node).isEmpty()) {
            DelayedAttributeReadRequest request = m_delayedReadRequests[node].takeFirst();
            request.cluster->readAttributes(request.attributes, request.manufacturerCode);
        }
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

void ZigbeeIntegrationPlugin::bindCluster(ZigbeeNodeEndpoint *endpoint, quint16 clusterId)
{
    ZigbeeNode *node = endpoint->node();
    ZigbeeDeviceObjectReply *bindClusterReply = node->deviceObject()->requestBindIeeeAddress(endpoint->endpointId(), clusterId,
                                                                                           hardwareManager()->zigbeeResource()->coordinatorAddress(node->networkUuid()), 0x01);
    connect(bindClusterReply, &ZigbeeDeviceObjectReply::finished, node, [=](){
        if (bindClusterReply->error() != ZigbeeDeviceObjectReply::ErrorNoError) {
            qCWarning(m_dc) << "Failed to cluster" << clusterId << bindClusterReply->error();
        }
    });
}

void ZigbeeIntegrationPlugin::bindPowerConfigurationCluster(ZigbeeNodeEndpoint *endpoint)
{
    ZigbeeDeviceObjectReply *bindPowerReply = endpoint->node()->deviceObject()->requestBindIeeeAddress(endpoint->endpointId(), ZigbeeClusterLibrary::ClusterIdPowerConfiguration,
                                                                                           hardwareManager()->zigbeeResource()->coordinatorAddress(endpoint->node()->networkUuid()), 0x01);
    connect(bindPowerReply, &ZigbeeDeviceObjectReply::finished, endpoint, [=](){
        if (bindPowerReply->error() != ZigbeeDeviceObjectReply::ErrorNoError) {
            qCWarning(m_dc) << "Failed to bind power configuration cluster" << bindPowerReply->error();
        }
    });
}

void ZigbeeIntegrationPlugin::bindThermostatCluster(ZigbeeNodeEndpoint *endpoint)
{
    ZigbeeDeviceObjectReply *bindThermostatReply = endpoint->node()->deviceObject()->requestBindIeeeAddress(endpoint->endpointId(), ZigbeeClusterLibrary::ClusterIdThermostat,
                                                                                           hardwareManager()->zigbeeResource()->coordinatorAddress(endpoint->node()->networkUuid()), 0x01);
    connect(bindThermostatReply, &ZigbeeDeviceObjectReply::finished, endpoint, [=](){
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

void ZigbeeIntegrationPlugin::bindOnOffCluster(ZigbeeNodeEndpoint *endpoint, int retries)
{
    ZigbeeDeviceObjectReply *bindOnOffClusterReply = endpoint->node()->deviceObject()->requestBindIeeeAddress(endpoint->endpointId(), ZigbeeClusterLibrary::ClusterIdOnOff,
                                                                                           hardwareManager()->zigbeeResource()->coordinatorAddress(endpoint->node()->networkUuid()), 0x01);
    connect(bindOnOffClusterReply, &ZigbeeDeviceObjectReply::finished, endpoint, [=](){
        if (bindOnOffClusterReply->error() != ZigbeeDeviceObjectReply::ErrorNoError) {
            qCWarning(m_dc) << "Failed to bind OnOff cluster" << bindOnOffClusterReply->error();
            if (retries > 0) {
                bindOnOffCluster(endpoint, retries - 1);
            }
        }
    });

    ZigbeeDeviceObjectReply * zdoReply = endpoint->node()->deviceObject()->requestBindGroupAddress(endpoint->endpointId(), ZigbeeClusterLibrary::ClusterIdOnOff, 0x0000);
    connect(zdoReply, &ZigbeeDeviceObjectReply::finished, endpoint->node(), [=](){
        if (zdoReply->error() != ZigbeeDeviceObjectReply::ErrorNoError) {
            qCWarning(m_dc) << "Failed to bind on/off cluster to coordinator" << zdoReply->error();
        } else {
            qCDebug(m_dc) << "Bind on/off cluster to coordinator finished successfully";
        }
    });
}

void ZigbeeIntegrationPlugin::bindLevelControlCluster(ZigbeeNodeEndpoint *endpoint)
{
    qCDebug(m_dc) << "Binding endpoint" << endpoint->endpointId() << "Level control input cluster";
    ZigbeeDeviceObjectReply *bindLevelControlInputClusterReply = endpoint->node()->deviceObject()->requestBindIeeeAddress(endpoint->endpointId(), ZigbeeClusterLibrary::ClusterIdLevelControl,
                                                                                           hardwareManager()->zigbeeResource()->coordinatorAddress(endpoint->node()->networkUuid()), 0x01);
    connect(bindLevelControlInputClusterReply, &ZigbeeDeviceObjectReply::finished, endpoint, [=](){
        if (bindLevelControlInputClusterReply->error() != ZigbeeDeviceObjectReply::ErrorNoError) {
            qCWarning(m_dc) << "Failed to bind Level Control input cluster" << bindLevelControlInputClusterReply->error();
        }
    });
}

void ZigbeeIntegrationPlugin::bindColorControlCluster(ZigbeeNodeEndpoint *endpoint)
{
    qCDebug(m_dc) << "Binding endpoint" << endpoint->endpointId() << "Color control input cluster";
    ZigbeeDeviceObjectReply *bindColorControlInputClusterReply = endpoint->node()->deviceObject()->requestBindIeeeAddress(endpoint->endpointId(), ZigbeeClusterLibrary::ClusterIdColorControl,
                                                                                           hardwareManager()->zigbeeResource()->coordinatorAddress(endpoint->node()->networkUuid()), 0x01);
    connect(bindColorControlInputClusterReply, &ZigbeeDeviceObjectReply::finished, endpoint, [=](){
        if (bindColorControlInputClusterReply->error() != ZigbeeDeviceObjectReply::ErrorNoError) {
            qCWarning(m_dc) << "Failed to bind Color Control input cluster" << bindColorControlInputClusterReply->error();
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
    });
}

void ZigbeeIntegrationPlugin::bindMeteringCluster(ZigbeeNodeEndpoint *endpoint)
{
    ZigbeeNode *node = endpoint->node();

    ZigbeeDeviceObjectReply *bindMeteringClusterReply = node->deviceObject()->requestBindIeeeAddress(endpoint->endpointId(), ZigbeeClusterLibrary::ClusterIdMetering,
                                                                                           hardwareManager()->zigbeeResource()->coordinatorAddress(node->networkUuid()), 0x01);
    connect(bindMeteringClusterReply, &ZigbeeDeviceObjectReply::finished, node, [=](){
        if (bindMeteringClusterReply->error() != ZigbeeDeviceObjectReply::ErrorNoError) {
            qCWarning(m_dc) << "Failed to bind metering cluster" << bindMeteringClusterReply->error();
        }
    });
}

void ZigbeeIntegrationPlugin::bindTemperatureMeasurementCluster(ZigbeeNodeEndpoint *endpoint, int retries)
{
    ZigbeeNode *node = endpoint->node();

    ZigbeeDeviceObjectReply *bindTemperatureMeasurementClusterReply = node->deviceObject()->requestBindIeeeAddress(endpoint->endpointId(), ZigbeeClusterLibrary::ClusterIdTemperatureMeasurement,
                                                                                           hardwareManager()->zigbeeResource()->coordinatorAddress(node->networkUuid()), 0x01);
    connect(bindTemperatureMeasurementClusterReply, &ZigbeeDeviceObjectReply::finished, node, [=](){
        if (bindTemperatureMeasurementClusterReply->error() != ZigbeeDeviceObjectReply::ErrorNoError) {
            qCWarning(m_dc) << "Failed to bind temperature measurement cluster" << bindTemperatureMeasurementClusterReply->error();
            if (retries > 0) {
                bindTemperatureMeasurementCluster(endpoint, retries - 1);
                return;
            }
        }
    });
}

void ZigbeeIntegrationPlugin::bindRelativeHumidityMeasurementCluster(ZigbeeNodeEndpoint *endpoint, int retries)
{
    ZigbeeNode *node = endpoint->node();
    ZigbeeDeviceObjectReply *bindRelativeHumidityMeasurementClusterReply = node->deviceObject()->requestBindIeeeAddress(endpoint->endpointId(), ZigbeeClusterLibrary::ClusterIdRelativeHumidityMeasurement,
                                                                                           hardwareManager()->zigbeeResource()->coordinatorAddress(node->networkUuid()), 0x01);
    connect(bindRelativeHumidityMeasurementClusterReply, &ZigbeeDeviceObjectReply::finished, node, [=](){
        if (bindRelativeHumidityMeasurementClusterReply->error() != ZigbeeDeviceObjectReply::ErrorNoError) {
            qCWarning(m_dc) << "Failed to bind relative humidity measurement cluster" << bindRelativeHumidityMeasurementClusterReply->error();
            if (retries > 0) {
                bindRelativeHumidityMeasurementCluster(endpoint, retries - 1);
                return;
            }
            // Intentionally falling through... Still trying to configure attribute reporting, just in case
        }
    });
}

void ZigbeeIntegrationPlugin::bindIasZoneCluster(ZigbeeNodeEndpoint *endpoint)
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

void ZigbeeIntegrationPlugin::bindIlluminanceMeasurementCluster(ZigbeeNodeEndpoint *endpoint, int retries)
{
    ZigbeeNode *node = endpoint->node();
    ZigbeeDeviceObjectReply *bindIlluminanceMeasurementClusterReply = node->deviceObject()->requestBindIeeeAddress(endpoint->endpointId(), ZigbeeClusterLibrary::ClusterIdIlluminanceMeasurement,
                                                                                           hardwareManager()->zigbeeResource()->coordinatorAddress(node->networkUuid()), 0x01);
    connect(bindIlluminanceMeasurementClusterReply, &ZigbeeDeviceObjectReply::finished, node, [=](){
        if (bindIlluminanceMeasurementClusterReply->error() != ZigbeeDeviceObjectReply::ErrorNoError) {
            qCWarning(m_dc) << "Failed to bind illuminance measurement cluster" << bindIlluminanceMeasurementClusterReply->error();
            if (retries > 0) {
                bindIlluminanceMeasurementCluster(endpoint, retries - 1);
                return;
            }
        }
    });
}

void ZigbeeIntegrationPlugin::bindOccupancySensingCluster(ZigbeeNodeEndpoint *endpoint)
{
    ZigbeeNode *node = endpoint->node();
    ZigbeeDeviceObjectReply *bindOccupancySensingClusterReply = node->deviceObject()->requestBindIeeeAddress(endpoint->endpointId(), ZigbeeClusterLibrary::ClusterIdOccupancySensing,
                                                                                           hardwareManager()->zigbeeResource()->coordinatorAddress(node->networkUuid()), 0x01);
    connect(bindOccupancySensingClusterReply, &ZigbeeDeviceObjectReply::finished, node, [=](){
        if (bindOccupancySensingClusterReply->error() != ZigbeeDeviceObjectReply::ErrorNoError) {
            qCWarning(m_dc) << "Failed to bind occupancy sensing cluster" << bindOccupancySensingClusterReply->error();
        }
    });
}

void ZigbeeIntegrationPlugin::bindFanControlCluster(ZigbeeNodeEndpoint *endpoint)
{
    ZigbeeNode *node = endpoint->node();
    ZigbeeDeviceObjectReply *bindFanControlClusterReply = node->deviceObject()->requestBindIeeeAddress(endpoint->endpointId(), ZigbeeClusterLibrary::ClusterIdFanControl,
                                                                                           hardwareManager()->zigbeeResource()->coordinatorAddress(node->networkUuid()), 0x01);
    connect(bindFanControlClusterReply, &ZigbeeDeviceObjectReply::finished, node, [=](){
        if (bindFanControlClusterReply->error() != ZigbeeDeviceObjectReply::ErrorNoError) {
            qCWarning(m_dc) << "Failed to bind fan control cluster" << bindFanControlClusterReply->error();
        }
    });
}

void ZigbeeIntegrationPlugin::configurePowerConfigurationInputClusterAttributeReporting(ZigbeeNodeEndpoint *endpoint)
{
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
        qCWarning(m_dc) << "No power configuation cluster found. Cannot configure attribute reporting for"<< endpoint;
        return;
    }
    ZigbeeClusterReply *reportingReply = powerConfigurationCluster->configureReporting({batteryPercentageConfig, batteryAlarmStateConfig});
    connect(reportingReply, &ZigbeeClusterReply::finished, this, [=](){
        if (reportingReply->error() != ZigbeeClusterReply::ErrorNoError) {
            qCWarning(m_dc) << "Failed to configure power configuration cluster attribute reporting" << reportingReply->error();
        }
    });
}

void ZigbeeIntegrationPlugin::configureOnOffInputClusterAttributeReporting(ZigbeeNodeEndpoint *endpoint)
{
    ZigbeeClusterOnOff *onOffInputCluster = endpoint->inputCluster<ZigbeeClusterOnOff>(ZigbeeClusterLibrary::ClusterIdOnOff);
    if (!onOffInputCluster) {
        qCWarning(m_dc) << "No OnOff input cluster on" << endpoint->node();
        return;
    }
    ZigbeeClusterLibrary::AttributeReportingConfiguration onOffConfig;
    onOffConfig.attributeId = ZigbeeClusterOnOff::AttributeOnOff;
    onOffConfig.dataType = Zigbee::Bool;
    onOffConfig.minReportingInterval = 0;
    onOffConfig.maxReportingInterval = 120;
    onOffConfig.reportableChange = ZigbeeDataType(static_cast<quint8>(0)).data();

    qCDebug(m_dc) << "Configuring attribute reporting for on/off cluster";
    ZigbeeClusterReply *reportingReply = onOffInputCluster->configureReporting({onOffConfig});
    connect(reportingReply, &ZigbeeClusterReply::finished, this, [=](){
        if (reportingReply->error() != ZigbeeClusterReply::ErrorNoError) {
            qCWarning(m_dc) << "Failed configure attribute reporting on on/off cluster" << reportingReply->error();
        } else {
            qCDebug(m_dc) << "Attribute reporting configuration finished for on/off cluster" << reportingReply->responseData().toHex() << ZigbeeClusterLibrary::parseAttributeReportingStatusRecords(reportingReply->responseFrame().payload);
        }
    });
}

void ZigbeeIntegrationPlugin::configureLevelControlInputClusterAttributeReporting(ZigbeeNodeEndpoint *endpoint)
{
    ZigbeeClusterLibrary::AttributeReportingConfiguration levelConfig;
    levelConfig.attributeId = ZigbeeClusterLevelControl::AttributeCurrentLevel;
    levelConfig.dataType = Zigbee::Uint8;
    levelConfig.reportableChange = ZigbeeDataType(static_cast<quint8>(1)).data();

    ZigbeeClusterReply *reportingReply = endpoint->getInputCluster(ZigbeeClusterLibrary::ClusterIdLevelControl)->configureReporting({levelConfig});
    connect(reportingReply, &ZigbeeClusterReply::finished, this, [=](){
        if (reportingReply->error() != ZigbeeClusterReply::ErrorNoError) {
            qCWarning(m_dc) << "Failed to configure Level Control input cluster attribute reporting" << reportingReply->error();
        } else {
            qCDebug(m_dc) << "Configured attribute reporting for Level Control Input cluster";
        }
    });
}

void ZigbeeIntegrationPlugin::configureColorControlInputClusterAttributeReporting(ZigbeeNodeEndpoint *endpoint)
{
    ZigbeeClusterLibrary::AttributeReportingConfiguration xConfig;
    xConfig.attributeId = ZigbeeClusterColorControl::AttributeCurrentX;
    xConfig.dataType = Zigbee::Uint16;
    xConfig.reportableChange = ZigbeeDataType(static_cast<quint16>(1)).data();

    ZigbeeClusterLibrary::AttributeReportingConfiguration yConfig;
    yConfig.attributeId = ZigbeeClusterColorControl::AttributeCurrentY;
    yConfig.dataType = Zigbee::Uint16;
    yConfig.reportableChange = ZigbeeDataType(static_cast<quint16>(1)).data();

    ZigbeeClusterLibrary::AttributeReportingConfiguration tempConfig;
    tempConfig.attributeId = ZigbeeClusterColorControl::AttributeColorTemperatureMireds;
    tempConfig.dataType = Zigbee::Uint16;
    tempConfig.reportableChange = ZigbeeDataType(static_cast<quint16>(1)).data();

    ZigbeeClusterReply *reportingReply = endpoint->getInputCluster(ZigbeeClusterLibrary::ClusterIdColorControl)->configureReporting({xConfig, yConfig, tempConfig});
    connect(reportingReply, &ZigbeeClusterReply::finished, this, [=](){
        if (reportingReply->error() != ZigbeeClusterReply::ErrorNoError) {
            qCWarning(m_dc) << "Failed to configure Color Control input cluster attribute reporting" << reportingReply->error();
        } else {
            qCDebug(m_dc) << "Configured attribute reporting for Color Control Input cluster";
        }
    });
}

void ZigbeeIntegrationPlugin::configureElectricalMeasurementInputClusterAttributeReporting(ZigbeeNodeEndpoint *endpoint)
{
    ZigbeeClusterLibrary::AttributeReportingConfiguration acTotalPowerConfig;
    acTotalPowerConfig.attributeId = ZigbeeClusterElectricalMeasurement::AttributeACPhaseAMeasurementActivePower;
    acTotalPowerConfig.dataType = Zigbee::Int16;
    acTotalPowerConfig.minReportingInterval = 1; // we want currentPower asap
    acTotalPowerConfig.maxReportingInterval = 30;
    acTotalPowerConfig.reportableChange = ZigbeeDataType(static_cast<quint16>(1)).data();

    ZigbeeClusterLibrary::AttributeReportingConfiguration rmsVoltageConfig;
    rmsVoltageConfig.attributeId = ZigbeeClusterElectricalMeasurement::AttributeACPhaseAMeasurementRMSVoltage;
    rmsVoltageConfig.dataType = Zigbee::Int16;
    rmsVoltageConfig.minReportingInterval = 50;
    rmsVoltageConfig.maxReportingInterval = 120;
    rmsVoltageConfig.reportableChange = ZigbeeDataType(static_cast<quint16>(1)).data();

    ZigbeeClusterLibrary::AttributeReportingConfiguration rmsCurrentConfig;
    rmsCurrentConfig.attributeId = ZigbeeClusterElectricalMeasurement::AttributeACPhaseAMeasurementRMSCurrent;
    rmsCurrentConfig.dataType = Zigbee::Int16;
    rmsCurrentConfig.minReportingInterval = 10;
    rmsCurrentConfig.maxReportingInterval = 120;
    rmsCurrentConfig.reportableChange = ZigbeeDataType(static_cast<quint16>(1)).data();

    ZigbeeClusterReply *reportingReply = endpoint->getInputCluster(ZigbeeClusterLibrary::ClusterIdElectricalMeasurement)->configureReporting({acTotalPowerConfig, rmsVoltageConfig, rmsCurrentConfig});
    connect(reportingReply, &ZigbeeClusterReply::finished, this, [=](){
        if (reportingReply->error() != ZigbeeClusterReply::ErrorNoError) {
            qCWarning(m_dc) << "Failed to configure electrical measurement cluster attribute reporting" << reportingReply->error();
        } else {
            qCDebug(m_dc) << "Enabled attribute reporting successfully";
        }
    });
}

void ZigbeeIntegrationPlugin::configureMeteringInputClusterAttributeReporting(ZigbeeNodeEndpoint *endpoint)
{
    ZigbeeClusterMetering* meteringCluster = endpoint->inputCluster<ZigbeeClusterMetering>(ZigbeeClusterLibrary::ClusterIdMetering);
    if (!meteringCluster) {
        qCWarning(m_dc) << "No metering cluster on this endpoint";
        return;
    }
    meteringCluster->readFormatting();
    meteringCluster->readAttributes({ZigbeeClusterMetering::AttributeInstantaneousDemand, ZigbeeClusterMetering::AttributeCurrentSummationDelivered});

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

}

void ZigbeeIntegrationPlugin::configureTemperatureMeasurementInputClusterAttributeReporting(ZigbeeNodeEndpoint *endpoint)
{
    ZigbeeClusterTemperatureMeasurement* temperatureMeasurementCluster = endpoint->inputCluster<ZigbeeClusterTemperatureMeasurement>(ZigbeeClusterLibrary::ClusterIdTemperatureMeasurement);
    if (!temperatureMeasurementCluster) {
        qCWarning(m_dc) << "No temperature measurement cluster on this endpoint";
        return;
    }

    ZigbeeClusterLibrary::AttributeReportingConfiguration measuredValueReportingConfig;
    measuredValueReportingConfig.attributeId = ZigbeeClusterTemperatureMeasurement::AttributeMeasuredValue;
    measuredValueReportingConfig.dataType = Zigbee::Int16;
    measuredValueReportingConfig.minReportingInterval = 5;
    measuredValueReportingConfig.maxReportingInterval = 1200;
    measuredValueReportingConfig.reportableChange = ZigbeeDataType(static_cast<qint16>(10)).data();

    ZigbeeClusterReply *reportingReply = temperatureMeasurementCluster->configureReporting({measuredValueReportingConfig});
    connect(reportingReply, &ZigbeeClusterReply::finished, this, [=](){
        if (reportingReply->error() != ZigbeeClusterReply::ErrorNoError) {
            qCWarning(m_dc) << "Failed to configure temperature measurement cluster attribute reporting" << reportingReply->error();
        }
    });
}

void ZigbeeIntegrationPlugin::configureRelativeHumidityMeasurementInputClusterAttributeReporting(ZigbeeNodeEndpoint *endpoint)
{
    ZigbeeClusterRelativeHumidityMeasurement* relativeHumidityMeasurementCluster = endpoint->inputCluster<ZigbeeClusterRelativeHumidityMeasurement>(ZigbeeClusterLibrary::ClusterIdRelativeHumidityMeasurement);
    if (!relativeHumidityMeasurementCluster) {
        qCWarning(m_dc) << "No relative humidity cluster on this endpoint";
        return;
    }

    relativeHumidityMeasurementCluster->readAttributes({ZigbeeClusterRelativeHumidityMeasurement::AttributeMeasuredValue});

    ZigbeeClusterLibrary::AttributeReportingConfiguration measuredValueReportingConfig;
    measuredValueReportingConfig.attributeId = ZigbeeClusterRelativeHumidityMeasurement::AttributeMeasuredValue;
    measuredValueReportingConfig.dataType = Zigbee::Int16;
    measuredValueReportingConfig.minReportingInterval = 60; // We want currentPower asap
    measuredValueReportingConfig.maxReportingInterval = 1200;
    measuredValueReportingConfig.reportableChange = ZigbeeDataType(static_cast<quint8>(1)).data();

    ZigbeeClusterReply *reportingReply = relativeHumidityMeasurementCluster->configureReporting({measuredValueReportingConfig});
    connect(reportingReply, &ZigbeeClusterReply::finished, this, [=](){
        if (reportingReply->error() != ZigbeeClusterReply::ErrorNoError) {
            qCWarning(m_dc) << "Failed to configure temperature measurement cluster attribute reporting" << reportingReply->error();
        }
    });
}

void ZigbeeIntegrationPlugin::configureIlluminanceMeasurementInputClusterAttributeReporting(ZigbeeNodeEndpoint *endpoint)
{
    ZigbeeClusterIlluminanceMeasurement* illuminanceMeasurementCluster = endpoint->inputCluster<ZigbeeClusterIlluminanceMeasurement>(ZigbeeClusterLibrary::ClusterIdIlluminanceMeasurement);
    if (!illuminanceMeasurementCluster) {
        qCWarning(m_dc) << "No illuminance measurement cluster on this endpoint";
        return;
    }
    ZigbeeClusterLibrary::AttributeReportingConfiguration measuredValueReportingConfig;
    measuredValueReportingConfig.attributeId = ZigbeeClusterIlluminanceMeasurement::AttributeMeasuredValue;
    measuredValueReportingConfig.dataType = Zigbee::Uint16;
    measuredValueReportingConfig.minReportingInterval = 5;
    measuredValueReportingConfig.maxReportingInterval = 1200;
    measuredValueReportingConfig.reportableChange = ZigbeeDataType(static_cast<quint16>(10)).data();

    ZigbeeClusterReply *reportingReply = illuminanceMeasurementCluster->configureReporting({measuredValueReportingConfig});
    connect(reportingReply, &ZigbeeClusterReply::finished, this, [=](){
        if (reportingReply->error() != ZigbeeClusterReply::ErrorNoError) {
            qCWarning(m_dc) << "Failed to configure illuminance measurement cluster attribute reporting" << reportingReply->error();
        } else {
            qCDebug(m_dc) << "Configured illuminance measurement input cluster attribue reporting successfully";
        }
    });
}

void ZigbeeIntegrationPlugin::configureOccupancySensingInputClusterAttributeReporting(ZigbeeNodeEndpoint *endpoint)
{
    ZigbeeClusterOccupancySensing *occupancySensingInputCluster = endpoint->inputCluster<ZigbeeClusterOccupancySensing>(ZigbeeClusterLibrary::ClusterIdOccupancySensing);
    if (!occupancySensingInputCluster) {
        qCWarning(m_dc) << "No occupancy sensing cluster on this endpoint";
        return;
    }
    ZigbeeClusterLibrary::AttributeReportingConfiguration reportingConfig;
    reportingConfig.attributeId = ZigbeeClusterOccupancySensing::AttributeOccupancy;
    reportingConfig.dataType = Zigbee::BitMap8;
    reportingConfig.minReportingInterval = 0;
    reportingConfig.maxReportingInterval = 300;

    ZigbeeClusterReply *reportingReply = occupancySensingInputCluster->configureReporting({reportingConfig});
    connect(reportingReply, &ZigbeeClusterReply::finished, this, [=](){
        if (reportingReply->error() != ZigbeeClusterReply::ErrorNoError) {
            qCWarning(m_dc) << "Failed to configure occupancy cluster attribute reporting" << reportingReply->error();
        }
    });
}

void ZigbeeIntegrationPlugin::configureFanControlInputClusterAttributeReporting(ZigbeeNodeEndpoint *endpoint)
{
    ZigbeeClusterFanControl *fanControlInputCluster = endpoint->inputCluster<ZigbeeClusterFanControl>(ZigbeeClusterLibrary::ClusterIdFanControl);
    if (!fanControlInputCluster) {
        qCWarning(m_dc) << "No fan control cluster on this endpoint";
        return;
    }
    ZigbeeClusterLibrary::AttributeReportingConfiguration reportingConfig;
    reportingConfig.attributeId = ZigbeeClusterFanControl::AttributeFanMode;
    reportingConfig.dataType = Zigbee::BitMap8;
    reportingConfig.minReportingInterval = 0;
    reportingConfig.maxReportingInterval = 300;

    ZigbeeClusterReply *reportingReply = fanControlInputCluster->configureReporting({reportingConfig});
    connect(reportingReply, &ZigbeeClusterReply::finished, this, [=](){
        if (reportingReply->error() != ZigbeeClusterReply::ErrorNoError) {
            qCWarning(m_dc) << "Failed to configure fan control attribute reporting" << reportingReply->error();
        }
    });
}

void ZigbeeIntegrationPlugin::connectToPowerConfigurationInputCluster(Thing *thing, ZigbeeNodeEndpoint *endpoint)
{
    ZigbeeClusterPowerConfiguration *powerCluster = endpoint->inputCluster<ZigbeeClusterPowerConfiguration>(ZigbeeClusterLibrary::ClusterIdPowerConfiguration);
    if (!powerCluster) {
        qCWarning(m_dc) << "No power configuration cluster on" << thing->name() << "and endpoint" << endpoint->endpointId();
        return;
    }

    if (thing->thingClass().hasStateType("batteryLevel") && powerCluster->hasAttribute(ZigbeeClusterPowerConfiguration::AttributeBatteryPercentageRemaining)) {
        thing->setStateValue("batteryLevel", powerCluster->batteryPercentage());
    }
    if (powerCluster->hasAttribute(ZigbeeClusterPowerConfiguration::AttributeBatteryAlarmState)) {
        thing->setStateValue("batteryCritical", powerCluster->batteryAlarmState() > 0);
    } else {
        thing->setStateValue("batteryCritical", thing->stateValue("batteryLevel").toInt() < 10);
    }

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

    powerCluster->readAttributes({
                                 ZigbeeClusterPowerConfiguration::AttributeBatteryPercentageRemaining,
                                 ZigbeeClusterPowerConfiguration::AttributeBatteryAlarmState
                             });
}

void ZigbeeIntegrationPlugin::connectToThermostatCluster(Thing *thing, ZigbeeNodeEndpoint *endpoint)
{
    ZigbeeClusterThermostat *thermostatCluster = endpoint->inputCluster<ZigbeeClusterThermostat>(ZigbeeClusterLibrary::ClusterIdThermostat);
    if (!thermostatCluster) {
        qCWarning(m_dc) << "No thermostat cluster on" << thing->name() << "and endpoint" << endpoint->endpointId();
        return;
    }

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

void ZigbeeIntegrationPlugin::connectToOnOffInputCluster(Thing *thing, ZigbeeNodeEndpoint *endpoint, const QString &stateName)
{
    ZigbeeClusterOnOff *onOffCluster = endpoint->inputCluster<ZigbeeClusterOnOff>(ZigbeeClusterLibrary::ClusterIdOnOff);
    if (!onOffCluster) {
        qCWarning(m_dc) << "No power OnOff cluster on" << thing->name() << "and endpoint" << endpoint->endpointId();
        return;
    }

    if (onOffCluster->hasAttribute(ZigbeeClusterOnOff::AttributeOnOff)) {
        thing->setStateValue(stateName, onOffCluster->power());
    }
    onOffCluster->readAttributes({ZigbeeClusterOnOff::AttributeOnOff});
    connect(onOffCluster, &ZigbeeClusterOnOff::powerChanged, thing, [thing, stateName](bool power){
        thing->setStateValue(stateName, power);
    });
}

void ZigbeeIntegrationPlugin::connectToLevelControlInputCluster(Thing *thing, ZigbeeNodeEndpoint *endpoint, const QString &stateName)
{
    ZigbeeClusterLevelControl *levelControlCluster = endpoint->inputCluster<ZigbeeClusterLevelControl>(ZigbeeClusterLibrary::ClusterIdLevelControl);
    if (!levelControlCluster) {
        qCWarning(m_dc) << "No level control cluster on" << thing->name() << "and endpoint" << endpoint->endpointId();
        return;
    }

    if (levelControlCluster->hasAttribute(ZigbeeClusterLevelControl::AttributeCurrentLevel)) {
        thing->setStateValue(stateName, levelControlCluster->currentLevel() * 100 / 255);
    }
    levelControlCluster->readAttributes({ZigbeeClusterLevelControl::AttributeCurrentLevel});
    connect(levelControlCluster, &ZigbeeClusterLevelControl::currentLevelChanged, thing, [thing, stateName](int currentLevel){
        thing->setStateValue(stateName, currentLevel * 100 / 255);
    });
}

void ZigbeeIntegrationPlugin::connectToColorControlInputCluster(Thing *thing, ZigbeeNodeEndpoint *endpoint)
{
    ZigbeeClusterColorControl *colorControlCluster = endpoint->inputCluster<ZigbeeClusterColorControl>(ZigbeeClusterLibrary::ClusterIdColorControl);
    if (!colorControlCluster) {
        qCWarning(m_dc) << "No color control cluster on" << thing->name() << "and endpoint" << endpoint->endpointId();
        return;
    }
    if (thing->hasState("color")) {
        if (colorControlCluster->hasAttribute(ZigbeeClusterColorControl::AttributeCurrentX)
                && colorControlCluster->hasAttribute(ZigbeeClusterColorControl::AttributeCurrentY)) {
            quint16 colorX = colorControlCluster->attribute(ZigbeeClusterColorControl::AttributeCurrentX).dataType().toUInt16();
            quint16 colorY = colorControlCluster->attribute(ZigbeeClusterColorControl::AttributeCurrentY).dataType().toUInt16();
            QColor color = ZigbeeUtils::convertXYToColor(QPoint(colorX, colorY));
            thing->setStateValue("color", color);
        }

        colorControlCluster->readAttributes({ZigbeeClusterColorControl::AttributeCurrentX, ZigbeeClusterColorControl::AttributeCurrentY});
        connect(colorControlCluster, &ZigbeeClusterColorControl::attributeChanged, thing, [thing, colorControlCluster](const ZigbeeClusterAttribute &attribute){
            if (attribute.id() == ZigbeeClusterColorControl::AttributeCurrentX || attribute.id() == ZigbeeClusterColorControl::AttributeCurrentY) {
                quint16 colorX = colorControlCluster->attribute(ZigbeeClusterColorControl::AttributeCurrentX).dataType().toUInt16();
                quint16 colorY = colorControlCluster->attribute(ZigbeeClusterColorControl::AttributeCurrentY).dataType().toUInt16();
                QColor color = ZigbeeUtils::convertXYToColor(QPoint(colorX, colorY));
                thing->setStateValue("color", color);
            }
        });
    }
    if (thing->hasState("colorTemperature")) {
        if (colorControlCluster->hasAttribute(ZigbeeClusterColorControl::AttributeColorTemperatureMireds)) {
            int colorTemperature = mapColorTemperatureToScaledValue(thing, colorControlCluster->colorTemperatureMireds());
            thing->setStateValue("colorTemperature", colorTemperature);
        }
        colorControlCluster->readAttributes({ZigbeeClusterColorControl::AttributeColorTemperatureMireds});
        connect(colorControlCluster, &ZigbeeClusterColorControl::colorTemperatureMiredsChanged, thing, [this, thing](quint16 colorTemperature) {
            thing->setStateValue("colorTemperature", mapColorTemperatureToScaledValue(thing, colorTemperature));
        });
    }
}

void ZigbeeIntegrationPlugin::connectToElectricalMeasurementCluster(Thing *thing, ZigbeeNodeEndpoint *endpoint)
{
    ZigbeeClusterElectricalMeasurement *electricalMeasurementCluster = endpoint->inputCluster<ZigbeeClusterElectricalMeasurement>(ZigbeeClusterLibrary::ClusterIdElectricalMeasurement);
    if (!electricalMeasurementCluster) {
        qCWarning(m_dc) << "No electrical measurement cluster on" << thing->name() << "and endpoint" << endpoint->endpointId();
        return;
    }

    connect(electricalMeasurementCluster, &ZigbeeClusterElectricalMeasurement::activePowerPhaseAChanged, thing, [thing](qint16 activePowerPhaseA){
        thing->setStateValue("currentPower", activePowerPhaseA);
    });
}

void ZigbeeIntegrationPlugin::connectToMeteringCluster(Thing *thing, ZigbeeNodeEndpoint *endpoint)
{
    ZigbeeClusterMetering *meteringCluster = endpoint->inputCluster<ZigbeeClusterMetering>(ZigbeeClusterLibrary::ClusterIdMetering);
    if (!meteringCluster) {
        qCWarning(m_dc) << "No metering cluster on" << thing->name() << "and endpoint" << endpoint->endpointId();
        return;
    }

    meteringCluster->readFormatting();

    connect(meteringCluster, &ZigbeeClusterMetering::currentSummationDeliveredChanged, thing, [=](quint64 currentSummationDelivered){
        thing->setStateValue("totalEnergyConsumed", 1.0 * currentSummationDelivered * meteringCluster->multiplier() / meteringCluster->divisor());
    });

    connect(meteringCluster, &ZigbeeClusterMetering::instantaneousDemandChanged, thing, [=](qint32 instantaneousDemand){
        thing->setStateValue("currentPower", instantaneousDemand);
    });
}

void ZigbeeIntegrationPlugin::connectToTemperatureMeasurementInputCluster(Thing *thing, ZigbeeNodeEndpoint *endpoint)
{
    ZigbeeClusterTemperatureMeasurement *temperatureMeasurementCluster = endpoint->inputCluster<ZigbeeClusterTemperatureMeasurement>(ZigbeeClusterLibrary::ClusterIdTemperatureMeasurement);
    if (!temperatureMeasurementCluster) {
        qCWarning(m_dc) << "No temperature measurement cluster on" << thing->name() << endpoint;
        return;
    }

    if (temperatureMeasurementCluster->hasAttribute(ZigbeeClusterTemperatureMeasurement::AttributeMaxMeasuredValue)) {
        thing->setStateValue("temperature", temperatureMeasurementCluster->temperature());
    }
    temperatureMeasurementCluster->readAttributes({ZigbeeClusterTemperatureMeasurement::AttributeMeasuredValue});
    connect(temperatureMeasurementCluster, &ZigbeeClusterTemperatureMeasurement::temperatureChanged, thing, [=](double temperature) {
        qCDebug(m_dc) << "Temperature for" << thing->name() << "changed to:" << temperature;
        thing->setStateValue("temperature", temperature);
    });
}

void ZigbeeIntegrationPlugin::connectToRelativeHumidityMeasurementInputCluster(Thing *thing, ZigbeeNodeEndpoint *endpoint)
{
    ZigbeeClusterRelativeHumidityMeasurement *relativeHumidityMeasurementCluster = endpoint->inputCluster<ZigbeeClusterRelativeHumidityMeasurement>(ZigbeeClusterLibrary::ClusterIdRelativeHumidityMeasurement);
    if (!relativeHumidityMeasurementCluster) {
        qCWarning(m_dc) << "No relative humidity measurement cluster on" << thing->name() << endpoint;
        return;
    }

    if (relativeHumidityMeasurementCluster->hasAttribute(ZigbeeClusterRelativeHumidityMeasurement::AttributeMaxMeasuredValue)) {
        thing->setStateValue("humidity", relativeHumidityMeasurementCluster->humidity());
    }
    relativeHumidityMeasurementCluster->readAttributes({ZigbeeClusterRelativeHumidityMeasurement::AttributeMeasuredValue});
    connect(relativeHumidityMeasurementCluster, &ZigbeeClusterRelativeHumidityMeasurement::humidityChanged, thing, [=](double humidity) {
        qCDebug(m_dc) << "Humidity for" << thing->name() << "changed to:" << humidity;
        thing->setStateValue("humidity", humidity);
    });
}

void ZigbeeIntegrationPlugin::connectToIasZoneInputCluster(Thing *thing, ZigbeeNodeEndpoint *endpoint, const QString &alarmStateName, bool inverted)
{
    ZigbeeClusterIasZone *iasZoneCluster = endpoint->inputCluster<ZigbeeClusterIasZone>(ZigbeeClusterLibrary::ClusterIdIasZone);
    if (!iasZoneCluster) {
        qCWarning(m_dc) << "Could not find IAS zone cluster on" << thing << endpoint;
        return;
    }

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

void ZigbeeIntegrationPlugin::connectToIlluminanceMeasurementInputCluster(Thing *thing, ZigbeeNodeEndpoint *endpoint)
{
    ZigbeeClusterIlluminanceMeasurement *illuminanceMeasurementCluster = endpoint->inputCluster<ZigbeeClusterIlluminanceMeasurement>(ZigbeeClusterLibrary::ClusterIdIlluminanceMeasurement);
    if (!illuminanceMeasurementCluster) {
        qCWarning(m_dc) << "No illuminance measurement cluster on" << thing->name() << endpoint;
        return;
    }

    if (illuminanceMeasurementCluster->hasAttribute(ZigbeeClusterIlluminanceMeasurement::AttributeMaxMeasuredValue)) {
        thing->setStateValue("lightIntensity", qPow(10, (illuminanceMeasurementCluster->illuminance() - 1) / 10000));
    }
    illuminanceMeasurementCluster->readAttributes({ZigbeeClusterIlluminanceMeasurement::AttributeMeasuredValue});
    connect(illuminanceMeasurementCluster, &ZigbeeClusterIlluminanceMeasurement::illuminanceChanged, thing, [=](double illuminance) {
        qCDebug(m_dc) << "Illuminance for" << thing->name() << "changed to:" << illuminance;
        thing->setStateValue("lightIntensity", qPow(10, (illuminance - 1) / 10000));
    });
}

void ZigbeeIntegrationPlugin::connectToOccupancySensingInputCluster(Thing *thing, ZigbeeNodeEndpoint *endpoint)
{
    ZigbeeClusterOccupancySensing *occupancyCluster = endpoint->inputCluster<ZigbeeClusterOccupancySensing>(ZigbeeClusterLibrary::ClusterIdOccupancySensing);
    if (!occupancyCluster) {
        qCWarning(m_dc) << "Occupancy cluster not found on" << thing;
    } else {
        connect(occupancyCluster, &ZigbeeClusterOccupancySensing::occupancyChanged, thing, [=](bool occupancy){
            qCDebug(m_dc) << thing << "occupancy cluster changed" << occupancy;
            thing->setStateValue("isPresent", occupancy);
            if (occupancy) {
                thing->setStateValue("lastSeenTime", QDateTime::currentMSecsSinceEpoch() / 1000);
            }
        });
    }
}

void ZigbeeIntegrationPlugin::connectToFanControlInputCluster(Thing *thing, ZigbeeNodeEndpoint *endpoint)
{
    ZigbeeClusterFanControl *fanControlCluster = endpoint->inputCluster<ZigbeeClusterFanControl>(ZigbeeClusterLibrary::ClusterIdFanControl);
    if (!fanControlCluster) {
        qCWarning(m_dc) << "Fan control cluster not found on" << thing;
    } else {
        connect(fanControlCluster, &ZigbeeClusterFanControl::fanModeChanged, thing, [=](ZigbeeClusterFanControl::FanMode fanMode){
            qCDebug(m_dc) << thing << "fan mode changed" << fanMode;
            switch (fanMode) {
            case ZigbeeClusterFanControl::FanModeOff:
                thing->setStateValue("power", false);
                break;
            case ZigbeeClusterFanControl::FanModeLow:
                thing->setStateValue("power", true);
                thing->setStateValue("flowRate", 1);
                break;
            case ZigbeeClusterFanControl::FanModeMedium:
                thing->setStateValue("power", true);
                thing->setStateValue("flowRate", 2);
                break;
            case ZigbeeClusterFanControl::FanModeHigh:
                thing->setStateValue("power", true);
                thing->setStateValue("flowRate", 3);
                break;
            case ZigbeeClusterFanControl::FanModeOn:
                thing->setStateValue("power", true);
                break;
            case ZigbeeClusterFanControl::FanModeAuto:
                thing->setStateValue("power", true);
                break;
            case ZigbeeClusterFanControl::FanModeSmart:
                thing->setStateValue("power", true);
                break;
            }
        });
    }
}

void ZigbeeIntegrationPlugin::connectToOtaOutputCluster(Thing *thing, ZigbeeNodeEndpoint *endpoint)
{
    ZigbeeClusterOta *otaCluster = endpoint->outputCluster<ZigbeeClusterOta>(ZigbeeClusterLibrary::ClusterIdOtaUpgrade);
    if (!otaCluster) {
        qCWarning(m_dc) << "OTA cluster not found for" << thing->name();
        return;
    }
    qCDebug(m_dc) << "Connecting to OTA cluster for" << thing->name();
    qCDebug(m_dc) << "Sending image notify to" << thing->name();

    connect(endpoint->node(), &ZigbeeNode::lastSeenChanged, otaCluster, [otaCluster, thing, this](){
        if (!otaCluster->property("imageNotifyPending").toBool() && otaCluster->property("lastFirmwareCheck").toDateTime().addSecs(60 * 60 * 24) < QDateTime::currentDateTime()) {
            qCDebug(m_dc) << "Sending image notify to" << thing->name();
            ZigbeeClusterReply *reply = otaCluster->sendImageNotify();
            otaCluster->setProperty("imageNotifyPending", true);

            connect(reply, &ZigbeeClusterReply::finished, thing, [this, reply, otaCluster](){
                qCDebug(m_dc) << "Image notify command finished" << reply->error();
                otaCluster->setProperty("imageNotifyPending", false);
            });
        }
    });

    connect(otaCluster, &ZigbeeClusterOta::queryNextImageRequestReceived, thing, [this, otaCluster, thing](quint8 transactionSequenceNumber, quint16 manufacturerCode, quint16 imageType, quint32 currentFileVersion, quint16 /*hardwareVersion*/){
        otaCluster->setProperty("lastFirmwareCheck", QDateTime::currentDateTime());

        ZigbeeNode *node = nodeForThing(thing);
        FirmwareIndexEntry newInfo = checkFirmwareAvailability(m_firmwareIndex, manufacturerCode, imageType, currentFileVersion, node->modelName());
        ZigbeeClusterOta::FileVersion currentParsed = ZigbeeClusterOta::parseFileVersion(currentFileVersion);
        thing->setStateValue("currentVersion", QString("%0.%1.%2.%3")
                             .arg(currentParsed.applicationRelease)
                             .arg(currentParsed.applicationBuild)
                             .arg(currentParsed.stackRelease)
                             .arg(currentParsed.stackBuild));
        if (newInfo.fileVersion > 0) {
            ZigbeeClusterOta::FileVersion newParsed = ZigbeeClusterOta::parseFileVersion(newInfo.fileVersion);
            qCDebug(m_dc) << QString("Device %0 requested firmware. Old version: %1.%2.%3.%4, new version: %5.%6.%7.%8")
                             .arg(thing->name())
                             .arg(currentParsed.applicationRelease)
                             .arg(currentParsed.applicationBuild)
                             .arg(currentParsed.stackRelease)
                             .arg(currentParsed.stackBuild)
                             .arg(newParsed.applicationRelease)
                             .arg(newParsed.applicationBuild)
                             .arg(newParsed.stackRelease)
                             .arg(newParsed.stackBuild);
            thing->setStateValue("availableVersion", QString("%0.%1.%2.%3")
                                 .arg(newParsed.applicationRelease)
                                 .arg(newParsed.applicationBuild)
                                 .arg(newParsed.stackRelease)
                                 .arg(newParsed.stackBuild));
            thing->setStateValue("updateStatus", "available");
            thing->setStateValue("updateProgress", 0);

            if (!m_enabledFirmwareUpdates.contains(thing)) {
                qCDebug(m_dc) << "Update not enabled for thing" << thing->name();
                otaCluster->sendQueryNextImageResponse(transactionSequenceNumber, ZigbeeClusterOta::StatusCodeNoImageAvailable);
                return;
            }

            thing->setStateValue("updateStatus", "updating");
            if (firmwareFileExists(newInfo)) {
                qCDebug(m_dc) << "Firmware file is present. Starting update...";
                otaCluster->sendQueryNextImageResponse(transactionSequenceNumber, ZigbeeClusterOta::StatusCodeSuccess, manufacturerCode, imageType, newInfo.fileVersion, newInfo.fileSize);
            } else {
                qCDebug(m_dc) << "Downloading firmware file...";
                FetchFirmwareReply *reply = fetchFirmware(newInfo);
                connect(reply, &FetchFirmwareReply::finished, otaCluster, [=](){
                    if (firmwareFileExists(newInfo)) {
                        qCDebug(m_dc) << "Firmware file downloaded successfully. Starting update...";
                        otaCluster->sendQueryNextImageResponse(transactionSequenceNumber, ZigbeeClusterOta::StatusCodeSuccess, manufacturerCode, imageType, newInfo.fileVersion, newInfo.fileSize);
                    } else {
                        qCWarning(m_dc) << "Failed to download firmware.";
                        otaCluster->sendQueryNextImageResponse(transactionSequenceNumber, ZigbeeClusterOta::StatusCodeNoImageAvailable);
                        thing->setStateValue("availableVersion", "-");
                        thing->setStateValue("updateStatus", "idle");
                        thing->setStateValue("updateProgress", 0);
                    }
                });
            }
        } else {
            qCDebug(m_dc) << QString("Device %0 requested firmware. Old version: %1.%2.%3.%4, no new version available.").arg(thing->name()).arg(currentParsed.applicationRelease).arg(currentParsed.applicationBuild).arg(currentParsed.stackRelease).arg(currentParsed.stackBuild);
            thing->setStateValue("availableVersion", "-");
            thing->setStateValue("updateStatus", "idle");
            thing->setStateValue("updateProgress", 0);
        }
    });

    connect(otaCluster, &ZigbeeClusterOta::imageBlockRequestReceived, thing, [this, thing, otaCluster](quint8 transactionSequenceNumber, quint16 manufacturerCode, quint16 imageType, quint32 fileVersion, quint32 fileOffset, quint8 maximumDataSize, const ZigbeeAddress &requestNodeAddress, quint16 minimumBlockPeriod){
        Q_UNUSED(requestNodeAddress)
        Q_UNUSED(minimumBlockPeriod)
        if (!m_enabledFirmwareUpdates.contains(thing)) {
            // If nymea restarted during the process, or the upgrade process has been cancelled in some other way, let's cancel the OTA.
            qCDebug(m_dc) << "Device requested an image block but update is not enabled for" << thing->name();
            otaCluster->sendAbortImageBlockResponse(transactionSequenceNumber);
            return;
        }
        FirmwareIndexEntry info = firmwareInfo(manufacturerCode, imageType, fileVersion);
        QString fileName = firmwareFileName(info);
        QFile file(fileName);
        if (!file.open(QFile::ReadOnly)) {
            qCWarning(m_dc) << "Unable to open firmware file for reading";
            otaCluster->sendAbortImageBlockResponse(transactionSequenceNumber);
            m_enabledFirmwareUpdates.removeAll(thing);
            return;
        }
        if (!file.seek(fileOffset)) {
            qCWarning(m_dc) << "Unable to seek in firmware file";
            otaCluster->sendAbortImageBlockResponse(transactionSequenceNumber);
            m_enabledFirmwareUpdates.removeAll(thing);
            return;
        }
        QByteArray data = file.read(maximumDataSize);
        double progress = 100.0 * (fileOffset + data.size()) / info.fileSize;
        qCDebug(m_dc).nospace() << "Sending firmware image data block to device (" << progress << "%, offset: " << fileOffset << ", size: " << data.size() << ")";
        thing->setStateValue("updateProgress", qRound(progress));
        otaCluster->sendImageBlockResponse(transactionSequenceNumber, manufacturerCode, imageType, fileVersion, fileOffset, data);
    });

    connect(otaCluster, &ZigbeeClusterOta::upgradeEndRequestReceived, thing, [this, thing, otaCluster](quint8 transactionSequenceNumber, ZigbeeClusterOta::StatusCode statusCode, quint16 manufacturerCode, quint16 imageType, quint32 fileVersion) {
        m_enabledFirmwareUpdates.removeAll(thing);
        if (statusCode != ZigbeeClusterOta::StatusCodeSuccess) {
            qCWarning(m_dc) << "Image integrity checks failed on the device. Upgrade aborted. Status code:" << statusCode;
            QFile::remove(firmwareFileName(firmwareInfo(manufacturerCode, imageType, fileVersion)));
            thing->setStateValue("updateStatus", "idle");
            thing->setStateValue("updateProgress", 0);
            otaCluster->sendImageNotify();
            return;
        }

//        // Only for testing, to not actually finish the OTA
//        otaCluster->sendAbortUpgradeEndResponse(transactionSequenceNumber);
//        otaCluster->sendImageNotify();
//        return;

        //Validating the image checksums once again now to make sure it didn't change during the possibly long lasting data transmission.
        FirmwareIndexEntry info = firmwareInfo(manufacturerCode, imageType, fileVersion);
        if (firmwareFileExists(info)) { // this also checks the checksum
            qCDebug(m_dc) << "Completing update.";
            ZigbeeClusterReply *upgradeEndReply = otaCluster->sendUpgradeEndResponse(transactionSequenceNumber, manufacturerCode, imageType, fileVersion);
            connect(upgradeEndReply, &ZigbeeClusterReply::finished, thing, [thing, otaCluster, upgradeEndReply, this](){
                if (upgradeEndReply->error() != ZigbeeClusterReply::ErrorNoError) {
                    qCWarning(m_dc) << "Failed to send the upgrade end reply" << upgradeEndReply->error();
                } else {
                    qCDebug(m_dc) << "Update complete.";
                }

                otaCluster->setProperty("lastFirmwareCheck", QDateTime());

                thing->setStateValue("updateStatus", "idle");
                thing->setStateValue("updateProgress", 0);
            });
        } else {
            qCWarning(m_dc) << "Image verification failed. Aborting update.";
            otaCluster->sendAbortUpgradeEndResponse(transactionSequenceNumber);

            thing->setStateValue("updateStatus", "idle");
            thing->setStateValue("updateProgress", 0);

            // Notifying again to obtain the installed firmware version
            otaCluster->sendImageNotify();
        }
    });
}

void ZigbeeIntegrationPlugin::connectToAnalogInputCluster(Thing *thing, ZigbeeNodeEndpoint *endpoint, const QString &stateName)
{
    ZigbeeClusterAnalogInput *analogInputCluster = endpoint->inputCluster<ZigbeeClusterAnalogInput>(ZigbeeClusterLibrary::ClusterIdAnalogInput);
    if (!analogInputCluster) {
        qCWarning(m_dc) << "Analog input cluster not found on" << thing;
        return;
    }

    thing->setStateValue(stateName, analogInputCluster->presentValue());
    analogInputCluster->readAttributes({ZigbeeClusterAnalogInput::AttributePresentValue});

    connect(analogInputCluster, &ZigbeeClusterAnalogInput::presentValueChanged, thing, [thing, stateName](float presentValue){
        thing->setStateValue(stateName, presentValue);
    });
}

void ZigbeeIntegrationPlugin::executePowerOnOffInputCluster(ThingActionInfo *info, ZigbeeNodeEndpoint *endpoint)
{
    ZigbeeClusterOnOff *onOffCluster = endpoint->inputCluster<ZigbeeClusterOnOff>(ZigbeeClusterLibrary::ClusterIdOnOff);
    if (!onOffCluster) {
        qCWarning(m_dc) << "OnOff cluster not found for" << info->thing()->name();
        info->finish(Thing::ThingErrorHardwareFailure);
        return;
    }

    bool power = info->action().paramValue(info->thing()->thingClass().actionTypes().findByName("power").id()).toBool();
    ZigbeeClusterReply *reply = (power ? onOffCluster->commandOn() : onOffCluster->commandOff());
    connect(reply, &ZigbeeClusterReply::finished, info, [=](){
        if (reply->error() != ZigbeeClusterReply::ErrorNoError) {
            qCWarning(m_dc) << "Failed to set power on" << info->thing()->name() << reply->error();
            info->finish(Thing::ThingErrorHardwareFailure);
        } else {
            info->thing()->setStateValue("power", power);
            info->finish(Thing::ThingErrorNoError);
        }
    });
}

void ZigbeeIntegrationPlugin::executeBrightnessLevelControlInputCluster(ThingActionInfo *info, ZigbeeNodeEndpoint *endpoint)
{
    ZigbeeClusterLevelControl *levelCluster = endpoint->inputCluster<ZigbeeClusterLevelControl>(ZigbeeClusterLibrary::ClusterIdLevelControl);
    if (!levelCluster) {
        qCWarning(m_dc) << "Level control cluster not found for" << info->thing()->name();
        info->finish(Thing::ThingErrorHardwareFailure);
        return;
    }

    int brightness = info->action().param(info->thing()->thingClass().actionTypes().findByName("brightness").id()).value().toInt();
    quint8 level = static_cast<quint8>(qRound(255.0 * brightness / 100.0));

    ZigbeeClusterReply *reply = levelCluster->commandMoveToLevel(level, 5);
    connect(reply, &ZigbeeClusterReply::finished, info, [=](){
        if (reply->error() != ZigbeeClusterReply::ErrorNoError) {
            qCWarning(m_dc) << "Failed to set brightness on" << info->thing()->name() << reply->error();
            info->finish(Thing::ThingErrorHardwareFailure);
        } else {
            info->thing()->setStateValue("brightness", brightness);
            info->finish(Thing::ThingErrorNoError);
        }
    });
}

void ZigbeeIntegrationPlugin::executeColorTemperatureColorControlInputCluster(ThingActionInfo *info, ZigbeeNodeEndpoint *endpoint)
{
    ZigbeeClusterColorControl *colorCluster = endpoint->inputCluster<ZigbeeClusterColorControl>(ZigbeeClusterLibrary::ClusterIdColorControl);
    if (!colorCluster) {
        qCWarning(m_dc) << "Color control cluster not found for" << info->thing()->name();
        info->finish(Thing::ThingErrorHardwareFailure);
        return;
    }
    int colorTemperatureScaled = info->action().param(info->thing()->thingClass().actionTypes().findByName("colorTemperature").id()).value().toInt();

    quint16 colorTemperature = mapScaledValueToColorTemperature(info->thing(), colorTemperatureScaled);
    ZigbeeClusterReply *reply = colorCluster->commandMoveToColorTemperature(colorTemperature, 5);
    connect(reply, &ZigbeeClusterReply::finished, info, [=](){
        if (reply->error() != ZigbeeClusterReply::ErrorNoError) {
            qCWarning(m_dc) << "Failed to set color temperature on" << info->thing()->name() << reply->error();
            info->finish(Thing::ThingErrorHardwareFailure);
        } else {
            info->thing()->setStateValue("colorTemperature", colorTemperatureScaled);
            info->finish(Thing::ThingErrorNoError);
        }
    });
}

void ZigbeeIntegrationPlugin::executeColorColorControlInputCluster(ThingActionInfo *info, ZigbeeNodeEndpoint *endpoint)
{
    ZigbeeClusterColorControl *colorCluster = endpoint->inputCluster<ZigbeeClusterColorControl>(ZigbeeClusterLibrary::ClusterIdColorControl);
    if (!colorCluster) {
        qCWarning(m_dc) << "Color control cluster not found for" << info->thing()->name();
        info->finish(Thing::ThingErrorHardwareFailure);
        return;
    }

    QColor color = info->action().param(info->thing()->thingClass().actionTypes().findByName("color").id()).value().value<QColor>();
    QPoint xyColorInt = ZigbeeUtils::convertColorToXYInt(color);


    ZigbeeClusterReply *reply = colorCluster->commandMoveToColor(xyColorInt.x(), xyColorInt.y(), 5);

    connect(reply, &ZigbeeClusterReply::finished, info, [=](){
        if (reply->error() != ZigbeeClusterReply::ErrorNoError) {
            qCWarning(m_dc) << "Failed to set color on" << info->thing()->name() << reply->error();
            info->finish(Thing::ThingErrorHardwareFailure);
        } else {
            info->thing()->setStateValue("color", color);
            info->finish(Thing::ThingErrorNoError);
        }
    });
}

void ZigbeeIntegrationPlugin::executeIdentifyIdentifyInputCluster(ThingActionInfo *info, ZigbeeNodeEndpoint *endpoint)
{
    ZigbeeClusterIdentify *identifyCluster = endpoint->inputCluster<ZigbeeClusterIdentify>(ZigbeeClusterLibrary::ClusterIdIdentify);
    if (!identifyCluster) {
        qCWarning(m_dc) << "Could not find identify cluster for" << info->thing()->name();
        info->finish(Thing::ThingErrorHardwareFailure);
        return;
    }

    ZigbeeClusterReply *reply = identifyCluster->identify(2);
    connect(reply, &ZigbeeClusterReply::finished, this, [reply, info](){
        if (reply->error() != ZigbeeClusterReply::ErrorNoError) {
            info->finish(Thing::ThingErrorHardwareFailure);
        } else {
            info->finish(Thing::ThingErrorNoError);
        }
    });
}

void ZigbeeIntegrationPlugin::executePowerFanControlInputCluster(ThingActionInfo *info, ZigbeeNodeEndpoint *endpoint)
{
    ZigbeeClusterFanControl *fanControlCluster = endpoint->inputCluster<ZigbeeClusterFanControl>(ZigbeeClusterLibrary::ClusterIdFanControl);
    if (!fanControlCluster) {
        qCWarning(m_dc) << "Could not find fan control cluster for" << info->thing()->name();
        info->finish(Thing::ThingErrorHardwareFailure);
        return;
    }

    ZigbeeClusterReply *reply = fanControlCluster->setFanMode(info->action().paramValue(info->thing()->thingClass().actionTypes().findByName("power").id()).toBool() ? ZigbeeClusterFanControl::FanModeOn : ZigbeeClusterFanControl::FanModeOff);
    connect(reply, &ZigbeeClusterReply::finished, this, [reply, info](){
        if (reply->error() != ZigbeeClusterReply::ErrorNoError) {
            info->finish(Thing::ThingErrorHardwareFailure);
        } else {
            info->finish(Thing::ThingErrorNoError);
        }
    });
}

void ZigbeeIntegrationPlugin::executeFlowRateFanControlInputCluster(ThingActionInfo *info, ZigbeeNodeEndpoint *endpoint)
{
    ZigbeeClusterFanControl *fanControlCluster = endpoint->inputCluster<ZigbeeClusterFanControl>(ZigbeeClusterLibrary::ClusterIdFanControl);
    if (!fanControlCluster) {
        qCWarning(m_dc) << "Could not find fan control cluster for" << info->thing()->name();
        info->finish(Thing::ThingErrorHardwareFailure);
        return;
    }

    ZigbeeClusterReply *reply = fanControlCluster->setFanMode(static_cast<ZigbeeClusterFanControl::FanMode>(info->action().paramValue(info->thing()->thingClass().actionTypes().findByName("flowRate").id()).toUInt()));
    connect(reply, &ZigbeeClusterReply::finished, this, [reply, info](){
        if (reply->error() != ZigbeeClusterReply::ErrorNoError) {
            info->finish(Thing::ThingErrorHardwareFailure);
        } else {
            info->finish(Thing::ThingErrorNoError);
        }
    });
}

void ZigbeeIntegrationPlugin::executeImageNotifyOtaOutputCluster(ThingActionInfo *info, ZigbeeNodeEndpoint *endpoint)
{
    ZigbeeClusterOta *otaCluster = endpoint->outputCluster<ZigbeeClusterOta>(ZigbeeClusterLibrary::ClusterIdOtaUpgrade);
    if (!otaCluster) {
        qCWarning(m_dc) << "Could not find OTA cluster for" << info->thing()->name();
        info->finish(Thing::ThingErrorHardwareFailure);
        return;
    }

    otaCluster->sendImageNotify(); // imageNotify has no default response flag set. So don't wait for a confirmation
    info->finish(Thing::ThingErrorNoError);
}

void ZigbeeIntegrationPlugin::readColorTemperatureRange(Thing *thing, ZigbeeNodeEndpoint *endpoint)
{
    ZigbeeClusterColorControl *colorCluster = endpoint->inputCluster<ZigbeeClusterColorControl>(ZigbeeClusterLibrary::ClusterIdColorControl);
    if (!colorCluster) {
        qCWarning(m_dc) << "Failed to read color temperature range for" << thing << "because the color cluster could not be found on" << endpoint;
        return;
    }

    m_colorTemperatureRanges[thing] = ColorTemperatureRange();

    ZigbeeClusterReply *reply = colorCluster->readAttributes({ZigbeeClusterColorControl::AttributeColorTempPhysicalMinMireds, ZigbeeClusterColorControl::AttributeColorTempPhysicalMaxMireds});
    connect(reply, &ZigbeeClusterReply::finished, thing, [=](){
        if (reply->error() != ZigbeeClusterReply::ErrorNoError) {
            qCWarning(m_dc) << "Reading color temperature range attributes finished with error" << reply->error();
            qCWarning(m_dc) << "Failed to read color temperature min/max interval values. Using default values for" << thing << "[" << m_colorTemperatureRanges[thing].minValue << "," << m_colorTemperatureRanges[thing].maxValue << "] mired";
            return;
        }

        QList<ZigbeeClusterLibrary::ReadAttributeStatusRecord> attributeStatusRecords = ZigbeeClusterLibrary::parseAttributeStatusRecords(reply->responseFrame().payload);
        if (attributeStatusRecords.count() != 2) {
            qCWarning(m_dc) << "Did not receive temperature min/max interval values from" << thing;
            qCWarning(m_dc) << "Using default values for" << thing << "[" << m_colorTemperatureRanges[thing].minValue << "," << m_colorTemperatureRanges[thing].maxValue << "] mired" ;
            return;
        }

        foreach (const ZigbeeClusterLibrary::ReadAttributeStatusRecord &attributeStatusRecord, attributeStatusRecords) {
            if (attributeStatusRecord.attributeId == ZigbeeClusterColorControl::AttributeColorTempPhysicalMinMireds) {
                bool valueOk = false;
                quint16 minMiredsValue = attributeStatusRecord.dataType.toUInt16(&valueOk);
                if (!valueOk) {
                    qCWarning(m_dc) << "Failed to read color temperature min mireds attribute value and convert it" << attributeStatusRecord;
                    break;
                }

                m_colorTemperatureRanges[thing].minValue = minMiredsValue;
            }

            if (attributeStatusRecord.attributeId == ZigbeeClusterColorControl::AttributeColorTempPhysicalMaxMireds) {
                bool valueOk = false;
                quint16 maxMiredsValue = attributeStatusRecord.dataType.toUInt16(&valueOk);
                if (!valueOk) {
                    qCWarning(m_dc) << "Failed to read color temperature max mireds attribute value and convert it" << attributeStatusRecord;
                    break;
                }

                m_colorTemperatureRanges[thing].maxValue = maxMiredsValue;
            }
        }

        qCDebug(m_dc) << "Using lamp specific color temperature mireds interval for mapping" << thing << "[" <<  m_colorTemperatureRanges[thing].minValue << "," << m_colorTemperatureRanges[thing].maxValue << "] mired";
    });
}

quint16 ZigbeeIntegrationPlugin::mapScaledValueToColorTemperature(Thing *thing, int scaledColorTemperature)
{
    if (!m_colorTemperatureRanges.contains(thing)) {
        m_colorTemperatureRanges[thing] = ColorTemperatureRange();
    }

    int minScaleValue = thing->thingClass().stateTypes().findByName("colorTemperature").minValue().toInt();
    int maxScaleValue = thing->thingClass().stateTypes().findByName("colorTemperature").maxValue().toInt();
    double percentage = static_cast<double>((scaledColorTemperature - minScaleValue)) / (maxScaleValue - minScaleValue);
    double mappedValue = (m_colorTemperatureRanges[thing].maxValue - m_colorTemperatureRanges[thing].minValue) * percentage + m_colorTemperatureRanges[thing].minValue;
    return static_cast<quint16>(qRound(mappedValue));
}

int ZigbeeIntegrationPlugin::mapColorTemperatureToScaledValue(Thing *thing, quint16 colorTemperature)
{
    if (!m_colorTemperatureRanges.contains(thing)) {
        m_colorTemperatureRanges[thing] = ColorTemperatureRange();
    }

    int minScaleValue = thing->thingClass().stateTypes().findByName("colorTemperature").minValue().toInt();
    int maxScaleValue = thing->thingClass().stateTypes().findByName("colorTemperature").maxValue().toInt();
    double percentage = static_cast<double>((colorTemperature - m_colorTemperatureRanges[thing].minValue)) / (m_colorTemperatureRanges[thing].maxValue - m_colorTemperatureRanges[thing].minValue);
    double mappedValue = (maxScaleValue - minScaleValue) * percentage + minScaleValue;
    return static_cast<int>(qRound(mappedValue));
}

void ZigbeeIntegrationPlugin::readAttributesDelayed(ZigbeeCluster *cluster, const QList<quint16> &attributes, quint16 manufacturerCode)
{
    DelayedAttributeReadRequest request {cluster, attributes, manufacturerCode};
    m_delayedReadRequests[cluster->node()].append(request);
}

void ZigbeeIntegrationPlugin::writeAttributesDelayed(ZigbeeCluster *cluster, const QList<ZigbeeClusterLibrary::WriteAttributeRecord> &records, quint16 manufacturerCode)
{
    DelayedAttributeWriteRequest request {cluster, records, manufacturerCode};
    m_delayedWriteRequests[cluster->node()].append(request);
}

void ZigbeeIntegrationPlugin::setFirmwareIndexUrl(const QUrl &url)
{
    m_firmwareIndexUrl = url;
}

ZigbeeIntegrationPlugin::FirmwareIndexEntry ZigbeeIntegrationPlugin::checkFirmwareAvailability(const QList<FirmwareIndexEntry> &index, quint16 manufacturerCode, quint16 imageType, quint32 currentFileVersion, const QString &modelName) const
{
    qCDebug(m_dc) << "Requesting OTA for" << manufacturerCode << imageType << currentFileVersion;
    foreach (const FirmwareIndexEntry &image, index) {
        if (image.manufacturerCode == manufacturerCode
                && image.imageType == imageType
                && image.fileVersion > currentFileVersion
                && (image.minFileVersion == 0 || image.minFileVersion <= currentFileVersion)
                && (image.maxFileVersion == 0 || image.maxFileVersion >= currentFileVersion)
                && (image.modelId.isEmpty() || image.modelId == modelName)) {
            qCDebug(m_dc) << "Found OTA for" << manufacturerCode << imageType << image.fileVersion;
            return image;
        }
    }
    return FirmwareIndexEntry();
}

void ZigbeeIntegrationPlugin::enableFirmwareUpdate(Thing *thing)
{
    m_enabledFirmwareUpdates.append(thing);
    thing->setStateValue("updateStatus", "updating");
}

void ZigbeeIntegrationPlugin::updateFirmwareIndex()
{
    if (m_lastFirmwareIndexUpdate.isNull()) {
        QFileInfo cacheFileInfo(QStandardPaths::writableLocation(QStandardPaths::CacheLocation) + "/zigbee-firmwares/" + m_firmwareIndexUrl.path());
        if (cacheFileInfo.exists()) {
            QFile cache(cacheFileInfo.absoluteFilePath());
            if (cache.open(QFile::ReadOnly)) {
                m_firmwareIndex = firmwareIndexFromJson(cache.readAll());
                m_lastFirmwareIndexUpdate = cacheFileInfo.lastModified();
            }
        }
    }

    if (m_lastFirmwareIndexUpdate.addDays(1) > QDateTime::currentDateTime()) {
        return;
    }
    QNetworkRequest request(m_firmwareIndexUrl);
    QNetworkReply *reply = hardwareManager()->networkManager()->get(request);
    qCDebug(m_dc) << "Fetching firmware index...";
    connect(reply, &QNetworkReply::finished, reply, &QNetworkReply::deleteLater);
    connect(reply, &QNetworkReply::finished, this, [=](){
        if (reply->error() != QNetworkReply::NoError) {
            qCWarning(m_dc) << "Unable to fetch firmware update index file. Zigbee device firmware updates won't work.";
            return;
        }
        QByteArray data = reply->readAll();
        m_firmwareIndex = firmwareIndexFromJson(data);
        m_lastFirmwareIndexUpdate = QDateTime::currentDateTime();
        QFileInfo cacheFileInfo(QStandardPaths::writableLocation(QStandardPaths::CacheLocation) + "/zigbee-firmwares/" + m_firmwareIndexUrl.path());
        QDir cacheDir(cacheFileInfo.absolutePath());
        if (!cacheDir.exists() && !cacheDir.mkpath(cacheFileInfo.absolutePath())) {
            qCWarning(m_dc) << "Unable to create cache file path" << cacheFileInfo.absolutePath();
            return;
        }
        QFile cache(cacheFileInfo.absoluteFilePath());
        if (!cache.open(QFile::WriteOnly | QFile::Truncate)) {
            qCWarning(m_dc) << "Unable to open cache file for writing" << cacheFileInfo.absoluteFilePath();
            return;
        }
        cache.write(data);
        cache.close();
    });
}

ZigbeeIntegrationPlugin::FirmwareIndexEntry ZigbeeIntegrationPlugin::firmwareInfo(quint16 manufacturerId, quint16 imageType, quint32 fileVersion) const
{
    foreach (const FirmwareIndexEntry &entry, m_firmwareIndex) {
        if (entry.manufacturerCode == manufacturerId
                && entry.imageType == imageType
                && entry.fileVersion == fileVersion) {
            return entry;
        }
    }
    return FirmwareIndexEntry();
}

QString ZigbeeIntegrationPlugin::firmwareFileName(const ZigbeeIntegrationPlugin::FirmwareIndexEntry &info) const
{
    return QString("%1/zigbee-firmwares/%2/%3/%4")
            .arg(QStandardPaths::writableLocation(QStandardPaths::CacheLocation))
            .arg(info.manufacturerCode)
            .arg(info.imageType)
            .arg(info.url.fileName());
}

FetchFirmwareReply *ZigbeeIntegrationPlugin::fetchFirmware(const ZigbeeIntegrationPlugin::FirmwareIndexEntry &info)
{
    FetchFirmwareReply *reply = new FetchFirmwareReply(this);
    QFuture<bool> future;
    qCDebug(m_dc) << "Downloading firmware from" << info.url.toString();
    QNetworkRequest request(info.url);
    QNetworkReply *networkReply = hardwareManager()->networkManager()->get(request);

    connect(networkReply, &QNetworkReply::finished, networkReply, &QNetworkReply::deleteLater);
    connect(networkReply, &QNetworkReply::finished, this, [=](){
        if (networkReply->error() != QNetworkReply::NoError) {
            qCWarning(m_dc) << "Error downloading firmware" << info.url.toString();
            emit reply->finished();
            return;
        }
        if (networkReply->attribute(QNetworkRequest::RedirectionTargetAttribute).isValid()) {
            QUrl newUrl = networkReply->attribute(QNetworkRequest::RedirectionTargetAttribute).toUrl();
            qCDebug(m_dc) << "Firmware download redirected to" << newUrl;
            FirmwareIndexEntry newInfo = info;
            newInfo.url = newUrl;
            FetchFirmwareReply *newReply = fetchFirmware(newInfo);
            connect(newReply, &FetchFirmwareReply::finished, reply, &FetchFirmwareReply::finished);
            return;
        }
        QFileInfo fileInfo(firmwareFileName(info));
        QDir path(fileInfo.absolutePath());
        if (!path.exists() && !path.mkpath(fileInfo.absolutePath())) {
            qCWarning(m_dc) << "Error creating cache path for firmware" << fileInfo.absolutePath();
            emit reply->finished();
            return;
        }
        QByteArray data = extractImage(info, networkReply->readAll());
        if (data.isEmpty()) {
            qCWarning(m_dc) << "Unable to extract image";
            emit reply->finished();
            return;
        }

        QFile file(fileInfo.absoluteFilePath());
        if (!file.open(QFile::WriteOnly | QFile::Truncate)) {
            qCWarning(m_dc) << "Error opening firmware cache file for writing" << fileInfo.absoluteFilePath();
            emit reply->finished();
            return;
        }
        file.write(data);
        file.close();
        emit reply->finished();
    });
    return reply;
}

bool ZigbeeIntegrationPlugin::firmwareFileExists(const ZigbeeIntegrationPlugin::FirmwareIndexEntry &info) const
{
    QFile file(firmwareFileName(info));
    if (!file.exists()) {
        qCDebug(m_dc) << "File does not exist";
        return false;
    }
    if (file.size() != info.fileSize) {
        qCDebug(m_dc) << "File size not matching:" << file.size() << "!=" << info.fileSize;
        return false;
    }
    if (!file.open(QFile::ReadOnly)) {
        return false;
    }
    if (!info.sha512.isEmpty()) {
        QByteArray hash = QCryptographicHash::hash(file.readAll(), QCryptographicHash::Sha512);
        if (info.sha512 != hash.toHex()) {
            qCDebug(m_dc) << "SHA512 verification failed";
            return false;
        }
        qCDebug(m_dc) << "SHA512 verified successfully";
    }
    file.close();

    return true;
}

QByteArray ZigbeeIntegrationPlugin::extractImage(const FirmwareIndexEntry &info, const QByteArray &data) const
{
    QDataStream stream(data);
    stream.setByteOrder(QDataStream::LittleEndian);

    // Trying to find the OTA upgrade file identifier 0x0BEEF11E
    bool found = false;
    quint32 otaFileIdentifier = 0x0BEEF11E;
    quint8 searchPos = 0;
    quint64 startPos = 0;
    while (!stream.atEnd()) {
        quint8 tmp;
        stream >> tmp;
        if (tmp == (otaFileIdentifier & (0xff << (searchPos * 8))) >> (searchPos * 8)) {
            searchPos++;
        } else {
            searchPos = 0;
        }
        if (searchPos == 4) {
            found = true;
            startPos -= 3;
            break;
        }
        startPos++;
    }

    if (!found) {
        qCDebug(m_dc) << "Image identifier not found in download.";
        return QByteArray();
    }

    quint16 headerVersion, headerLength, fieldControl, manufacturerCode, imageType, zigbeeStackVersion;
    quint32 fileVersion, totalImageSize;
    stream >> headerVersion >> headerLength >> fieldControl >> manufacturerCode >> imageType >> fileVersion >> zigbeeStackVersion;
    char headerStr[32];
    stream.readRawData(headerStr, 32);
    QByteArray headerString(headerStr, 32);
    stream >> totalImageSize;

    quint8 securityCredentialVersion = 0;
    if (fieldControl & 0x01) {
        stream >> securityCredentialVersion;
    }

    ZigbeeAddress upgradeFileDestination;
    if (fieldControl & 0x02) {
        quint64 destinationAddr;
        stream >> destinationAddr;
        upgradeFileDestination = ZigbeeAddress(destinationAddr);
    }

    quint16 minimumHardwareVersion = 0;
    quint16 maximumHardwareVersion = 0;
    if (fieldControl & 0x04) {
        stream >> minimumHardwareVersion >> maximumHardwareVersion;
    }

    qCDebug(m_dc) << "Header version:" << headerVersion;
    qCDebug(m_dc) << "Header length:" << headerLength;
    qCDebug(m_dc) << "Field control:" << fieldControl;
    qCDebug(m_dc) << "Manufacturer code:" << manufacturerCode;
    qCDebug(m_dc) << "Image type:" << imageType;
    ZigbeeClusterOta::FileVersion parsedVersion = ZigbeeClusterOta::parseFileVersion(fileVersion);
    qCDebug(m_dc) << "File version:" << fileVersion << QString("%0.%1.%2.%3").arg(parsedVersion.applicationRelease).arg(parsedVersion.applicationBuild).arg(parsedVersion.stackRelease).arg(parsedVersion.stackBuild);
    qCDebug(m_dc) << "Zigbee Stack version:" << zigbeeStackVersion;
    qCDebug(m_dc) << "Header string:" << headerString;
    qCDebug(m_dc) << "Image size:" << totalImageSize;
    qCDebug(m_dc) << "Security credentials version:" << securityCredentialVersion;
    qCDebug(m_dc) << "Min HW version:" << minimumHardwareVersion << "Max HW version:" << maximumHardwareVersion;

    qCDebug(m_dc) << "Download file size:" << data.size() << "Image start position:" << startPos;

    if (totalImageSize != info.fileSize || data.size() - startPos < totalImageSize) {
        qCWarning(m_dc) << "Image file size not matching";
        return QByteArray();
    }
    if (manufacturerCode != info.manufacturerCode) {
        qCWarning(m_dc) << "Manufacturer code not matching in downloaded image" << manufacturerCode << "!=" << info.manufacturerCode;
        return QByteArray();
    }
    if (imageType != info.imageType) {
        qCWarning(m_dc) << "Image type not matching in downloaded image" << imageType << "!=" << info.imageType;
        return QByteArray();
    }
    qCDebug(m_dc) << "Image data:" << data.mid(startPos, 16).toHex();
    return data.mid(startPos, totalImageSize);
}

QList<ZigbeeIntegrationPlugin::FirmwareIndexEntry> ZigbeeIntegrationPlugin::firmwareIndexFromJson(const QByteArray &data) const
{
    QList<ZigbeeIntegrationPlugin::FirmwareIndexEntry> ret;

    QJsonParseError error;
    QJsonDocument jsonDoc = QJsonDocument::fromJson(data, &error);
    if (error.error != QJsonParseError::NoError) {
        qCWarning(m_dc) << "Unable to parse firmware update index.";
        return ret;
    }

    foreach (const QVariant &entryVariant, jsonDoc.toVariant().toList()) {
        QVariantMap map = entryVariant.toMap();
        FirmwareIndexEntry entry;
        entry.manufacturerCode = map.value("manufacturerCode").toUInt();
        entry.imageType = map.value("imageType").toUInt();
        entry.fileVersion = map.value("fileVersion").toUInt();
        entry.minFileVersion = map.value("minFileVersion").toUInt();
        entry.maxFileVersion = map.value("maxFileVersion").toUInt();
        entry.fileSize = map.value("fileSize").toUInt();
        entry.url = map.value("url").toUrl();
        entry.modelId = map.value("modelId").toString();
        entry.sha512 = map.value("sha512").toByteArray();
        ret.append(entry);
    }
    return ret;

}
