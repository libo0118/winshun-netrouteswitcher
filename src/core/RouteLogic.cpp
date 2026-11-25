#include "core/RouteLogic.h"

#include <QRegularExpression>

#include <algorithm>
#include <limits>

namespace nrs {
namespace {

constexpr unsigned int kIfTypeEthernetCsmacd = 6u;
constexpr unsigned int kIfTypePpp = 23u;
constexpr unsigned int kIfTypeSoftwareLoopback = 24u;
constexpr unsigned int kIfTypeFastEthernetT = 62u;
constexpr unsigned int kIfTypeFastEthernetFx = 69u;
constexpr unsigned int kIfTypeIeee80211 = 71u;
constexpr unsigned int kIfTypeGigabitEthernet = 117u;
constexpr unsigned int kIfTypeTunnel = 131u;

QString formatTrimmedDecimal(double value, int maxDecimals)
{
    QString s = QString::number(value, 'f', maxDecimals);
    if (s.contains(QLatin1Char('.'))) {
        while (s.endsWith(QLatin1Char('0'))) {
            s.chop(1);
        }
        if (s.endsWith(QLatin1Char('.'))) {
            s.chop(1);
        }
    }
    return s;
}

} // namespace

bool shouldIgnoreAdapter(unsigned int ifType, const QString& description)
{
    if (ifType == kIfTypeSoftwareLoopback) {
        return true;
    }

    return description.contains(QLatin1String("Kernel Debug"), Qt::CaseInsensitive)
        || description.contains(QLatin1String("WAN Miniport"), Qt::CaseInsensitive)
        || description.contains(QLatin1String("QoS Packet Scheduler"), Qt::CaseInsensitive)
        || description.contains(QLatin1String("Teredo"), Qt::CaseInsensitive)
        || description.contains(QLatin1String("IP-HTTPS"), Qt::CaseInsensitive)
        || description.contains(QLatin1String("6to4"), Qt::CaseInsensitive);
}

AdapterKind detectAdapterKind(
    unsigned int ifType,
    const QString& name,
    const QString& description)
{
    const QString text = name + QLatin1Char(' ') + description;

    if (ifType == kIfTypeIeee80211
        || text.contains(QLatin1String("Wi-Fi"), Qt::CaseInsensitive)
        || text.contains(QLatin1String("WLAN"), Qt::CaseInsensitive)
        || text.contains(QLatin1String("Wireless"), Qt::CaseInsensitive)) {
        return AdapterKind::Wifi;
    }

    if (ifType == kIfTypeTunnel
        || ifType == kIfTypePpp
        || text.contains(QLatin1String("Tunnel"), Qt::CaseInsensitive)
        || text.contains(QLatin1String("Wintun"), Qt::CaseInsensitive)
        || text.contains(QLatin1String("WireGuard"), Qt::CaseInsensitive)
        || text.contains(QLatin1String("Tailscale"), Qt::CaseInsensitive)
        || text.contains(QLatin1String("ZeroTier"), Qt::CaseInsensitive)
        || text.contains(QLatin1String("Virtual"), Qt::CaseInsensitive)
        || name.startsWith(QLatin1String("et_"), Qt::CaseInsensitive)) {
        return AdapterKind::VirtualTunnel;
    }

    if (ifType == kIfTypeEthernetCsmacd
        || ifType == kIfTypeGigabitEthernet
        || ifType == kIfTypeFastEthernetFx
        || ifType == kIfTypeFastEthernetT) {
        return AdapterKind::Ethernet;
    }

    return AdapterKind::Other;
}

AdapterCategory classifyAdapter(const AdapterInfo& info)
{
    if (!info.isUp) {
        return AdapterCategory::Disconnected;
    }

    const bool hasInternet = info.hasIpv4DefaultRoute
        || info.hasIpv6DefaultRoute
        || !info.ipv4Gateways.isEmpty()
        || !info.ipv6Gateways.isEmpty();

    return hasInternet ? AdapterCategory::Internet : AdapterCategory::LanOrVirtual;
}

void sortAdapters(QList<AdapterInfo>& adapters)
{
    auto sortMetric = [](const AdapterInfo& a) -> int {
        if (const auto v4 = a.effectiveV4Metric(); v4.has_value()) {
            return *v4;
        }
        if (const auto v6 = a.effectiveV6Metric(); v6.has_value()) {
            return *v6;
        }
        return a.ipv4InterfaceMetric;
    };

    std::stable_sort(
        adapters.begin(),
        adapters.end(),
        [&](const AdapterInfo& lhs, const AdapterInfo& rhs) {
            const int catL = static_cast<int>(lhs.category);
            const int catR = static_cast<int>(rhs.category);
            if (catL != catR) {
                return catL < catR;
            }
            const int metricL = sortMetric(lhs);
            const int metricR = sortMetric(rhs);
            if (metricL != metricR) {
                return metricL < metricR;
            }
            return lhs.ifIndex < rhs.ifIndex;
        });
}

void resolvePhysicalEgress(RouteSnapshot& snap)
{
    if (!snap.osActiveV4IfIndex.has_value()) {
        snap.osActiveV4IfIndex = snap.activeV4IfIndex;
    }
    if (!snap.osActiveV6IfIndex.has_value()) {
        snap.osActiveV6IfIndex = snap.activeV6IfIndex;
    }

    snap.tunTakeoverV4IfIndex.reset();
    snap.tunTakeoverV6IfIndex.reset();

    auto findBestPhysical = [&](bool isV6) -> const AdapterInfo* {
        const AdapterInfo* bestDefault = nullptr;
        int bestDefaultMetric = std::numeric_limits<int>::max();
        const AdapterInfo* bestGateway = nullptr;
        int bestGatewayMetric = std::numeric_limits<int>::max();

        for (const AdapterInfo& a : snap.adapters) {
            if (!a.isUp || a.kind == AdapterKind::VirtualTunnel) {
                continue;
            }
            const bool hasDef = isV6 ? a.hasIpv6DefaultRoute : a.hasIpv4DefaultRoute;
            const std::optional<int> eff = isV6 ? a.effectiveV6Metric() : a.effectiveV4Metric();
            if (hasDef && eff.has_value()) {
                if (!bestDefault || *eff < bestDefaultMetric
                    || (*eff == bestDefaultMetric && a.ifIndex < bestDefault->ifIndex)) {
                    bestDefault = &a;
                    bestDefaultMetric = *eff;
                }
                continue;
            }
            const bool hasGw = isV6 ? !a.ipv6Gateways.isEmpty() : !a.ipv4Gateways.isEmpty();
            if (hasGw) {
                const int ifMet = isV6 ? a.ipv6InterfaceMetric.value_or(0) : a.ipv4InterfaceMetric;
                if (!bestGateway || ifMet < bestGatewayMetric
                    || (ifMet == bestGatewayMetric && a.ifIndex < bestGateway->ifIndex)) {
                    bestGateway = &a;
                    bestGatewayMetric = ifMet;
                }
            }
        }
        return bestDefault ? bestDefault : bestGateway;
    };

    if (const std::optional<int> osV4 = snap.osActiveV4IfIndex; osV4.has_value()) {
        const AdapterInfo* osV4Adapter = snap.findByIfIndex(*osV4);
        if (osV4Adapter && osV4Adapter->kind == AdapterKind::VirtualTunnel) {
            if (const AdapterInfo* phys = findBestPhysical(false)) {
                snap.tunTakeoverV4IfIndex = osV4;
                snap.activeV4IfIndex = phys->ifIndex;
            } else {
                snap.activeV4IfIndex = osV4;
            }
        } else {
            snap.activeV4IfIndex = osV4;
        }
    }

    if (const std::optional<int> osV6 = snap.osActiveV6IfIndex; osV6.has_value()) {
        const AdapterInfo* osV6Adapter = snap.findByIfIndex(*osV6);
        if (osV6Adapter && osV6Adapter->kind == AdapterKind::VirtualTunnel) {
            if (const AdapterInfo* phys = findBestPhysical(true)) {
                snap.tunTakeoverV6IfIndex = osV6;
                snap.activeV6IfIndex = phys->ifIndex;
            } else {
                snap.activeV6IfIndex = osV6;
            }
        } else {
            snap.activeV6IfIndex = osV6;
        }
    }
}

QList<int> computePriorityMetrics(
    const QList<AdapterInfo>& orderedInternetAdapters,
    bool syncIpv6)
{
    QList<int> metrics;
    if (orderedInternetAdapters.isEmpty()) {
        return metrics;
    }

    metrics.resize(orderedInternetAdapters.size());
    metrics[0] = 10;

    for (qsizetype i = 1; i < orderedInternetAdapters.size(); ++i) {
        const AdapterInfo& curr = orderedInternetAdapters[i];
        long long candidate = static_cast<long long>(metrics[i - 1]) + 20;

        for (qsizetype p = 0; p < i; ++p) {
            const AdapterInfo& prev = orderedInternetAdapters[p];
            const long long prevMetric = metrics[p];

            if (prev.hasIpv4DefaultRoute && curr.hasIpv4DefaultRoute
                && prev.ipv4RouteMetric.has_value() && curr.ipv4RouteMetric.has_value()) {
                const long long neededV4 = static_cast<long long>(*prev.ipv4RouteMetric)
                    + prevMetric + 20 - static_cast<long long>(*curr.ipv4RouteMetric);
                candidate = std::max(candidate, neededV4);
            }

            if (syncIpv6 && prev.hasIpv6DefaultRoute && curr.hasIpv6DefaultRoute
                && prev.ipv6RouteMetric.has_value() && curr.ipv6RouteMetric.has_value()) {
                const long long neededV6 = static_cast<long long>(*prev.ipv6RouteMetric)
                    + prevMetric + 20 - static_cast<long long>(*curr.ipv6RouteMetric);
                candidate = std::max(candidate, neededV6);
            }
        }

        if (candidate % 10 != 0) {
            candidate = ((candidate / 10) + 1) * 10;
        }

        metrics[i] = static_cast<int>(std::clamp<long long>(candidate, 1, 9999));
    }

    return metrics;
}

QString formatLinkSpeed(quint64 speedBps)
{
    if (speedBps == 0 || speedBps == std::numeric_limits<quint64>::max()) {
        return QStringLiteral("-");
    }

    const double mbps = static_cast<double>(speedBps) / 1000000.0;
    if (mbps >= 1000.0) {
        return formatTrimmedDecimal(mbps / 1000.0, 2) + QStringLiteral(" Gbps");
    }
    return formatTrimmedDecimal(mbps, 1) + QStringLiteral(" Mbps");
}

QString kindDisplayText(AdapterKind kind)
{
    switch (kind) {
    case AdapterKind::Wifi:
        return QStringLiteral("Wi-Fi");
    case AdapterKind::Ethernet:
        return QStringLiteral("以太网");
    case AdapterKind::VirtualTunnel:
        return QStringLiteral("虚拟网卡");
    case AdapterKind::Other:
    default:
        return QStringLiteral("其他");
    }
}

QString categoryDisplayText(const AdapterInfo& a)
{
    switch (a.category) {
    case AdapterCategory::Internet:
        return QStringLiteral("互联网出口");
    case AdapterCategory::LanOrVirtual:
        return a.kind == AdapterKind::VirtualTunnel
            ? QStringLiteral("虚拟隧道")
            : QStringLiteral("局域网专用");
    case AdapterCategory::Disconnected:
    default:
        return QStringLiteral("已断开");
    }
}

QString formatNetworkSummary(const AdapterInfo& a)
{
    const QString ip = a.ipv4Addresses.isEmpty()
        ? QStringLiteral("无 IPv4")
        : a.ipv4Addresses.join(QStringLiteral(", "));
    const QString gw = a.ipv4Gateways.isEmpty()
        ? QStringLiteral("无网关")
        : a.ipv4Gateways.join(QStringLiteral(", "));
    const QString dns = a.dnsServers.isEmpty()
        ? QStringLiteral("-")
        : a.dnsServers.mid(0, 2).join(QStringLiteral(", "));

    return QStringLiteral("IP: %1   |   网关: %2   |   速率: %3   |   DNS: %4")
        .arg(ip, gw, formatLinkSpeed(a.speedBps), dns);
}

QString formatMetricSummary(const AdapterInfo& a)
{
    const QString mode = a.ipv4AutoMetric ? QStringLiteral("自动") : QStringLiteral("手动");
    const auto effV4Opt = a.effectiveV4Metric();
    const auto effV6Opt = a.effectiveV6Metric();
    const QString effV4 = effV4Opt.has_value() ? QString::number(*effV4Opt) : QStringLiteral("-");
    const QString effV6 = effV6Opt.has_value() ? QString::number(*effV6Opt) : QStringLiteral("-");

    return QStringLiteral("跃点数 %1 (%2)   ·   有效优先级: IPv4=%3 / IPv6=%4")
        .arg(a.ipv4InterfaceMetric)
        .arg(mode, effV4, effV6);
}

QString formatEgressBadge(const AdapterInfo& a, const RouteSnapshot& snap)
{
    const bool isActiveV4 = snap.activeV4IfIndex.has_value() && *snap.activeV4IfIndex == a.ifIndex;
    const bool isActiveV6 = snap.activeV6IfIndex.has_value() && *snap.activeV6IfIndex == a.ifIndex;

    if (isActiveV4 && isActiveV6) {
        return QStringLiteral("当前 IPv4 + IPv6 出口");
    }
    if (isActiveV4) {
        return QStringLiteral("当前 IPv4 出口");
    }
    if (isActiveV6) {
        return QStringLiteral("仅 IPv6 出口");
    }
    if (snap.isTunTakeoverAdapter(a.ifIndex)) {
        return QStringLiteral("TUN 流量接管中");
    }
    if (a.category == AdapterCategory::LanOrVirtual) {
        return QStringLiteral("无默认网关 · 不抢外网");
    }
    return {};
}

QString formatActiveV4Headline(const RouteSnapshot& snap)
{
    const AdapterInfo* v4 = snap.activeV4Adapter();
    if (!v4) {
        return QStringLiteral("无可用 IPv4 互联网出口");
    }
    const QString gw = v4->ipv4Gateways.isEmpty()
        ? QStringLiteral("无网关")
        : v4->ipv4Gateways.first();
    return QStringLiteral("%1  (%2 · 跃点 %3)")
        .arg(v4->name, gw)
        .arg(v4->ipv4InterfaceMetric);
}

QString formatActiveV6Headline(const RouteSnapshot& snap)
{
    const AdapterInfo* v6 = snap.activeV6Adapter();
    if (!v6) {
        return QStringLiteral("未启用 IPv6 公网路由");
    }
    return QStringLiteral("%1  (跃点 %2)")
        .arg(v6->name)
        .arg(v6->ipv6InterfaceMetric.value_or(0));
}

QString formatSplitEgressWarning(const RouteSnapshot& snap)
{
    if (!snap.hasSplitEgress()) {
        return {};
    }
    const AdapterInfo* v4 = snap.activeV4Adapter();
    const AdapterInfo* v6 = snap.activeV6Adapter();
    const QString v4Name = v4 ? v4->name : QStringLiteral("#%1").arg(*snap.activeV4IfIndex);
    const QString v6Name = v6 ? v6->name : QStringLiteral("#%1").arg(*snap.activeV6IfIndex);
    return QStringLiteral("检测到 IPv4 (%1) 与 IPv6 (%2) 出口不一致，切换首选网卡并勾选“同步 IPv6 优先级”即可统一。")
        .arg(v4Name, v6Name);
}

QString formatTunTakeoverBadge(const RouteSnapshot& snap)
{
    const AdapterInfo* tun = snap.tunTakeoverAdapter();
    if (!tun) {
        return {};
    }
    return QStringLiteral("TUN 接管: %1").arg(tun->name);
}

bool matchesFilter(const AdapterInfo& a, const QString& keyword)
{
    const QString trimmed = keyword.trimmed();
    if (trimmed.isEmpty()) {
        return true;
    }

    static const QRegularExpression kWhitespace(QStringLiteral("\\s+"));
    const QStringList tokens = trimmed.split(kWhitespace, Qt::SkipEmptyParts);
    const QString ifIndexText = QString::number(a.ifIndex);
    const QString kindText = kindDisplayText(a.kind);
    const QString catText = categoryDisplayText(a);

    auto matchesToken = [&](const QString& token) -> bool {
        if (a.name.contains(token, Qt::CaseInsensitive)
            || a.description.contains(token, Qt::CaseInsensitive)
            || ifIndexText.contains(token, Qt::CaseInsensitive)
            || kindText.contains(token, Qt::CaseInsensitive)
            || catText.contains(token, Qt::CaseInsensitive)) {
            return true;
        }
        auto listHas = [&](const QStringList& list) {
            for (const QString& entry : list) {
                if (entry.contains(token, Qt::CaseInsensitive)) {
                    return true;
                }
            }
            return false;
        };
        return listHas(a.ipv4Addresses)
            || listHas(a.ipv4Gateways)
            || listHas(a.ipv6Gateways)
            || listHas(a.dnsServers);
    };

    for (const QString& token : tokens) {
        if (!matchesToken(token)) {
            return false;
        }
    }
    return true;
}

} // namespace nrs
