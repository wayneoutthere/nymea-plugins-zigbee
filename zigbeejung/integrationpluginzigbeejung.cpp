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

#include "integrationpluginzigbeejung.h"
#include "plugininfo.h"

#include <hardware/zigbee/zigbeehardwareresource.h>
#include <zcl/general/zigbeeclusteronoff.h>
#include <zcl/general/zigbeeclusterlevelcontrol.h>
#include <zcl/general/zigbeeclusterscenes.h>
#include <zcl/general/zigbeeclusterpowerconfiguration.h>

#include <QDebug>


IntegrationPluginZigbeeJung::IntegrationPluginZigbeeJung():
    ZigbeeIntegrationPlugin(ZigbeeHardwareResource::HandlerTypeVendor, dcZigbeeJung())
{
}

QString IntegrationPluginZigbeeJung::name() const
{
    return "Remotes";
}

bool IntegrationPluginZigbeeJung::handleNode(ZigbeeNode *node, const QUuid &/*networkUuid*/)
{
    qCDebug(dcZigbeeJung) << "Evaluating node:" << node << node->nodeDescriptor().manufacturerCode << node->modelName();
    bool handled = false;
    // "Insta" remote (JUNG ZLL 5004)
    if (node->nodeDescriptor().manufacturerCode == 0x117A && node->modelName() == " Remote") {
        ZigbeeNodeEndpoint *endpoint = node->getEndpoint(0x01);
        if (!endpoint) {
            qCWarning(dcZigbeeJung()) << "Device claims to be an Insta remote but does not provide endpoint 1";
            return false;
        }

        createThing(instaThingClassId, node);

        // Nothing to be done here... The device does not support battery level updates and will send all the commands
        // to the coordinator unconditionally, no need to bind any clusters...

        handled = true;
    }

    return handled;
}

void IntegrationPluginZigbeeJung::setupThing(ThingSetupInfo *info)
{
    Thing *thing = info->thing();

    if (!manageNode(thing)) {
        qCWarning(dcZigbeeJung()) << "Failed to claim node during setup.";
        info->finish(Thing::ThingErrorHardwareNotAvailable);
        return;
    }

    ZigbeeNode *node = nodeForThing(thing);

    if (thing->thingClassId() == instaThingClassId) {
        ZigbeeNodeEndpoint *endpoint = node->getEndpoint(0x01);

        ZigbeeClusterOnOff *onOffCluster = endpoint->outputCluster<ZigbeeClusterOnOff>(ZigbeeClusterLibrary::ClusterIdOnOff);
        ZigbeeClusterLevelControl *levelControlCluster = endpoint->outputCluster<ZigbeeClusterLevelControl>(ZigbeeClusterLibrary::ClusterIdLevelControl);
        ZigbeeClusterScenes *scenesCluster = endpoint->outputCluster<ZigbeeClusterScenes>(ZigbeeClusterLibrary::ClusterIdScenes);
        if (!onOffCluster || !levelControlCluster || !scenesCluster) {
            qCWarning(dcZigbeeJung()) << "Could not find all of the needed clusters for" << thing->name() << "in" << node << "on endpoint" << endpoint->endpointId();
            info->finish(Thing::ThingErrorHardwareNotAvailable);
            return;
        }
        connect(onOffCluster, &ZigbeeClusterOnOff::commandReceived, this, [=](ZigbeeClusterOnOff::Command command, const QByteArray &parameters){
            qCDebug(dcZigbeeJung()) << "OnOff command received:" << command << parameters;
            switch (command) {
            case ZigbeeClusterOnOff::CommandOn:
                thing->emitEvent(instaPressedEventTypeId, {Param(instaPressedEventButtonNameParamTypeId, "ON")});
                break;
            case ZigbeeClusterOnOff::CommandOffWithEffect:
                thing->emitEvent(instaPressedEventTypeId, {Param(instaPressedEventButtonNameParamTypeId, "OFF")});
                break;
            default:
                qCWarning(dcZigbeeJung()) << "Unhandled command from Insta Remote:" << command << parameters.toHex();
            }
        });
        connect(levelControlCluster, &ZigbeeClusterLevelControl::commandStepReceived, this, [=](bool withOnOff, ZigbeeClusterLevelControl::StepMode stepMode, quint8 stepSize, quint16 transitionTime){
            qCDebug(dcZigbeeJung()) << "Level command received" << withOnOff << stepMode << stepSize << transitionTime;
            thing->emitEvent(instaPressedEventTypeId, {Param(instaPressedEventButtonNameParamTypeId, stepMode == ZigbeeClusterLevelControl::StepModeUp ? "+" : "-")});
        });
        connect(scenesCluster, &ZigbeeClusterScenes::commandReceived, this, [=](ZigbeeClusterScenes::Command command, quint16 groupId, quint8 sceneId){
            qCDebug(dcZigbeeJung()) << "Scenes command received:" << command << groupId << sceneId;
            thing->emitEvent(instaPressedEventTypeId, {Param(instaPressedEventButtonNameParamTypeId, QString::number(sceneId))});
        });


        // The device also supports setting saturation, color and color temperature. However, it's quite funky to
        // actually get there on the device and that mode seems to be only enabled if there are bindings to
        // actual lamps. Once it's bound to lamps, pressing on and off simultaneously will start cycling through the bound
        // lights and during that mode, the color/saturation/temperature will act on the currently selected lamp only.
        // After some seconds without button press, it will revert back to the default mode where it sends all commands
        // to the coordinator *and* all the bound lights simultaneously.
        // So, in order to get that working we'd need to fake a like and somehow allow binding that via touch-link from a key-combo on the device.

        // Not supporting that here... A user may still additionally bind the device to a lamp and use that feature with the remote....

        connectToOtaOutputCluster(thing, endpoint);

        info->finish(Thing::ThingErrorNoError);
        return;
    }

    info->finish(Thing::ThingErrorNoError);
}

void IntegrationPluginZigbeeJung::executeAction(ThingActionInfo *info)
{
    ZigbeeNode *node = nodeForThing(info->thing());

    if (info->action().actionTypeId() == instaPerformUpdateActionTypeId) {
        enableFirmwareUpdate(info->thing());
        executeImageNotifyOtaOutputCluster(info, node->getEndpoint(1));
        return;
    }
    info->finish(Thing::ThingErrorUnsupportedFeature);
}
