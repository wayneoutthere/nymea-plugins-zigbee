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
#include "hardware/zigbee/zigbeehardwareresource.h"

#include "zcl/hvac/zigbeeclusterthermostat.h"

#include <QDebug>

#define ATTRIBUTE_ID_SOCKET_POWER_ON_DEFAULT_MODE 0x8002

IntegrationPluginZigbeeTuya::IntegrationPluginZigbeeTuya(): ZigbeeIntegrationPlugin(ZigbeeHardwareResource::HandlerTypeVendor)
{
}

QString IntegrationPluginZigbeeTuya::name() const
{
    return "Tuya";
}

bool IntegrationPluginZigbeeTuya::handleNode(ZigbeeNode *node, const QUuid &/*networkUuid*/)
{
    if (node->nodeDescriptor().manufacturerCode == 0x1141 || node->modelName() == "TS011F") {
        qCDebug(dcZigbeeTuya()) << "Tuya smart plug";

        ZigbeeNodeEndpoint *endpoint = node->getEndpoint(0x01);
        if (!endpoint) {
            qCWarning(dcZigbeeTuya()) << "Endpoint 1 not found on device....";
            return false;
        }

        bindOnOffCluster(node, endpoint);
        bindElectricalMeasurementCluster(endpoint);
        bindMeteringCluster(endpoint);

        createThing(powerSocketThingClassId, node);

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
        connectToOnOffCluster(thing, endpoint);
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
                    ZigbeeDataType dataType(map.key(value.toString()), Zigbee::Enum8);
                    ZigbeeClusterLibrary::WriteAttributeRecord record;
                    record.attributeId = ATTRIBUTE_ID_SOCKET_POWER_ON_DEFAULT_MODE;
                    record.dataType = dataType.dataType();
                    record.data = dataType.data();
                    onOffCluster->writeAttributes({record});
                }
            });
        }
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

