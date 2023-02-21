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

#include "integrationpluginzigbeetuya.h"
#include "plugininfo.h"
#include "dpvalue.h"

#include <hardware/zigbee/zigbeehardwareresource.h>

#include <zcl/general/zigbeeclusteronoff.h>
#include <zcl/hvac/zigbeeclusterthermostat.h>
#include <zcl/smartenergy/zigbeeclustermetering.h>
#include <zcl/measurement/zigbeeclusterelectricalmeasurement.h>
#include <zcl/security/zigbeeclusteriaszone.h>

#include <QDebug>
#include <QDataStream>

#define ATTRIBUTE_ID_SOCKET_POWER_ON_DEFAULT_MODE 0x8002

#define COMMAND_ID_DATA_REQUEST 0
#define COMMAND_ID_DATA_RESPONSE 1
#define COMMAND_ID_DATA_REPORT 2
#define COMMAND_ID_DATA_QUERY 4

#define CLUSTER_ID_MANUFACTURER_SPECIFIC_TUYA 0xef00

#define PRESENCE_SENSOR_DP_PRESENCE 1
#define PRESENCE_SENSOR_DP_SENSITIVITY 2
#define PRESENCE_SENSOR_DP_MINIMUM_RANGE 3
#define PRESENCE_SENSOR_DP_MAXIMUM_RANGE 4
#define PRESENCE_SENSOR_DP_SELF_TEST 6
#define PRESENCE_SENSOR_DP_TARGET_DISTANCE 9
#define PRESENCE_SENSOR_DP_DETECTION_DELAY 101
#define PRESENCE_SENSOR_DP_FADING_TIME 102
#define PRESENCE_SENSOR_DP_CLI 103
#define PRESENCE_SENSOR_DP_LUX 104

#define LUMINANCE_MOTION_SENSOR_DP_PRESENCE 1
#define LUMINANCE_MOTION_SENSOR_DP_BATTERY 4
#define LUMINANCE_MOTION_SENSOR_DP_SENSITIVITY 9
#define LUMINANCE_MOTION_SENSOR_DP_KEEP_TIME 10
#define LUMINANCE_MOTION_SENSOR_DP_ILLUMINANCE 12

#define HT_SENSOR_DP_TEMPERATURE 1
#define HT_SENSOR_DP_HUMIDITY 2
#define HT_SENSOR_DP_BATTERY 4
#define HT_SENSOR_DP_TEMPERATURE_UNIT 9
#define HT_SENSOR_DP_TEMPERATURE_CALIBRATION 23
#define HT_SENSOR_DP_HUMIDITY_CALIBRATION 24

#define SAH_DP_PM25 2
#define SAH_DP_TEMPERATURE 18
#define SAH_DP_HUMIDITY 19
#define SAH_DP_FORMALDEHYD 20
#define SAH_DP_VOC 21
#define SAH_DP_CO2 22

IntegrationPluginZigbeeTuya::IntegrationPluginZigbeeTuya(): ZigbeeIntegrationPlugin(ZigbeeHardwareResource::HandlerTypeVendor, dcZigbeeTuya())
{
}

QString IntegrationPluginZigbeeTuya::name() const
{
    return "Tuya";
}

bool IntegrationPluginZigbeeTuya::handleNode(ZigbeeNode *node, const QUuid &/*networkUuid*/)
{
    if (node->nodeDescriptor().manufacturerCode == 0x1141 && node->modelName() == "TS011F") {
        qCDebug(dcZigbeeTuya()) << "Tuya smart plug";

        ZigbeeNodeEndpoint *endpoint = node->getEndpoint(0x01);
        if (!endpoint) {
            qCWarning(dcZigbeeTuya()) << "Endpoint 1 not found on device....";
            return false;
        }

        bindOnOffCluster(endpoint);
        configureOnOffInputClusterAttributeReporting(endpoint);

        bindElectricalMeasurementCluster(endpoint);
        configureElectricalMeasurementInputClusterAttributeReporting(endpoint);

        bindMeteringCluster(endpoint);
        configureMeteringInputClusterAttributeReporting(endpoint);

        createThing(powerSocketThingClassId, node);

        return true;
    }

    if (node->nodeDescriptor().manufacturerCode == 0x1002 && node->modelName() == "TS0601") {
        createThing(presenceSensorThingClassId, node);
        return true;
    }

    if (match(node, "TS0210", {
              "_TYZB01_3zv6oleo",
              "_TYZB01_j9xxahcl",
              "_TYZB01_kulduhbj",
              "_TZ3000_bmfw9ykl",
              "_TZ3000_fkxmyics"})) {
        ZigbeeNodeEndpoint *endpoint = node->getEndpoint(0x01);
        if (!endpoint) {
            qCWarning(dcZigbeeTuya()) << "Endpoint 1 not found on device....";
            return false;
        }
        bindPowerConfigurationCluster(endpoint);
        bindIasZoneCluster(endpoint);
        createThing(vibrationSensorThingClassId, node);
        return true;
    }

    if (match(node, "TS0601", {"_TZE200_3towulqd", "_TZE200_1ibpyhdc"})) {
        createThing(motionSensorThingClassId, node);
        return true;
    }

    if (match(node, "TS0601", {"_TZE200_nnrfa68v", "_TZE200_qoy0ekbd", "_TZE200_znbl8dj5", "_TZE200_a8sdabtg"})) {
        createThing(htlcdSensorThingClassId, node);
        return true;
    }

    if (match(node, "TS0601", {"_TZE200_dwcarsat"})) {
        createThing(airHousekeeperThingClassId, node);
        return true;
    }

    return false;
}

