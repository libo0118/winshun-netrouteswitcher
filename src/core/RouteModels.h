#pragma once

#include <QJsonObject>
#include <QList>
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

struct PluginSettings {
    bool syncIpv6 = true;
    bool showDisconnected = false;

    static PluginSettings fromJson(const QJsonObject& obj)
    {
        PluginSettings s;
        if (obj.contains(QStringLiteral("syncIpv6"))) {
            s.syncIpv6 = obj.value(QStringLiteral("syncIpv6")).toBool(true);
        }
        if (obj.contains(QStringLiteral("showDisconnected"))) {
            s.showDisconnected = obj.value(QStringLiteral("showDisconnected")).toBool(false);
        }
        return s;
    }

    QJsonObject toJson() const
    {
        return QJsonObject{
            {QStringLiteral("syncIpv6"), syncIpv6},
            {QStringLiteral("showDisconnected"), showDisconnected},
        };
    }
};

} // namespace nrs
