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
#include "plugintimer.h"

#include <zcl/lighting/zigbeeclustercolorcontrol.h>
#include <zcl/ota/zigbeeclusterota.h>

#include <QFuture>

class FetchFirmwareReply;

class ZigbeeIntegrationPlugin: public IntegrationPlugin, public ZigbeeHandler
{
    Q_OBJECT

public:
    class FirmwareIndexEntry {
    public:
        quint16 manufacturerCode = 0;
        quint16 imageType = 0;
        quint32 fileVersion = 0;
        quint32 minFileVersion = 0;
        quint32 maxFileVersion = 0;
        quint32 fileSize = 0;
        QString modelId;
        QUrl url;
        QByteArray sha512;
    };

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
    void bindLevelControlCluster(ZigbeeNodeEndpoint *endpoint);
    void bindColorControlCluster(ZigbeeNodeEndpoint *endpoint);
    void bindElectricalMeasurementCluster(ZigbeeNodeEndpoint *endpoint);
    void bindMeteringCluster(ZigbeeNodeEndpoint *endpoint);
    void bindTemperatureMeasurementCluster(ZigbeeNodeEndpoint *endpoint, int retries = 3);
    void bindRelativeHumidityMeasurementCluster(ZigbeeNodeEndpoint *endpoint, int retries = 3);
    void bindIasZoneCluster(ZigbeeNodeEndpoint *endpoint);
    void bindIlluminanceMeasurementCluster(ZigbeeNodeEndpoint *endpoint, int retries = 3);
    void bindOccupancySensingCluster(ZigbeeNodeEndpoint *endpoint);

    void configurePowerConfigurationInputClusterAttributeReporting(ZigbeeNodeEndpoint *endpoint);
    void configureOnOffInputClusterAttributeReporting(ZigbeeNodeEndpoint *endpoint);
    void configureLevelControlInputClusterAttributeReporting(ZigbeeNodeEndpoint *endpoint);
    void configureColorControlInputClusterAttributeReporting(ZigbeeNodeEndpoint *endpoint);
    void configureElectricalMeasurementInputClusterAttributeReporting(ZigbeeNodeEndpoint *endpoint);
    void configureMeteringInputClusterAttributeReporting(ZigbeeNodeEndpoint *endpoint);
    void configureTemperatureMeasurementInputClusterAttributeReporting(ZigbeeNodeEndpoint *endpoint);
    void configureRelativeHumidityMeasurementInputClusterAttributeReporting(ZigbeeNodeEndpoint *endpoint);
    void configureIlluminanceMeasurementInputClusterAttributeReporting(ZigbeeNodeEndpoint *endpoint);
    void configureOccupancySensingInputClusterAttributeReporting(ZigbeeNodeEndpoint *endpoint);

    void connectToPowerConfigurationInputCluster(Thing *thing, ZigbeeNodeEndpoint *endpoint);
    void connectToThermostatCluster(Thing *thing, ZigbeeNodeEndpoint *endpoint);
    void connectToOnOffInputCluster(Thing *thing, ZigbeeNodeEndpoint *endpoint, const QString &stateName = "power");
    void connectToLevelControlInputCluster(Thing *thing, ZigbeeNodeEndpoint *endpoint, const QString &stateName);
    void connectToColorControlInputCluster(Thing *thing, ZigbeeNodeEndpoint *endpoint);
    void connectToElectricalMeasurementCluster(Thing *thing, ZigbeeNodeEndpoint *endpoint);
    void connectToMeteringCluster(Thing *thing, ZigbeeNodeEndpoint *endpoint);
    void connectToTemperatureMeasurementInputCluster(Thing *thing, ZigbeeNodeEndpoint *endpoint);
    void connectToRelativeHumidityMeasurementInputCluster(Thing *thing, ZigbeeNodeEndpoint *endpoint);
    void connectToIasZoneInputCluster(Thing *thing, ZigbeeNodeEndpoint *endpoint, const QString &alarmStateName, bool inverted = false);
    void connectToIlluminanceMeasurementInputCluster(Thing *thing, ZigbeeNodeEndpoint *endpoint);
    void connectToOccupancySensingInputCluster(Thing *thing, ZigbeeNodeEndpoint *endpoint);
    void connectToOtaOutputCluster(Thing *thing, ZigbeeNodeEndpoint *endpoint);

