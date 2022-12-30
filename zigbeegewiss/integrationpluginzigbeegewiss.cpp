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

#include "integrationpluginzigbeegewiss.h"
#include "plugininfo.h"

#include <hardware/zigbee/zigbeehardwareresource.h>
#include <plugintimer.h>

#include <zigbeenodeendpoint.h>
#include <zcl/general/zigbeeclusteronoff.h>
#include <zcl/general/zigbeeclusterbinaryinput.h>

#include <QDebug>

IntegrationPluginZigbeeGewiss::IntegrationPluginZigbeeGewiss(): ZigbeeIntegrationPlugin(ZigbeeHardwareResource::HandlerTypeVendor, dcZigbeeGewiss())
{
}

QString IntegrationPluginZigbeeGewiss::name() const
{
    return "Gewiss";
}

bool IntegrationPluginZigbeeGewiss::handleNode(ZigbeeNode *node, const QUuid &/*networkUuid*/)
{
    qCDebug(dcZigbeeGewiss()) << "Handle node:" << node->nodeDescriptor().manufacturerCode;

    if (node->nodeDescriptor().manufacturerCode != 0x1994) {
        return false;
    }

    if (node->modelName().startsWith("GWA1501") || node->modelName().startsWith("GWA1502")) {
        qCDebug(dcZigbeeGewiss()) << "Handling" << node->modelName();

        ZigbeeNodeEndpoint *endpoint1 = node->getEndpoint(0x01);
        ZigbeeNodeEndpoint *endpoint2 = node->getEndpoint(0x02);

        if (!endpoint1 || !endpoint2) {
            qCWarning(dcZigbeeGewiss()) << "Unable to get endpoints from device.";
            return false;
        }

        bindPowerConfigurationCluster(endpoint1);
        configurePowerConfigurationInputClusterAttributeReporting(endpoint1);

        bindOnOffCluster(endpoint2);
        bindOnOffCluster(endpoint1);

        // Device supports BinaryInput but that doesn't seem to report any attribute changes, no matter the DIP switches
        // Device supports LevelControl. We could bind that for longpress, but binding it changes the behavior of the OnOff
        // cluster and makes it rather unmangeable. So only using the OnOff cluster which is the only setting that has an acceptable
        // WAF anyways. So we're supporting DIP switches B1 to B4 and not bother with the rest.
        // Device supports ScenesControl. Same as LevelControl tho.

//        bindLevelControlOutputCluster(node, endpoint1);
//        bindLevelControlOutputCluster(node, endpoint2);

        createThing(gwa1501BinaryInputThingClassId, node);
        return true;
    }

    if (node->modelName().startsWith("GWA1521")) {
        ZigbeeNodeEndpoint *endpoint1 = node->getEndpoint(0x01);
        bindOnOffCluster(endpoint1);
        createThing(gwa1521ActuatorThingClassId, node);
        return true;
    }

    return false;
}

void IntegrationPluginZigbeeGewiss::setupThing(ThingSetupInfo *info)
{
    qCDebug(dcZigbeeGewiss()) << "Setting up thing" << info->thing()->name();
    Thing *thing = info->thing();

    if (!manageNode(thing)) {
        qCWarning(dcZigbeeGewiss()) << "Failed to claim node during setup.";
        info->finish(Thing::ThingErrorHardwareNotAvailable);
        return;
    }

    ZigbeeNode *node = nodeForThing(thing);


    if (thing->thingClassId() == gwa1501BinaryInputThingClassId) {

        ZigbeeNodeEndpoint *endpoint1 = node->getEndpoint(0x01);
        ZigbeeNodeEndpoint *endpoint2 = node->getEndpoint(0x02);

        if (!endpoint1 || !endpoint2) {
            qCWarning(dcZigbeeGewiss()) << "one ore more endpoints not found" << thing->name();
            return;
        }

        connectToPowerConfigurationInputCluster(thing, endpoint1);
        connectToOnOffOutputCluster(thing, endpoint1, "Toggle 1", "On 1", "Off 1", "input1");
        connectToOnOffOutputCluster(thing, endpoint2, "Toggle 2", "On 2", "Off 2", "input2");

        info->finish(Thing::ThingErrorNoError);
        return;

        // Single channel relay
    } else if (thing->thingClassId() == gwa1521ActuatorThingClassId) {
        ZigbeeNodeEndpoint *endpoint = node->getEndpoint(0x01);
        if (!endpoint) {
            qCWarning(dcZigbeeGewiss()) << "Endpoint not found" << thing->name();
            return;
        }

        ZigbeeClusterOnOff *onOffCluster = endpoint->inputCluster<ZigbeeClusterOnOff>(ZigbeeClusterLibrary::ClusterIdOnOff);
        if (!onOffCluster) {
            qCWarning(dcZigbeeGewiss()) << "Could not find on/off cluster on" << thing << endpoint;
        } else {
            if (onOffCluster->hasAttribute(ZigbeeClusterOnOff::AttributeOnOff)) {
                thing->setStateValue(gwa1521ActuatorRelayStateTypeId, onOffCluster->power());
            }

            connect(onOffCluster, &ZigbeeClusterOnOff::powerChanged, thing, [thing](bool power){
                qCDebug(dcZigbeeGewiss()) << thing << "power changed" << power;
                thing->setStateValue(gwa1521ActuatorRelayStateTypeId, power);
            });
        }
        return info->finish(Thing::ThingErrorNoError);
    }
    qCWarning(dcZigbeeGewiss()) << "Thing class not found" << info->thing()->thingClassId();
    Q_ASSERT_X(false, "ZigbeeGewiss", "Unhandled thing class");
    return info->finish(Thing::ThingErrorThingClassNotFound);
}