void IntegrationPluginZigbeeTuya::setupThing(ThingSetupInfo *info)
{
    Thing *thing = info->thing();

    if (!manageNode(thing)) {
        qCWarning(dcZigbeeTuya()) << "Failed to claim node during setup.";
        info->finish(Thing::ThingErrorHardwareNotAvailable);
        return;
    }

    ZigbeeNode *node = nodeForThing(thing);
    ZigbeeNodeEndpoint *endpoint = node->getEndpoint(0x01);

    if (thing->thingClassId() == powerSocketThingClassId) {
        connectToOnOffInputCluster(thing, endpoint);
        connectToElectricalMeasurementCluster(thing, endpoint);

        // Device doesn't use standard divisor and the formatting attribute is not supported.
        // Can't use common connection, manually connecting and hardcoding divisor of 100
        // connectToMeteringCluster(thing, endpoint);
        ZigbeeClusterMetering *meteringCluster = endpoint->inputCluster<ZigbeeClusterMetering>(ZigbeeClusterLibrary::ClusterIdMetering);
        if (meteringCluster) {
            connect(meteringCluster, &ZigbeeClusterMetering::currentSummationDeliveredChanged, thing, [=](quint64 currentSummationDelivered){
                thing->setStateValue("totalEnergyConsumed", 1.0 * currentSummationDelivered / 100);
            });
        }


        // Attribute reporting seems not to be working for some of the models, we'll need to poll
        if (!m_energyPollTimer) {
            m_energyPollTimer = hardwareManager()->pluginTimerManager()->registerTimer(5);
            connect(m_energyPollTimer, &PluginTimer::timeout, this, &IntegrationPluginZigbeeTuya::pollEnergyMeters);
        }

        // proprietary attribute for configuring power on default mode
        ZigbeeClusterOnOff *onOffCluster = endpoint->inputCluster<ZigbeeClusterOnOff>(ZigbeeClusterLibrary::ClusterIdOnOff);
        if (onOffCluster) {
            QHash<uint, QString> map = {{0, "Off"}, {1, "On"}, {2, "Restore"}};
            if (onOffCluster->hasAttribute(ATTRIBUTE_ID_SOCKET_POWER_ON_DEFAULT_MODE)) {
                thing->setSettingValue(powerSocketSettingsDefaultPowerStateParamTypeId, map.value(onOffCluster->attribute(ATTRIBUTE_ID_SOCKET_POWER_ON_DEFAULT_MODE).dataType().toUInt8()));
            }
            connect(onOffCluster, &ZigbeeClusterOnOff::attributeChanged, thing, [thing, map](const ZigbeeClusterAttribute &attribute){
                if (attribute.id() == ATTRIBUTE_ID_SOCKET_POWER_ON_DEFAULT_MODE) {
                    thing->setSettingValue(powerSocketSettingsDefaultPowerStateParamTypeId, map.value(attribute.dataType().toUInt8()));
                }
            });
            onOffCluster->readAttributes({ATTRIBUTE_ID_SOCKET_POWER_ON_DEFAULT_MODE});
            connect(thing, &Thing::settingChanged, onOffCluster, [onOffCluster, map](const ParamTypeId &paramTypeId, const QVariant &value){
                if (paramTypeId == powerSocketSettingsDefaultPowerStateParamTypeId) {
                    ZigbeeDataType dataType(static_cast<quint8>(map.key(value.toString())));
                    ZigbeeClusterLibrary::WriteAttributeRecord record;
                    record.attributeId = ATTRIBUTE_ID_SOCKET_POWER_ON_DEFAULT_MODE;
                    record.dataType = Zigbee::Enum8;
                    record.data = dataType.data();
                    onOffCluster->writeAttributes({record});
                }
            });
        }
    }

    if (thing->thingClassId() == presenceSensorThingClassId) {
        ZigbeeCluster *cluster = node->getEndpoint(0x01)->getInputCluster(static_cast<ZigbeeClusterLibrary::ClusterId>(CLUSTER_ID_MANUFACTURER_SPECIFIC_TUYA));
        cluster->executeClusterCommand(COMMAND_ID_DATA_QUERY, QByteArray(), ZigbeeClusterLibrary::DirectionClientToServer, true);

        connect(cluster, &ZigbeeCluster::dataIndication, thing, [thing](const ZigbeeClusterLibrary::Frame &frame){

            if (frame.header.command == COMMAND_ID_DATA_REPORT) {
                DpValue dpValue = DpValue::fromData(frame.payload);

                switch (dpValue.dp()) {
                case PRESENCE_SENSOR_DP_PRESENCE:
                    qCDebug(dcZigbeeTuya()) << "presence changed:" << dpValue;
                    thing->setStateValue(presenceSensorIsPresentStateTypeId, dpValue.value().toBool());
                    break;
                case PRESENCE_SENSOR_DP_SENSITIVITY:
                    qCDebug(dcZigbeeTuya()) << "Sensitivity changed:" << dpValue << thing->setting(presenceSensorSettingsSensitivityParamTypeId);
                    thing->setSettingValue(presenceSensorSettingsSensitivityParamTypeId, dpValue.value().toUInt());
                    break;
                case PRESENCE_SENSOR_DP_MINIMUM_RANGE:
                    qCDebug(dcZigbeeTuya()) << "min range changed:" << dpValue;
                    thing->setSettingValue(presenceSensorSettingsMinimumRangeParamTypeId, dpValue.value().toDouble() / 100.0);
                    break;
                case PRESENCE_SENSOR_DP_MAXIMUM_RANGE:
                    qCDebug(dcZigbeeTuya()) << "max range changed:" << dpValue << thing->setting(presenceSensorSettingsMaximumRangeParamTypeId);
                    thing->setSettingValue(presenceSensorSettingsMaximumRangeParamTypeId, dpValue.value().toDouble() / 100.0);
                    break;
                case PRESENCE_SENSOR_DP_SELF_TEST: {
                    QHash<int, QString> map = {
                        {0, "checking"},
                        {1, "success"},
                        {2, "failure"},
                        {3, "other"},
                        {4, "communication_error"},
                        {5, "radar_error"}
                    };
                    thing->setStateValue(presenceSensorSelfTestStateTypeId, map.value(dpValue.value().toUInt()));
                    break;
                }
                case PRESENCE_SENSOR_DP_TARGET_DISTANCE:
//                    qCDebug(dcZigbeeTuya()) << "Target distance:" << data.toUInt() / 100.0;
                    thing->setStateValue(presenceSensorTargetDistanceStateTypeId, dpValue.value().toUInt() / 100.0);
                    break;
                case PRESENCE_SENSOR_DP_DETECTION_DELAY:
                    qCDebug(dcZigbeeTuya()) << "Detection delay:" << dpValue;
                    thing->setSettingValue(presenceSensorSettingsDetectionDelayParamTypeId, dpValue.value().toUInt() / 10.0);
                    break;
                case PRESENCE_SENSOR_DP_FADING_TIME:
                    qCDebug(dcZigbeeTuya()) << "Fading time:" << dpValue;
                    thing->setSettingValue(presenceSensorSettingsFadingTimeParamTypeId, dpValue.value().toUInt() / 10.0);
                    break;
                case PRESENCE_SENSOR_DP_CLI:
                    qCDebug(dcZigbeeTuya()) << "CLI:" << dpValue;
                    break;
                case PRESENCE_SENSOR_DP_LUX:
                    qCDebug(dcZigbeeTuya()) << "LUX changed:" << dpValue;
                    thing->setStateValue(presenceSensorLightIntensityStateTypeId, dpValue.value().toDouble());
                    break;
                default:
                    qCWarning(dcZigbeeTuya()) << "Unhandled data point" << dpValue;
                }

            } else {
                qCWarning(dcZigbeeTuya()) << "Unhandled presence sensor cluster command:" << frame.header.command;
            }
        });

        connect(thing, &Thing::settingChanged, cluster, [cluster, thing, this](const ParamTypeId &settingTypeId, const QVariant &value) {
            DpValue dp;

            if (settingTypeId == presenceSensorSettingsDetectionDelayParamTypeId) {
                dp = DpValue(PRESENCE_SENSOR_DP_DETECTION_DELAY, DpValue::TypeUInt32, value.toUInt() * 10, 4, m_seq++);
            }
            if (settingTypeId == presenceSensorSettingsMinimumRangeParamTypeId) {
                dp = DpValue(PRESENCE_SENSOR_DP_MINIMUM_RANGE, DpValue::TypeUInt32, value.toUInt() * 100, 4, m_seq++);
            }
            if (settingTypeId == presenceSensorSettingsMaximumRangeParamTypeId) {
                dp = DpValue(PRESENCE_SENSOR_DP_MAXIMUM_RANGE, DpValue::TypeUInt32, value.toUInt() * 100, 4, m_seq++);
            }
            if (settingTypeId == presenceSensorSettingsSensitivityParamTypeId) {
                dp = DpValue(PRESENCE_SENSOR_DP_SENSITIVITY, DpValue::TypeUInt32, value.toUInt(), 4, m_seq++);
            }
            if (settingTypeId == presenceSensorSettingsFadingTimeParamTypeId) {
                dp = DpValue(PRESENCE_SENSOR_DP_FADING_TIME, DpValue::TypeUInt32, value.toUInt() * 10, 4, m_seq++);
            }
            qCDebug(dcZigbeeTuya()) << "setting" << thing->thingClass().settingsTypes().findById(settingTypeId).name() << dp << dp.toData().toHex();
            ZigbeeClusterReply *reply = cluster->executeClusterCommand(COMMAND_ID_DATA_REQUEST, dp.toData(), ZigbeeClusterLibrary::DirectionClientToServer, true);
            connect(reply, &ZigbeeClusterReply::finished, reply, [=](){
                qCDebug(dcZigbeeTuya()) << "setting set with status" << reply->error();
            });
        });
    }

    if (thing->thingClassId() == vibrationSensorThingClassId) {
        ZigbeeNodeEndpoint *endpoint = node->getEndpoint(1);
        if (!endpoint) {
            qCWarning(dcZigbeeTuya()) << "Endpoint 1 not found on" << node;
            info->finish(Thing::ThingErrorHardwareNotAvailable);
            return;
        }
        ZigbeeClusterIasZone *iasZoneCluster = endpoint->inputCluster<ZigbeeClusterIasZone>(ZigbeeClusterLibrary::ClusterIdIasZone);
        if (!iasZoneCluster) {
            qCWarning(dcZigbeeTuya()) << "Could not find IAS zone cluster on" << thing << endpoint;
            return;
        }

        if (iasZoneCluster->hasAttribute(ZigbeeClusterIasZone::AttributeCurrentZoneSensitivityLevel)) {
            thing->setSettingValue(vibrationSensorSettingsSensitivityParamTypeId, iasZoneCluster->attribute(ZigbeeClusterIasZone::AttributeCurrentZoneSensitivityLevel).dataType().toUInt8());
        }
        iasZoneCluster->readAttributes({ZigbeeClusterIasZone::AttributeCurrentZoneSensitivityLevel});
        connect(iasZoneCluster, &ZigbeeClusterIasZone::attributeChanged, thing, [thing](const ZigbeeClusterAttribute &attribute){
            thing->setSettingValue(vibrationSensorSettingsSensitivityParamTypeId, attribute.dataType().toUInt8());
        });
        connect(thing, &Thing::settingChanged, iasZoneCluster, [iasZoneCluster](const ParamTypeId &settingId, const QVariant &value){
            Q_UNUSED(settingId)
            ZigbeeDataType dataType(static_cast<quint8>(value.toUInt()));
            ZigbeeClusterLibrary::WriteAttributeRecord sensitivityAttribute;
            sensitivityAttribute.attributeId = ZigbeeClusterIasZone::AttributeCurrentZoneSensitivityLevel;
            sensitivityAttribute.dataType = dataType.dataType();
            sensitivityAttribute.data = dataType.data();
            iasZoneCluster->writeAttributes({sensitivityAttribute});
        });

        connect(iasZoneCluster, &ZigbeeClusterIasZone::zoneStatusChanged, thing, [=](ZigbeeClusterIasZone::ZoneStatusFlags zoneStatus, quint8 extendedStatus, quint8 zoneId, quint16 delays) {
            qCDebug(dcZigbeeTuya()) << "Zone status changed to:" << zoneStatus << extendedStatus << zoneId << delays;
            bool zoneAlarm = zoneStatus.testFlag(ZigbeeClusterIasZone::ZoneStatusAlarm1) || zoneStatus.testFlag(ZigbeeClusterIasZone::ZoneStatusAlarm2);
            if (zoneAlarm) {
                thing->emitEvent(vibrationSensorVibrationDetectedEventTypeId);
            }
        });
    }

    if (thing->thingClassId() == motionSensorThingClassId) {
        ZigbeeNodeEndpoint *endpoint = node->getEndpoint(1);
        if (!endpoint) {
            qCWarning(dcZigbeeTuya()) << "Unable to find endpoint 1 on node" << node;
            info->finish(Thing::ThingErrorHardwareNotAvailable, QT_TR_NOOP("Unable to find endpoint 1 on Zigbee node."));
            return;
        }
        ZigbeeCluster *cluster = endpoint->getInputCluster(static_cast<ZigbeeClusterLibrary::ClusterId>(CLUSTER_ID_MANUFACTURER_SPECIFIC_TUYA));
        if (!cluster) {
            qCWarning(dcZigbeeTuya()) << "Unable to find Tuya manufacturer specific cliuster on endpoint 1 on node" << node;
            info->finish(Thing::ThingErrorHardwareNotAvailable, QT_TR_NOOP("Unable to find Tuya cluster on Zigbee node."));
            return;
        }

        cluster->executeClusterCommand(COMMAND_ID_DATA_QUERY, QByteArray(), ZigbeeClusterLibrary::DirectionClientToServer, true);

        connect(cluster, &ZigbeeCluster::dataIndication, thing, [thing](const ZigbeeClusterLibrary::Frame &frame){

            if (frame.header.command == COMMAND_ID_DATA_REPORT) {
                DpValue dpValue = DpValue::fromData(frame.payload);

                switch (dpValue.dp()) {
                case LUMINANCE_MOTION_SENSOR_DP_PRESENCE:
                    qCDebug(dcZigbeeTuya()) << "presence changed:" << dpValue;
                    thing->setStateValue(motionSensorIsPresentStateTypeId, dpValue.value().toInt() == 0);
                    break;
                case LUMINANCE_MOTION_SENSOR_DP_BATTERY:
                    qCDebug(dcZigbeeTuya()) << "Battery changed:" << dpValue;
                    thing->setStateValue(motionSensorBatteryLevelStateTypeId, dpValue.value().toInt());
                    thing->setStateValue(motionSensorBatteryCriticalStateTypeId, dpValue.value().toInt() < 5);
                    break;
                case LUMINANCE_MOTION_SENSOR_DP_SENSITIVITY:
                    qCDebug(dcZigbeeTuya()) << "Sensitivity:" << dpValue << thing->setting(motionSensorSettingsSensitivityParamTypeId);
                    thing->setSettingValue(motionSensorSettingsSensitivityParamTypeId, dpValue.value().toInt() + 1);
                    break;
                case LUMINANCE_MOTION_SENSOR_DP_KEEP_TIME: {
                    qCDebug(dcZigbeeTuya()) << "timeout changed:" << dpValue << thing->setting(motionSensorSettingsTimeoutParamTypeId);
                    QHash<int, int> map = {{0, 10}, {1, 30}, {2, 60}, {3, 120}};
                    thing->setSettingValue(motionSensorSettingsTimeoutParamTypeId, map.value(dpValue.value().toInt()));
                    break;
                }
                case LUMINANCE_MOTION_SENSOR_DP_ILLUMINANCE:
                    qCDebug(dcZigbeeTuya()) << "illuminance changed:" << dpValue;
                    thing->setStateValue(motionSensorLightIntensityStateTypeId, dpValue.value().toDouble());
                    break;

                default:
                    qCWarning(dcZigbeeTuya()) << "Unhandled data point" << dpValue;
                }

            } else {
                qCWarning(dcZigbeeTuya()) << "Unhandled presence sensor cluster command:" << frame.header.command;
            }
        });

        connect(thing, &Thing::settingChanged, cluster, [cluster, thing, this](const ParamTypeId &settingTypeId, const QVariant &value) {
            DpValue dp;

            if (settingTypeId == motionSensorSettingsTimeoutParamTypeId) {
                QHash<int, int> map = {{0, 10}, {1, 30}, {2, 60}, {3, 120}};
                dp = DpValue(LUMINANCE_MOTION_SENSOR_DP_KEEP_TIME, DpValue::TypeEnum, map.key(value.toUInt()), 4, m_seq++);
            }
            if (settingTypeId == motionSensorSettingsSensitivityParamTypeId) {
                dp = DpValue(LUMINANCE_MOTION_SENSOR_DP_SENSITIVITY, DpValue::TypeEnum, value.toUInt(), 4, m_seq++);
            }
            qCDebug(dcZigbeeTuya()) << "setting" << thing->thingClass().settingsTypes().findById(settingTypeId).name() << dp << dp.toData().toHex();
            ZigbeeClusterReply *reply = cluster->executeClusterCommand(COMMAND_ID_DATA_REQUEST, dp.toData(), ZigbeeClusterLibrary::DirectionClientToServer, true);
            connect(reply, &ZigbeeClusterReply::finished, reply, [=](){
                qCDebug(dcZigbeeTuya()) << "setting set with status" << reply->error();
            });
        });

    }

    if (info->thing()->thingClassId() == htlcdSensorThingClassId) {
        ZigbeeNodeEndpoint *endpoint = node->getEndpoint(1);
        if (!endpoint) {
            qCWarning(dcZigbeeTuya()) << "Unable to find endpoint 1 on node" << node;
            info->finish(Thing::ThingErrorHardwareNotAvailable, QT_TR_NOOP("Unable to find endpoint 1 on Zigbee node."));
            return;
        }
        ZigbeeCluster *cluster = endpoint->getInputCluster(static_cast<ZigbeeClusterLibrary::ClusterId>(CLUSTER_ID_MANUFACTURER_SPECIFIC_TUYA));
        if (!cluster) {
            qCWarning(dcZigbeeTuya()) << "Unable to find Tuya manufacturer specific cliuster on endpoint 1 on node" << node;
            info->finish(Thing::ThingErrorHardwareNotAvailable, QT_TR_NOOP("Unable to find Tuya cluster on Zigbee node."));
            return;
        }

        if (node->reachable()) {
            cluster->executeClusterCommand(COMMAND_ID_DATA_QUERY, QByteArray(), ZigbeeClusterLibrary::DirectionClientToServer, true);
        }
        connect(node, &ZigbeeNode::reachableChanged, thing, [=](bool reachable){
            if (reachable) {
                cluster->executeClusterCommand(COMMAND_ID_DATA_QUERY, QByteArray(), ZigbeeClusterLibrary::DirectionClientToServer, true);
            }
        });

        connect(cluster, &ZigbeeCluster::dataIndication, thing, [thing](const ZigbeeClusterLibrary::Frame &frame){

            if (frame.header.command == COMMAND_ID_DATA_REPORT || frame.header.command == COMMAND_ID_DATA_RESPONSE) {
                DpValue dpValue = DpValue::fromData(frame.payload);

                switch (dpValue.dp()) {
                case HT_SENSOR_DP_TEMPERATURE:
                    qCDebug(dcZigbeeTuya()) << "temperature changed:" << dpValue;
                    thing->setStateValue(htlcdSensorTemperatureStateTypeId, dpValue.value().toInt() / 10.0);
                    break;
                case HT_SENSOR_DP_HUMIDITY:
                    qCDebug(dcZigbeeTuya()) << "Humidity changed:" << dpValue;
                    thing->setStateValue(htlcdSensorHumidityStateTypeId, dpValue.value().toInt());
                    break;
                case HT_SENSOR_DP_BATTERY:
                    qCDebug(dcZigbeeTuya()) << "Battery changed:" << dpValue;
                    thing->setStateValue(htlcdSensorBatteryLevelStateTypeId, dpValue.value().toInt());
                    thing->setStateValue(htlcdSensorBatteryCriticalStateTypeId, dpValue.value().toInt() < 5);
                    break;
                case HT_SENSOR_DP_TEMPERATURE_UNIT: {
                    qCDebug(dcZigbeeTuya()) << "Temp unit:" << dpValue;
                    QHash<int, QString> map = {{0, "C"}, {1, "F"}};
                    thing->setSettingValue(htlcdSensorSettingsUnitDisplayParamTypeId, map.value(dpValue.value().toInt()));
                    break;
                }
                case HT_SENSOR_DP_TEMPERATURE_CALIBRATION:
                    qCDebug(dcZigbeeTuya()) << "temp calib changed:" << dpValue;
                    thing->setSettingValue(htlcdSensorSettingsTemperatureCalibrationParamTypeId, dpValue.value().toInt() / 10.0);
                    break;
                case HT_SENSOR_DP_HUMIDITY_CALIBRATION: {
                    qCDebug(dcZigbeeTuya()) << "hum calib changed:" << dpValue;
                    thing->setSettingValue(htlcdSensorSettingsHumidityCalibrationParamTypeId, dpValue.value().toInt());
                    break;
                }
                default:
                    qCWarning(dcZigbeeTuya()) << "Unhandled data point" << dpValue;
                }

            } else {
                qCWarning(dcZigbeeTuya()) << "Unhandled HT LCD sensor cluster command:" << frame.header.command;
            }

            if (frame.header.command == COMMAND_ID_DATA_RESPONSE) {
                qCDebug(dcZigbeeTuya()) << "Command response:" << frame.payload.toHex();
            }

        });

        connect(thing, &Thing::settingChanged, cluster, [cluster, thing, this](const ParamTypeId &settingTypeId, const QVariant &value) {
            DpValue dp;

            if (settingTypeId == htlcdSensorSettingsUnitDisplayParamTypeId) {
                QHash<int, QString> map = {{0, "C"}, {1, "F"}};
                dp = DpValue(HT_SENSOR_DP_TEMPERATURE_UNIT, DpValue::TypeEnum, map.key(value.toString()), 1, m_seq++);
            }
            if (settingTypeId == htlcdSensorSettingsTemperatureCalibrationParamTypeId) {
                dp = DpValue(HT_SENSOR_DP_TEMPERATURE_CALIBRATION, DpValue::TypeUInt32, qRound(value.toDouble() * 10), 4, m_seq++);
            }
            if (settingTypeId == htlcdSensorSettingsHumidityCalibrationParamTypeId) {
                dp = DpValue(HT_SENSOR_DP_HUMIDITY_CALIBRATION, DpValue::TypeUInt32, value.toUInt(), 4, m_seq++);
            }
            qCDebug(dcZigbeeTuya()) << "setting" << thing->thingClass().settingsTypes().findById(settingTypeId).name() << dp << dp.toData().toHex();
            cluster->executeClusterCommand(COMMAND_ID_DATA_REQUEST, dp.toData(), ZigbeeClusterLibrary::DirectionClientToServer, true);
        });

    }

    if (info->thing()->thingClassId() == airHousekeeperThingClassId) {
        ZigbeeNodeEndpoint *endpoint = node->getEndpoint(1);
        if (!endpoint) {
            qCWarning(dcZigbeeTuya()) << "Unable to find endpoint 1 on node" << node;
            info->finish(Thing::ThingErrorHardwareNotAvailable, QT_TR_NOOP("Unable to find endpoint 1 on Zigbee node."));
            return;
        }
        ZigbeeCluster *cluster = endpoint->getInputCluster(static_cast<ZigbeeClusterLibrary::ClusterId>(CLUSTER_ID_MANUFACTURER_SPECIFIC_TUYA));
        if (!cluster) {
            qCWarning(dcZigbeeTuya()) << "Unable to find Tuya manufacturer specific cliuster on endpoint 1 on node" << node;
            info->finish(Thing::ThingErrorHardwareNotAvailable, QT_TR_NOOP("Unable to find Tuya cluster on Zigbee node."));
            return;
        }

        if (node->reachable()) {
            cluster->executeClusterCommand(COMMAND_ID_DATA_QUERY, QByteArray(), ZigbeeClusterLibrary::DirectionClientToServer, true);
        }
        connect(node, &ZigbeeNode::reachableChanged, thing, [=](bool reachable){
            if (reachable) {
                cluster->executeClusterCommand(COMMAND_ID_DATA_QUERY, QByteArray(), ZigbeeClusterLibrary::DirectionClientToServer, true);
            }
        });

        connect(cluster, &ZigbeeCluster::dataIndication, thing, [thing](const ZigbeeClusterLibrary::Frame &frame){

            if (frame.header.command == COMMAND_ID_DATA_REPORT || frame.header.command == COMMAND_ID_DATA_RESPONSE) {
                DpValue dpValue = DpValue::fromData(frame.payload);

                switch (dpValue.dp()) {
                case SAH_DP_CO2:
                    qCDebug(dcZigbeeTuya()) << "CO2 changed:" << dpValue;
                    thing->setStateValue(airHousekeeperCo2StateTypeId, dpValue.value().toInt() / 10.0);
                    break;
                case SAH_DP_TEMPERATURE:
                    qCDebug(dcZigbeeTuya()) << "Temperature changed:" << dpValue;
                    thing->setStateValue(airHousekeeperTemperatureStateTypeId, dpValue.value().toInt() / 10.0);
                    break;
                case SAH_DP_HUMIDITY:
                    qCDebug(dcZigbeeTuya()) << "Humidity changed:" << dpValue;
                    thing->setStateValue(airHousekeeperHumidityStateTypeId, dpValue.value().toInt() / 10.0);
                    break;
                case SAH_DP_PM25:
                    qCDebug(dcZigbeeTuya()) << "PM2.5 changed:" << dpValue;
                    thing->setStateValue(airHousekeeperPm25StateTypeId, dpValue.value().toInt());
                    break;
                case SAH_DP_FORMALDEHYD:
                    qCDebug(dcZigbeeTuya()) << "Temperature changed:" << dpValue;
                    thing->setStateValue(airHousekeeperFormaldehydStateTypeId, dpValue.value().toInt());
                    break;
                case SAH_DP_VOC:
                    qCDebug(dcZigbeeTuya()) << "VOC changed:" << dpValue;
                    thing->setStateValue(airHousekeeperVocStateTypeId, dpValue.value().toInt());
                    break;
                default:
                    qCWarning(dcZigbeeTuya()) << "Unhandled data point" << dpValue;
                }

            } else {
                qCWarning(dcZigbeeTuya()) << "Unhandled smart air housekeeper command:" << frame.header.command;
            }

            if (frame.header.command == COMMAND_ID_DATA_RESPONSE) {
                qCDebug(dcZigbeeTuya()) << "Command response:" << frame.payload.toHex();
            }

        });
    }

    info->finish(Thing::ThingErrorNoError);
}

