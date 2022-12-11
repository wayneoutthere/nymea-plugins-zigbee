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

#ifndef DPVALUE_H
#define DPVALUE_H

#include <QVariant>

// This is a Tuya Data Point value as used in Tuya cloud. Each DpValue is basically a state on a tuya device.
// Some Tuya ZigBee devices just send out those values to the tuya gateway so the gateway can forward them straight
// to the Tuya cloud via MQTT without understanding what it means (as opposed to following the ZigBee spec where the gateway would
// need to implement all the clusters).

// In order to understand what a particular data point means, one can go to the tuya cloud, register a developer account
// and start creating a new whitelabel device by picking the device of interest as base device. Then all the supported
// DpValues of the device can be seen in the configuration.

class DpValue
{
    Q_GADGET
public:
    enum Type {
        TypeRaw = 0,
        TypeBool = 1,
        TypeUInt32 = 2,
        TypeString = 3,
        TypeEnum = 4, // 8 bit
        TypeFlags = 5, // may be 8, 16 or 32 bits
    };
    Q_ENUM(Type)

    DpValue();
    DpValue(quint8 dp, Type type, const QVariant &value, quint8 length = 0, quint16 sequence = 0);

    quint16 sequence() const;
    quint8 dp() const;
    Type type() const;
    QVariant value() const;
    quint16 length() const;

    static DpValue fromData(const QByteArray &data);
    QByteArray toData() const;

private:
    quint16 m_sequence = 0;
    quint8 m_dp = 0;
    Type m_type = TypeRaw;
    QVariant m_value;
    quint16 m_length = 0;
};

QDebug operator<<(QDebug dbg, const DpValue &value);

#endif // DPVALUE_H
