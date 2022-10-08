/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * *
*
* Copyright 2013 - 2021, nymea GmbH
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

#ifndef INTEGRATIONPLUGINZIGBEEDEVELCO_H
#define INTEGRATIONPLUGINZIGBEEDEVELCO_H

#include "../common/zigbeeintegrationplugin.h"
#include "plugintimer.h"

#include <QTimer>

#include <integrations/integrationplugin.h>
#include <hardware/zigbee/zigbeehandler.h>

#include <zigbeereply.h>

#include "extern-plugininfo.h"

/* IO Module
 *
 * https://www.develcoproducts.com/media/1967/iomzb-110-technical-manual-io-module.pdf
 *
 * Profile 0xC0C9 (Develco Products private profile)
 * Endpoints:
 * - 0x70 Simple sensor: input 1
 * - 0x71 Simple sensor: input 2
 * - 0x72 Simple sensor: input 3
 * - 0x74 Simple sensor: input 4
 * - 0x75 On/Off Output : relay 1
 * - 0x76 On/Off Output : relay 2
 */
#define IO_MODULE_EP_INPUT1 0x70
#define IO_MODULE_EP_INPUT2 0x71
#define IO_MODULE_EP_INPUT3 0x72
#define IO_MODULE_EP_INPUT4 0x73
#define IO_MODULE_EP_OUTPUT1 0x74
#define IO_MODULE_EP_OUTPUT2 0x75

/* Develco specific application endpoints */
#define DEVELCO_EP_IAS_ZONE 0x23
#define DEVELCO_EP_TEMPERATURE_SENSOR 0x026
#define DEVELCO_EP_LIGHT_SENSOR 0x27

#define AIR_QUALITY_SENSOR_VOC_MEASUREMENT_CLUSTER_ID 0xfc03
#define AIR_QUALITY_SENSOR_VOC_MEASUREMENT_ATTRIBUTE_MEASURED_VALUE 0x0000
#define AIR_QUALITY_SENSOR_VOC_MEASUREMENT_ATTRIBUTE_MIN_MEASURED_VALUE 0x0001
#define AIR_QUALITY_SENSOR_VOC_MEASUREMENT_ATTRIBUTE_RESOLUTION 0x0003


/* Develco manufacturer specific Basic cluster attributes */
#define DEVELCO_BASIC_ATTRIBUTE_SW_VERSION 0x8000
#define DEVELCO_BASIC_ATTRIBUTE_BOOTLOADER_VERSION 0x8010
#define DEVELCO_BASIC_ATTRIBUTE_HARDWARE_VERSION 0x8020
#define DEVELCO_BASIC_ATTRIBUTE_HARDWARE_NAME 0x8030
#define DEVELCO_BASIC_ATTRIBUTE_3RD_PARTY_SW_VERSION 0x8050


class IntegrationPluginZigbeeDevelco: public ZigbeeIntegrationPlugin
{
    Q_OBJECT

    Q_PLUGIN_METADATA(IID "io.nymea.IntegrationPlugin" FILE "integrationpluginzigbeedevelco.json")
    Q_INTERFACES(IntegrationPlugin)

public:
    explicit IntegrationPluginZigbeeDevelco();

    QString name() const override;
    bool handleNode(ZigbeeNode *node, const QUuid &networkUuid) override;

    void setupThing(ThingSetupInfo *info) override;
    void postSetupThing(Thing *thing) override;
    void executeAction(ThingActionInfo *info) override;

private:    
    QString parseDevelcoVersionString(ZigbeeNodeEndpoint *endpoint);

    void initIoModule(ZigbeeNode *node);

    void configureOnOffPowerReporting(ZigbeeNode *node, ZigbeeNodeEndpoint *endpoint);
    void configureBinaryInputReporting(ZigbeeNode *node, ZigbeeNodeEndpoint *endpoint);

    void configureTemperatureReporting(ZigbeeNode *node, ZigbeeNodeEndpoint *endpoint);
    void configureHumidityReporting(ZigbeeNode *node, ZigbeeNodeEndpoint *endpoint);
    void configureBattryVoltageReporting(ZigbeeNode *node, ZigbeeNodeEndpoint *endpoint);
    void configureVocReporting(ZigbeeNode *node, ZigbeeNodeEndpoint *endpoint);

    void readDevelcoFirmwareVersion(ZigbeeNode *node, ZigbeeNodeEndpoint *endpoint);
    void readOnOffPowerAttribute(ZigbeeNode *node, ZigbeeNodeEndpoint *endpoint);
    void readBinaryInputPresentValueAttribute(ZigbeeNode *node, ZigbeeNodeEndpoint *endpoint);

    void readIoModuleOutputPowerStates(Thing *thing);
    void readIoModuleInputPowerStates(Thing *thing);

};

#endif // INTEGRATIONPLUGINZIGBEEDEVELCO_H
