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

#ifndef ZIGBEEINTEGRATIONPLUGIN_H
#define ZIGBEEINTEGRATIONPLUGIN_H

#include "integrations/integrationplugin.h"
#include "hardware/zigbee/zigbeehandler.h"
#include "hardware/zigbee/zigbeehardwareresource.h"

#include <zcl/lighting/zigbeeclustercolorcontrol.h>


class ZigbeeIntegrationPlugin: public IntegrationPlugin, public ZigbeeHandler
{
    Q_OBJECT

public:
    explicit ZigbeeIntegrationPlugin(ZigbeeHardwareResource::HandlerType handlerType, const QLoggingCategory &loggingCategory);
    virtual ~ZigbeeIntegrationPlugin();

    virtual void init() override;
    virtual void handleRemoveNode(ZigbeeNode *node, const QUuid &networkUuid) override;
    virtual void thingRemoved(Thing *thing) override;

protected:
    bool manageNode(Thing *thing);
    Thing *thingForNode(ZigbeeNode *node);
    ZigbeeNode *nodeForThing(Thing *thing);

    void createThing(const ThingClassId &thingClassId, ZigbeeNode *node, const ParamList &additionalParams = ParamList());

    void bindPowerConfigurationCluster(ZigbeeNodeEndpoint *endpoint);
    void bindThermostatCluster(ZigbeeNodeEndpoint *endpoint);
    void bindOnOffCluster(ZigbeeNodeEndpoint *endpoint, int retries = 3);
    void bindLevelControlInputCluster(ZigbeeNodeEndpoint *endpoint);
    void bindElectricalMeasurementCluster(ZigbeeNodeEndpoint *endpoint);
    void bindMeteringCluster(ZigbeeNodeEndpoint *endpoint);
    void bindTemperatureMeasurementInputCluster(ZigbeeNodeEndpoint *endpoint, int retries = 3);
    void bindRelativeHumidityMeasurementInputCluster(ZigbeeNodeEndpoint *endpoint, int retries = 3);
    void bindIasZoneInputCluster(ZigbeeNodeEndpoint *endpoint);
    void bindIlluminanceMeasurementInputCluster(ZigbeeNodeEndpoint *endpoint, int retries = 3);

    void configureOnOffInputAttributeReporting(ZigbeeNodeEndpoint *endpoint);

    void connectToPowerConfigurationCluster(Thing *thing, ZigbeeNodeEndpoint *endpoint);
    void connectToThermostatCluster(Thing *thing, ZigbeeNodeEndpoint *endpoint);
    void connectToOnOffInputCluster(Thing *thing, ZigbeeNodeEndpoint *endpoint, const QString &stateName = "power");
    void connectToLevelControlInputCluster(Thing *thing, ZigbeeNodeEndpoint *endpoint, const QString &stateName);
    void connectToElectricalMeasurementCluster(Thing *thing, ZigbeeNodeEndpoint *endpoint);
    void connectToMeteringCluster(Thing *thing, ZigbeeNodeEndpoint *endpoint);
    void connectToTemperatureMeasurementInputCluster(Thing *thing, ZigbeeNodeEndpoint *endpoint);
    void connectToRelativeHumidityMeasurementInputCluster(Thing *thing, ZigbeeNodeEndpoint *endpoint);
    void connectToIasZoneInputCluster(Thing *thing, ZigbeeNodeEndpoint *endpoint, const QString &alarmStateName, bool inverted = false);
    void connectToIlluminanceMeasurementInputCluster(Thing *thing, ZigbeeNodeEndpoint *endpoint);

    void executePowerOnOffInputCluster(ThingActionInfo *info, ZigbeeNodeEndpoint *endpoint);
    void executeBrightnessLevelControlInputCluster(ThingActionInfo *info, ZigbeeNodeEndpoint *endpoint);
    void executeColorTemperatureColorControlInputCluster(ThingActionInfo *info, ZigbeeNodeEndpoint *endpoint);
    void executeColorColorControlInputCluster(ThingActionInfo *info, ZigbeeNodeEndpoint *endpoint);

    void readColorTemperatureRange(Thing *thing, ZigbeeNodeEndpoint *endpoint);
    quint16 mapScaledValueToColorTemperature(Thing *thing, int scaledColorTemperature);
    int mapColorTemperatureToScaledValue(Thing *thing, quint16 colorTemperature);

private:
    QHash<Thing*, ZigbeeNode*> m_thingNodes;

    ZigbeeHardwareResource::HandlerType m_handlerType = ZigbeeHardwareResource::HandlerTypeVendor;
    QLoggingCategory m_dc;

    typedef struct ColorTemperatureRange {
        quint16 minValue = 250;
        quint16 maxValue = 450;
    } ColorTemperatureRange;

    QHash<Thing *, ColorTemperatureRange> m_colorTemperatureRanges;
    QHash<Thing *, ZigbeeClusterColorControl::ColorCapabilities> m_colorCapabilities;
};

#endif // INTEGRATIONPLUGINZIGBEEEUROTRONIC_H