void IntegrationPluginZigbeeGewiss::executeAction(ThingActionInfo *info)
{
    Action action = info->action();
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

    if (thing->thingClassId() == gwa1521ActuatorThingClassId) {
        if (action.actionTypeId() == gwa1521ActuatorRelayActionTypeId) {

            if (info->action().actionTypeId() == gwa1521ActuatorRelayActionTypeId) {
                ZigbeeNodeEndpoint *endpoint = node->getEndpoint(0x01);
                if (!endpoint) {
                    qCWarning(dcZigbeeGewiss()) << "Unable to get the endpoint from node" << node << "for" << thing;
                    info->finish(Thing::ThingErrorSetupFailed);
                    return;
                }

                ZigbeeClusterOnOff *onOffCluster = endpoint->inputCluster<ZigbeeClusterOnOff>(ZigbeeClusterLibrary::ClusterIdOnOff);
                if (!onOffCluster) {
                    qCWarning(dcZigbeeGewiss()) << "Unable to get the OnOff cluster from endpoint" << endpoint << "on" << node << "for" << thing;
                    info->finish(Thing::ThingErrorSetupFailed);
                    return;
                }
                bool power = info->action().param(gwa1521ActuatorRelayActionRelayParamTypeId).value().toBool();
                ZigbeeClusterReply *reply = (power ? onOffCluster->commandOn() : onOffCluster->commandOff());
                connect(reply, &ZigbeeClusterReply::finished, this, [=](){
                    // Note: reply will be deleted automatically
                    if (reply->error() != ZigbeeClusterReply::ErrorNoError) {
                        info->finish(Thing::ThingErrorHardwareFailure);
                    } else {
                        info->finish(Thing::ThingErrorNoError);
                        thing->setStateValue(gwa1521ActuatorRelayStateTypeId, power);
                    }
                });
            }
        }
    } else {
        qCDebug(dcZigbeeGewiss()) << "Execute action" << info->thing()->name() << info->action().actionTypeId();
        info->finish(Thing::ThingErrorUnsupportedFeature);
    }
}

void IntegrationPluginZigbeeGewiss::connectToOnOffOutputCluster(Thing *thing, ZigbeeNodeEndpoint *endpoint, const QString &toggleButton, const QString &onButton, const QString &offButton, const QString &powerStateName)
{
    ZigbeeClusterOnOff *onOffCluster = endpoint->outputCluster<ZigbeeClusterOnOff>(ZigbeeClusterLibrary::ClusterIdOnOff);
    if (onOffCluster) {
        connect(onOffCluster, &ZigbeeClusterOnOff::commandReceived, thing, [=](ZigbeeClusterOnOff::Command command, const QByteArray &parameters, quint8 transactionSequenceNumber){
            qCDebug(dcZigbeeGewiss()) << "Command received!" << command << parameters << transactionSequenceNumber;
            switch (command) {
            case ZigbeeClusterOnOff::CommandToggle:
                thing->emitEvent("pressed", {Param(gwa1501BinaryInputPressedEventButtonNameParamTypeId, toggleButton)});
                return;
            case ZigbeeClusterOnOff::CommandOn:
                thing->emitEvent("pressed", {Param(gwa1501BinaryInputPressedEventButtonNameParamTypeId, onButton)});
                thing->setStateValue(powerStateName, true);
                return;
            case ZigbeeClusterOnOff::CommandOff:
                thing->emitEvent("pressed", {Param(gwa1501BinaryInputPressedEventButtonNameParamTypeId, offButton)});
                thing->setStateValue(powerStateName, false);
                return;
            default:
                qCWarning(dcZigbeeGewiss()) << "Unhandled OnOff cluster command:" << command;
            }
        });
    }
}

void IntegrationPluginZigbeeGewiss::bindBinaryInputCluster(ZigbeeNode *node, ZigbeeNodeEndpoint *endpoint)
{
    ZigbeeDeviceObjectReply * zdoReply = node->deviceObject()->requestBindGroupAddress(endpoint->endpointId(), ZigbeeClusterLibrary::ClusterIdBinaryInput, 0x0000);
    connect(zdoReply, &ZigbeeDeviceObjectReply::finished, endpoint, [=](){
        if (zdoReply->error() != ZigbeeDeviceObjectReply::ErrorNoError) {
            qCWarning(dcZigbeeGewiss()) << "Failed to bind bianry cluster to coordinator" << zdoReply->error();
        } else {
            qCDebug(dcZigbeeGewiss()) << "Bind binary cluster to coordinator finished successfully";
        }

        ZigbeeClusterLibrary::AttributeReportingConfiguration inputConfig;
        inputConfig.attributeId = ZigbeeClusterBinaryInput::AttributePresentValue;
        inputConfig.dataType = Zigbee::Uint8;
        inputConfig.minReportingInterval = 1;
        inputConfig.maxReportingInterval = 120;
        inputConfig.reportableChange = ZigbeeDataType(static_cast<quint8>(1)).data();

        ZigbeeClusterReply *reportingReply = endpoint->getInputCluster(ZigbeeClusterLibrary::ClusterIdBinaryInput)->configureReporting({inputConfig});
        connect(reportingReply, &ZigbeeClusterReply::finished, this, [=](){
            if (reportingReply->error() != ZigbeeClusterReply::ErrorNoError) {
                qCWarning(dcZigbeeGewiss()) << "Failed to configure Binary Input cluster attribute reporting" << reportingReply->error();
            }
        });

    });
}
