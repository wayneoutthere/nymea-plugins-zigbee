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

#ifndef INTEGRATIONPLUGINZIGBEEPHILIPSHUE_H
#define INTEGRATIONPLUGINZIGBEEPHILIPSHUE_H

#include "../common/zigbeeintegrationplugin.h"
#include "hardware/zigbee/zigbeehandler.h"
#include "extern-plugininfo.h"
#include <QTimer>

class IntegrationPluginZigbeePhilipsHue: public ZigbeeIntegrationPlugin
{
    Q_OBJECT

    Q_PLUGIN_METADATA(IID "io.nymea.IntegrationPlugin" FILE "integrationpluginzigbeephilipshue.json")
    Q_INTERFACES(IntegrationPlugin)

public:
    explicit IntegrationPluginZigbeePhilipsHue();

    QString name() const override;
    bool handleNode(ZigbeeNode *node, const QUuid &networkUuid) override;

    void setupThing(ThingSetupInfo *info) override;
    void executeAction(ThingActionInfo *info) override;

private slots:
    void pollLight(Thing *thing);

private:
    void bindManufacturerSpecificPhilipsCluster(ZigbeeNodeEndpoint *endpoint);

    QHash<Thing*, PluginTimer*> m_pollTimers;
};

#endif // INTEGRATIONPLUGINZIGBEEPHILIPSHUE_H
