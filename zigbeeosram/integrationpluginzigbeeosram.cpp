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

#include "integrationpluginzigbeeosram.h"
#include "plugininfo.h"
#include "hardware/zigbee/zigbeehardwareresource.h"

//#include "zcl/hvac/zigbeeclusterthermostat.h"
//#include "zcl/closures/zigbeeclusterdoorlock.h"
//#include "zcl/general/zigbeeclusteridentify.h"
#include "zcl/general/zigbeeclusteronoff.h"
#include "zcl/general/zigbeeclusterlevelcontrol.h"
//#include "zcl/security/zigbeeclusteriaszone.h"
//#include "zcl/security/zigbeeclusteriaswd.h"

#include <QDebug>

IntegrationPluginZigbeeOsram::IntegrationPluginZigbeeOsram(): ZigbeeIntegrationPlugin(ZigbeeHardwareResource::HandlerTypeVendor, dcZigbeeOsram())
{
}

QString IntegrationPluginZigbeeOsram::name() const
{
    return "Osram";
}

bool IntegrationPluginZigbeeOsram::handleNode(ZigbeeNode *node, const QUuid &/*networkUuid*/)
{
    if (node->nodeDescriptor().manufacturerCode != 0x110c) {
        return false;
    }

    if (QStringList{"Lightify Switch Mini", "Lightify Switch Mini blue"}.contains(node->modelName())) {
        ZigbeeNodeEndpoint *ep1 = node->getEndpoint(1),
                *ep2 = node->getEndpoint(2),
                *ep3 = node->getEndpoint(3);

        if (!ep1 || !ep2 || !ep3) {
            qCWarning(dcZigbeeOsram) << "Expected endpoint not found on Light switch mini";
            return false;
        }
        createThing(switchMiniThingClassId, node);

        bindCluster(ep1, ZigbeeClusterLibrary::ClusterIdPowerConfiguration);
        bindCluster(ep1, ZigbeeClusterLibrary::ClusterIdOnOff);
        bindCluster(ep1, ZigbeeClusterLibrary::ClusterIdLevelControl);

        bindCluster(ep2, ZigbeeClusterLibrary::ClusterIdOnOff);
        bindCluster(ep2, ZigbeeClusterLibrary::ClusterIdLevelControl);

        bindCluster(ep3, ZigbeeClusterLibrary::ClusterIdLevelControl);
        bindCluster(ep3, ZigbeeClusterLibrary::ClusterIdColorControl);
        return true;
    }

    return false;
}