    void executePowerOnOffInputCluster(ThingActionInfo *info, ZigbeeNodeEndpoint *endpoint);
    void executeBrightnessLevelControlInputCluster(ThingActionInfo *info, ZigbeeNodeEndpoint *endpoint);
    void executeColorTemperatureColorControlInputCluster(ThingActionInfo *info, ZigbeeNodeEndpoint *endpoint);
    void executeColorColorControlInputCluster(ThingActionInfo *info, ZigbeeNodeEndpoint *endpoint);
    void executeIdentifyIdentifyInputCluster(ThingActionInfo *info, ZigbeeNodeEndpoint *endpoint);
    void executeImageNotifyOtaOutputCluster(ThingActionInfo *info, ZigbeeNodeEndpoint *endpoint);

    void readColorTemperatureRange(Thing *thing, ZigbeeNodeEndpoint *endpoint);
    quint16 mapScaledValueToColorTemperature(Thing *thing, int scaledColorTemperature);
    int mapColorTemperatureToScaledValue(Thing *thing, quint16 colorTemperature);

    void readAttributesDelayed(ZigbeeCluster *cluster, const QList<quint16> &attributes, quint16 manufacturerCode = 0x0000);
    void writeAttributesDelayed(ZigbeeCluster *cluster, const QList<ZigbeeClusterLibrary::WriteAttributeRecord> &records, quint16 manufacturerCode = 0x0000);

    // To support OTA updates, set the firmware update index url and override the parsing.
    // This base class will take care for fetching, caching and managing.
    void setFirmwareIndexUrl(const QUrl &url);
    virtual QList<FirmwareIndexEntry> firmwareIndexFromJson(const QByteArray &data) const;

    FirmwareIndexEntry checkFirmwareAvailability(const QList<FirmwareIndexEntry> &index, quint16 manufacturerCode, quint16 imageType, quint32 currentFileVersion, const QString &modelName) const;
    void enableFirmwareUpdate(Thing *thing);

private slots:
    virtual void updateFirmwareIndex();

private:
    FirmwareIndexEntry firmwareInfo(quint16 manufacturerId, quint16 imageType, quint32 fileVersion) const;
    QString firmwareFileName(const FirmwareIndexEntry &info) const;
    FetchFirmwareReply *fetchFirmware(const FirmwareIndexEntry &info);
    bool firmwareFileExists(const FirmwareIndexEntry &info) const;
    QByteArray extractImage(const FirmwareIndexEntry &info, const QByteArray &data) const;

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

    struct DelayedAttributeReadRequest {
        ZigbeeCluster *cluster;
        QList<quint16> attributes;
        quint16 manufacturerCode;
    };
    QHash<ZigbeeNode*, QList<DelayedAttributeReadRequest>> m_delayedReadRequests;

    struct DelayedAttributeWriteRequest {
        ZigbeeCluster *cluster;
        QList<ZigbeeClusterLibrary::WriteAttributeRecord> records;
        quint16 manufacturerCode;
    };
    QHash<ZigbeeNode*, QList<DelayedAttributeWriteRequest>> m_delayedWriteRequests;

    // OTA
    QList<Thing*> m_enabledFirmwareUpdates;
    QUrl m_firmwareIndexUrl = QUrl("https://raw.githubusercontent.com/Koenkk/zigbee-OTA/master/index.json");
    QList<FirmwareIndexEntry> m_firmwareIndex;
    QDateTime m_lastFirmwareIndexUpdate;
};

class FetchFirmwareReply: public QObject
{
    Q_OBJECT
public:
    FetchFirmwareReply(QObject *parent): QObject(parent) {
        connect(this, &FetchFirmwareReply::finished, this, &FetchFirmwareReply::deleteLater);
    }
signals:
    void finished();
};

#endif // INTEGRATIONPLUGINZIGBEEEUROTRONIC_H