void IntegrationPluginZigbeeTuya::executeAction(ThingActionInfo *info)
{
    if (!hardwareManager()->zigbeeResource()->available()) {
        info->finish(Thing::ThingErrorHardwareNotAvailable);
        return;
    }

    Thing *thing = info->thing();
    ZigbeeNode *node = nodeForThing(info->thing());
    if (!node->reachable()) {
        info->finish(Thing::ThingErrorHardwareNotAvailable);
        return;
    }

    if (thing->thingClassId() == powerSocketThingClassId) {
        ZigbeeNodeEndpoint *endpoint = node->getEndpoint(0x01);
        if (info->action().actionTypeId() == powerSocketPowerActionTypeId) {
            ZigbeeClusterOnOff *onOffCluster = endpoint->inputCluster<ZigbeeClusterOnOff>(ZigbeeClusterLibrary::ClusterIdOnOff);
            if (!onOffCluster) {
                qCWarning(dcZigbeeTuya()) << "Could not find on/off cluster for" << thing << "in" << endpoint;
                info->finish(Thing::ThingErrorHardwareFailure);
                return;
            }

            bool power = info->action().param(powerSocketPowerActionPowerParamTypeId).value().toBool();
            ZigbeeClusterReply *reply = (power ? onOffCluster->commandOn() : onOffCluster->commandOff());
            connect(reply, &ZigbeeClusterReply::finished, info, [=](){
                info->finish(reply->error() == ZigbeeClusterReply::ErrorNoError ? Thing::ThingErrorNoError : Thing::ThingErrorHardwareFailure);
            });
            return;
        }
    }

    info->finish(Thing::ThingErrorUnsupportedFeature);
}

