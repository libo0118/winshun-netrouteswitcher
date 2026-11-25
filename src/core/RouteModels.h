#pragma once

#include <QJsonArray>
#include <QJsonObject>
#include <QJsonValue>
#include <QList>
#include <QSet>
#include <QString>
#include <QStringList>
#include <QtGlobal>

#include <optional>

namespace nrs {

enum class AdapterKind {
    Ethernet = 0,
    Wifi = 1,
    VirtualTunnel = 2,
    Other = 3,
};

enum class AdapterCategory {
    Internet = 0,
    LanOrVirtual = 1,
    Disconnected = 2,
};

struct AdapterInfo {
    int ifIndex = 0;
    QString name;
    QString description;
    unsigned int ifType = 0;
    bool isUp = false;
    quint64 speedBps = 0;

    QStringList ipv4Addresses;
    QStringList ipv4Gateways;
    QStringList ipv6Gateways;
    QStringList dnsServers;

    bool hasIpv4DefaultRoute = false;
    std::optional<int> ipv4RouteMetric;
    bool hasIpv6DefaultRoute = false;
    std::optional<int> ipv6RouteMetric;

    int ipv4InterfaceMetric = 0;
    bool ipv4AutoMetric = true;
    std::optional<int> ipv6InterfaceMetric;
    bool ipv6AutoMetric = true;

    AdapterKind kind = AdapterKind::Other;
    AdapterCategory category = AdapterCategory::Disconnected;

    std::optional<int> effectiveV4Metric() const
    {
        if (hasIpv4DefaultRoute && ipv4RouteMetric.has_value()) {
            return *ipv4RouteMetric + ipv4InterfaceMetric;
        }
        return std::nullopt;
    }

    std::optional<int> effectiveV6Metric() const
    {
        if (hasIpv6DefaultRoute && ipv6RouteMetric.has_value()) {
            return *ipv6RouteMetric + ipv6InterfaceMetric.value_or(0);
        }
        return std::nullopt;
    }
};

struct RouteSnapshot {
    QList<AdapterInfo> adapters;
    std::optional<int> activeV4IfIndex;
    std::optional<int> activeV6IfIndex;
    std::optional<int> osActiveV4IfIndex;
    std::optional<int> osActiveV6IfIndex;
    std::optional<int> tunTakeoverV4IfIndex;
    std::optional<int> tunTakeoverV6IfIndex;
    QString capturedAtIso;
    QString connectedWifiSsid;

    const AdapterInfo* findByIfIndex(int ifIndex) const
    {
        for (const AdapterInfo& adapter : adapters) {
            if (adapter.ifIndex == ifIndex) {
                return &adapter;
            }
        }
        return nullptr;
    }

    const AdapterInfo* activeV4Adapter() const
    {
        return activeV4IfIndex.has_value() ? findByIfIndex(*activeV4IfIndex) : nullptr;
    }

    const AdapterInfo* activeV6Adapter() const
    {
        return activeV6IfIndex.has_value() ? findByIfIndex(*activeV6IfIndex) : nullptr;
    }

    const AdapterInfo* tunTakeoverAdapter() const
    {
        if (tunTakeoverV4IfIndex.has_value()) {
            return findByIfIndex(*tunTakeoverV4IfIndex);
        }
        if (tunTakeoverV6IfIndex.has_value()) {
            return findByIfIndex(*tunTakeoverV6IfIndex);
        }
        return nullptr;
    }

    bool hasTunTakeover() const
    {
        return tunTakeoverV4IfIndex.has_value() || tunTakeoverV6IfIndex.has_value();
    }

    bool isTunTakeoverAdapter(int ifIndex) const
    {
        return (tunTakeoverV4IfIndex.has_value() && *tunTakeoverV4IfIndex == ifIndex)
            || (tunTakeoverV6IfIndex.has_value() && *tunTakeoverV6IfIndex == ifIndex);
    }

    bool hasSplitEgress() const
    {
        return activeV4IfIndex.has_value()
            && activeV6IfIndex.has_value()
            && *activeV4IfIndex != *activeV6IfIndex;
    }
};

struct OperationResult {
    bool ok = false;
    QString message;
    int win32Error = 0;
};

struct WifiRouteRule {
    QString id;
    QString ssid;
    QStringList adapterOrder;
    bool enabled = true;

    bool operator==(const WifiRouteRule&) const = default;

    static WifiRouteRule fromJson(const QJsonObject& obj)
    {
        WifiRouteRule r;
        r.id = obj.value(QStringLiteral("id")).toString();
        r.ssid = obj.value(QStringLiteral("ssid")).toString();
        if (obj.contains(QStringLiteral("enabled"))) {
            r.enabled = obj.value(QStringLiteral("enabled")).toBool(true);
        }

        const QJsonArray arr = obj.value(QStringLiteral("adapterOrder")).toArray();
        QSet<QString> seen;
        r.adapterOrder.reserve(arr.size());
        for (const QJsonValue& val : arr) {
            const QString name = val.toString().trimmed();
            if (name.isEmpty()) {
                continue;
            }
            const QString key = name.toLower();
            if (seen.contains(key)) {
                continue;
            }
            seen.insert(key);
            r.adapterOrder.append(name);
        }
        return r;
    }

    QJsonObject toJson() const
    {
        QJsonArray arr;
        QSet<QString> seen;
        for (const QString& raw : adapterOrder) {
            const QString name = raw.trimmed();
            if (name.isEmpty()) {
                continue;
            }
            const QString key = name.toLower();
            if (seen.contains(key)) {
                continue;
            }
            seen.insert(key);
            arr.append(name);
        }
        return QJsonObject{
            {QStringLiteral("id"), id},
            {QStringLiteral("ssid"), ssid},
            {QStringLiteral("adapterOrder"), arr},
            {QStringLiteral("enabled"), enabled},
        };
    }
};

struct PluginSettings {
    bool syncIpv6 = true;
    bool showDisconnected = false;
    bool autoSwitchByWifi = true;

    static PluginSettings fromJson(const QJsonObject& obj)
    {
        PluginSettings s;
        if (obj.contains(QStringLiteral("syncIpv6"))) {
            s.syncIpv6 = obj.value(QStringLiteral("syncIpv6")).toBool(true);
        }
        if (obj.contains(QStringLiteral("showDisconnected"))) {
            s.showDisconnected = obj.value(QStringLiteral("showDisconnected")).toBool(false);
        }
        if (obj.contains(QStringLiteral("autoSwitchByWifi"))) {
            s.autoSwitchByWifi = obj.value(QStringLiteral("autoSwitchByWifi")).toBool(true);
        }
        return s;
    }

    QJsonObject toJson() const
    {
        return QJsonObject{
            {QStringLiteral("syncIpv6"), syncIpv6},
            {QStringLiteral("showDisconnected"), showDisconnected},
            {QStringLiteral("autoSwitchByWifi"), autoSwitchByWifi},
        };
    }
};

} // namespace nrs