void IntegrationPluginZigbeeOsram::setupThing(ThingSetupInfo *info)
{
    Thing *thing = info->thing();

    ZigbeeNode *node = manageNode(thing);
    if (!node) {
        qCWarning(dcZigbeeOsram()) << "Failed to claim node during setup.";
        info->finish(Thing::ThingErrorHardwareNotAvailable);
        return;
    }


    if (thing->thingClassId() == switchMiniThingClassId) {
        ZigbeeNodeEndpoint *ep1 = node->getEndpoint(1),
                *ep2 = node->getEndpoint(2),
                *ep3 = node->getEndpoint(3);

        thing->setStateValue("currentVersion", ep1->softwareBuildId());

        connectToPowerConfigurationInputCluster(thing, ep1, 3, 2.5);
        connectToOtaOutputCluster(thing, ep1);


        ZigbeeClusterOnOff *onOff1 = ep1->outputCluster<ZigbeeClusterOnOff>(ZigbeeClusterLibrary::ClusterIdOnOff);
        if (!onOff1) {
            qCWarning(dcZigbeeOsram()) << "Could not find level control output cluster on" << thing << 1;
        } else {
            connect(onOff1, &ZigbeeClusterOnOff::commandReceived, thing, [=](ZigbeeClusterOnOff::Command command, const QByteArray &/*payload*/, quint8 transactionSequenceNumber){
                if (!deduplicate(thing, transactionSequenceNumber)) {
                    return;
                }
                switch (command) {
                case ZigbeeClusterOnOff::CommandOn:
                    thing->emitEvent(switchMiniPressedEventTypeId, ParamList{{switchMiniPressedEventButtonNameParamTypeId, "UP"}});
                    break;
                default:
                    qCInfo(dcZigbeeOsram()) << "Unhandled button press on" << thing->name() << "in level control cluster on EP 1";
                }
            });
        }

        ZigbeeClusterOnOff *onOff2 = ep2->outputCluster<ZigbeeClusterOnOff>(ZigbeeClusterLibrary::ClusterIdOnOff);
        if (!onOff2) {
            qCWarning(dcZigbeeOsram()) << "Could not find level control output cluster on" << thing << 2;
        } else {
            connect(onOff2, &ZigbeeClusterOnOff::commandReceived, thing, [=](ZigbeeClusterOnOff::Command command, const QByteArray &/*payload*/, quint8 transactionSequenceNumber){
                if (!deduplicate(thing, transactionSequenceNumber)) {
                    return;
                }
                switch (command) {
                case ZigbeeClusterOnOff::CommandOff:
                    thing->emitEvent(switchMiniPressedEventTypeId, ParamList{{switchMiniPressedEventButtonNameParamTypeId, "DOWN"}});
                    break;
                default:
                    qCInfo(dcZigbeeOsram()) << "Unhandled button press on" << thing->name() << "in level control cluster on EP 2";
                }
            });
        }

        ZigbeeClusterLevelControl *levelCluster1 = ep1->outputCluster<ZigbeeClusterLevelControl>(ZigbeeClusterLibrary::ClusterIdLevelControl);
        if (!levelCluster1) {
            qCWarning(dcZigbeeOsram()) << "Could not find level control output cluster on" << thing << 1;
        } else {
            connect(levelCluster1, &ZigbeeClusterLevelControl::commandReceived, thing, [=](ZigbeeClusterLevelControl::Command command, const QByteArray &/*parameter*/, quint8 transactionSequenceNumber){
                if (!deduplicate(thing, transactionSequenceNumber)) {
                    return;
                }
                switch (command) {
                case ZigbeeClusterLevelControl::CommandMoveWithOnOff:
                    thing->emitEvent(switchMiniLongPressedEventTypeId, ParamList{{switchMiniLongPressedEventButtonNameParamTypeId, "UP"}});
                    break;
                case ZigbeeClusterLevelControl::CommandMoveToLevelWithOnOff:
                    thing->emitEvent(switchMiniPressedEventTypeId, ParamList{{switchMiniPressedEventButtonNameParamTypeId, "TOGGLE"}});
                    break;
                default:
                    qCInfo(dcZigbeeOsram()) << "Unhandled button press on" << thing->name() << "in level control cluster on EP 1";
                }
            });
        }

        ZigbeeClusterLevelControl *levelCluster2 = ep2->outputCluster<ZigbeeClusterLevelControl>(ZigbeeClusterLibrary::ClusterIdLevelControl);
        if (!levelCluster2) {
            qCWarning(dcZigbeeOsram()) << "Could not find level control output cluster on" << thing << 2;
        } else {
            connect(levelCluster2, &ZigbeeClusterLevelControl::commandReceived, thing, [=](ZigbeeClusterLevelControl::Command command, const QByteArray &/*parameter*/, quint8 transactionSequenceNumber){
                if (!deduplicate(thing, transactionSequenceNumber)) {
                    return;
                }
                switch (command) {
                case ZigbeeClusterLevelControl::CommandMove:
                    thing->emitEvent(switchMiniLongPressedEventTypeId, ParamList{{switchMiniLongPressedEventButtonNameParamTypeId, "DOWN"}});
                    break;
                default:
                    qCInfo(dcZigbeeOsram()) << "Unhandled button press on" << thing->name() << " in level control cluster on EP 2";
                }
            });
        }

        ZigbeeClusterColorControl *cluster3 = ep3->outputCluster<ZigbeeClusterColorControl>(ZigbeeClusterLibrary::ClusterIdColorControl);
        connect(cluster3, &ZigbeeClusterColorControl::commandReceived, thing, [=](ZigbeeClusterColorControl::Command command, const QByteArray &payload, quint8 transactionSequenceNumber){
            qCDebug(dcZigbeeOsram()) << "***** data indication on cc cluster 3" << command << payload << transactionSequenceNumber;
            if (!deduplicate(thing, transactionSequenceNumber)) {
                return;
            }
            switch (command) {
            case ZigbeeClusterColorControl::CommandMoveToColorTemperature:
                thing->emitEvent(switchMiniPressedEventTypeId, ParamList{{switchMiniPressedEventButtonNameParamTypeId, "TOGGLE"}});
                break;
            case ZigbeeClusterColorControl::CommandMoveToSaturation:
                thing->emitEvent(switchMiniLongPressedEventTypeId, ParamList{{switchMiniLongPressedEventButtonNameParamTypeId, "TOGGLE"}});
                break;
            default:
                qCInfo(dcZigbeeOsram()) << "Unhandled button press on" << thing->name() << "in color control cluster on EP 3";
            }

        });

    }

    info->finish(Thing::ThingErrorNoError);
}

void IntegrationPluginZigbeeOsram::executeAction(ThingActionInfo *info)
{
    ZigbeeNode *node = nodeForThing(info->thing());
    if (!node) {
        qCWarning(dcZigbeeOsram()) << "Unable to find zigbee node for thing" << info->thing()->name();
        info->finish(Thing::ThingErrorHardwareNotAvailable);
        return;
    }

    if (info->action().actionTypeId() == switchMiniPerformUpdateActionTypeId) {
        enableFirmwareUpdate(info->thing());
        ZigbeeNodeEndpoint *ep1 = node->getEndpoint(1);
        executeImageNotifyOtaOutputCluster(info, ep1);
    }
}

bool IntegrationPluginZigbeeOsram::deduplicate(Thing *thing, quint8 transactionSequenceNumber)
{
    int diff = transactionSequenceNumber - m_transactionSequenceNumbers.value(thing);
    if (diff <= 0 && diff > -10) {
        qCDebug(dcZigbeeOsram()) << "Deduplicating transaction" << transactionSequenceNumber;
        return false;
    }
    m_transactionSequenceNumbers[thing] = transactionSequenceNumber;
    return true;
}