void IntegrationPluginZigbeeTuya::thingRemoved(Thing *thing)
{
    ZigbeeIntegrationPlugin::thingRemoved(thing);
    if (myThings().filterByThingClassId(powerSocketThingClassId).isEmpty()) {
        hardwareManager()->pluginTimerManager()->unregisterTimer(m_energyPollTimer);
        m_energyPollTimer = nullptr;
    }
}

void IntegrationPluginZigbeeTuya::pollEnergyMeters()
{
    foreach (Thing *thing, myThings().filterByThingClassId(powerSocketThingClassId)) {
        ZigbeeNode *node = nodeForThing(thing);
        ZigbeeNodeEndpoint *endpoint = node->getEndpoint(0x01);
        ZigbeeClusterElectricalMeasurement *electricalMeasurementCluster = endpoint->inputCluster<ZigbeeClusterElectricalMeasurement>(ZigbeeClusterLibrary::ClusterIdElectricalMeasurement);
        electricalMeasurementCluster->readAttributes(
                    {
                        ZigbeeClusterElectricalMeasurement::AttributeACPhaseAMeasurementActivePower,
                        ZigbeeClusterElectricalMeasurement::AttributeACPhaseAMeasurementRMSCurrent,
                        ZigbeeClusterElectricalMeasurement::AttributeACPhaseAMeasurementRMSVoltage
                    });
        ZigbeeClusterMetering *meteringCluster = endpoint->inputCluster<ZigbeeClusterMetering>(ZigbeeClusterLibrary::ClusterIdMetering);
        meteringCluster->readAttributes({ZigbeeClusterMetering::AttributeCurrentSummationDelivered});
    }
}

bool IntegrationPluginZigbeeTuya::match(ZigbeeNode *node, const QString &modelName, const QStringList &manufacturerNames)
{
    return node->modelName() == modelName && manufacturerNames.contains(node->manufacturerName());
}

