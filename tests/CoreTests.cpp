#include "core/RouteLogic.h"
#include "core/RouteModels.h"
#include "core/ThemeData.h"

#include <QColor>
#include <QJsonArray>
#include <QJsonObject>
#include <QList>
#include <QString>

#include <iostream>
#include <limits>
#include <string>

namespace {

int g_failures = 0;
int g_passedGroups = 0;

void failAt(const char* expr, const char* file, int line, const QString& detail = {})
{
    ++g_failures;
    std::cerr << "[FAIL] " << file << ":" << line << " (" << expr << ")";
    if (!detail.isEmpty()) {
        std::cerr << " - " << detail.toStdString();
    }
    std::cerr << "\n";
}

#define NRS_CHECK(cond)                                                                            \
    do {                                                                                           \
        if (!(cond)) {                                                                             \
            failAt(#cond, __FILE__, __LINE__);                                                     \
            return;                                                                                \
        }                                                                                          \
    } while (false)

#define NRS_CHECK_EQ(actual, expected)                                                             \
    do {                                                                                           \
        const auto& _act = (actual);                                                               \
        const auto& _exp = (expected);                                                             \
        if (!(_act == _exp)) {                                                                     \
            failAt(#actual " == " #expected, __FILE__, __LINE__);                                  \
            return;                                                                                \
        }                                                                                          \
    } while (false)

nrs::AdapterInfo makeInternetAdapter(
    int ifIndex,
    const QString& name,
    int v4RouteMetric,
    std::optional<int> v6RouteMetric = std::nullopt)
{
    nrs::AdapterInfo a;
    a.ifIndex = ifIndex;
    a.name = name;
    a.isUp = true;
    a.hasIpv4DefaultRoute = true;
    a.ipv4RouteMetric = v4RouteMetric;
    if (v6RouteMetric.has_value()) {
        a.hasIpv6DefaultRoute = true;
        a.ipv6RouteMetric = *v6RouteMetric;
    }
    a.category = nrs::AdapterCategory::Internet;
    return a;
}

void testComputePriorityMetricsBasicAndRounding()
{
    // Empty & single adapter
    NRS_CHECK(nrs::computePriorityMetrics({}, true).isEmpty());

    const auto single = nrs::computePriorityMetrics({makeInternetAdapter(1, QStringLiteral("WLAN"), 0)}, true);
    NRS_CHECK_EQ(single, (QList<int>{10}));

    // Identical route metrics -> 10, 30, 50
    const QList<nrs::AdapterInfo> equalRoutes = {
        makeInternetAdapter(1, QStringLiteral("WLAN"), 0),
        makeInternetAdapter(2, QStringLiteral("以太网"), 0),
        makeInternetAdapter(3, QStringLiteral("USB"), 0),
    };
    NRS_CHECK_EQ(nrs::computePriorityMetrics(equalRoutes, true), (QList<int>{10, 30, 50}));

    // Route metric offset + ceiling to multiple of 10:
    // prev: route=256, ifMetric=10 => eff=266. curr: route=0 => needed = 266 + 20 - 0 = 286 -> 290
    const QList<nrs::AdapterInfo> offsetRoutes = {
        makeInternetAdapter(1, QStringLiteral("A"), 256),
        makeInternetAdapter(2, QStringLiteral("B"), 0),
    };
    NRS_CHECK_EQ(nrs::computePriorityMetrics(offsetRoutes, false), (QList<int>{10, 290}));

    // Exact multiple of 10 stays unchanged:
    // prev: route=250, ifMetric=10 => eff=260. curr: route=0 => needed = 280 -> 280
    const QList<nrs::AdapterInfo> exactMultiple = {
        makeInternetAdapter(1, QStringLiteral("A"), 250),
        makeInternetAdapter(2, QStringLiteral("B"), 0),
    };
    NRS_CHECK_EQ(nrs::computePriorityMetrics(exactMultiple, false), (QList<int>{10, 280}));
    ++g_passedGroups;
}

void testComputePriorityMetricsIpv6AndClamp()
{
    // IPv6 sync toggle:
    // A: v4Route=0, v6Route=300; B: v4Route=0, v6Route=10
    // syncIpv6=false -> [10, 30]
    // syncIpv6=true  -> neededV6 = 300 + 10 + 20 - 10 = 320 -> [10, 320]
    const QList<nrs::AdapterInfo> dualStack = {
        makeInternetAdapter(10, QStringLiteral("WLAN"), 0, 300),
        makeInternetAdapter(11, QStringLiteral("以太网"), 0, 10),
    };
    NRS_CHECK_EQ(nrs::computePriorityMetrics(dualStack, false), (QList<int>{10, 30}));
    NRS_CHECK_EQ(nrs::computePriorityMetrics(dualStack, true), (QList<int>{10, 320}));

    // Earlier ancestor p=0 imposes stricter constraint than immediate predecessor p=1:
    // 0: v4Route=200 (ifMetric=10, eff=210)
    // 1: v4Route=200 (ifMetric=30, eff=230)
    // 2: v4Route=0   -> needed from p=1: 200+30+20-0 = 250 -> 250
    // Also test where p=0 has v6Route=400, p=1 has no v6, p=2 has v6Route=0:
    // needed from p=0 (v6): 400 + 10 + 20 - 0 = 430 -> 430
    const QList<nrs::AdapterInfo> multiPrev = {
        makeInternetAdapter(1, QStringLiteral("A"), 0, 400),
        makeInternetAdapter(2, QStringLiteral("B"), 0, std::nullopt),
        makeInternetAdapter(3, QStringLiteral("C"), 0, 0),
    };
    NRS_CHECK_EQ(nrs::computePriorityMetrics(multiPrev, true), (QList<int>{10, 30, 430}));
    NRS_CHECK_EQ(nrs::computePriorityMetrics(multiPrev, false), (QList<int>{10, 30, 50}));

    // Clamp to [1, 9999]
    const QList<nrs::AdapterInfo> hugeOffset = {
        makeInternetAdapter(1, QStringLiteral("A"), 10000),
        makeInternetAdapter(2, QStringLiteral("B"), 0),
    };
    NRS_CHECK_EQ(nrs::computePriorityMetrics(hugeOffset, false), (QList<int>{10, 9999}));
    ++g_passedGroups;
}

void testFilteringAndKindDetection()
{
    // Loopback & ignored virtual/system descriptions
    NRS_CHECK(nrs::shouldIgnoreAdapter(24u, QStringLiteral("Software Loopback Interface 1")));
    NRS_CHECK(nrs::shouldIgnoreAdapter(6u, QStringLiteral("Microsoft Kernel Debug Network Adapter")));
    NRS_CHECK(nrs::shouldIgnoreAdapter(6u, QStringLiteral("WAN Miniport (IPv6)")));
    NRS_CHECK(nrs::shouldIgnoreAdapter(6u, QStringLiteral("QoS Packet Scheduler-0001")));
    NRS_CHECK(nrs::shouldIgnoreAdapter(131u, QStringLiteral("Microsoft Teredo Tunneling Adapter")));
    NRS_CHECK(nrs::shouldIgnoreAdapter(131u, QStringLiteral("Microsoft IP-HTTPS Platform Interface")));
    NRS_CHECK(nrs::shouldIgnoreAdapter(131u, QStringLiteral("Microsoft 6to4 Adapter")));

    NRS_CHECK(!nrs::shouldIgnoreAdapter(6u, QStringLiteral("Intel(R) Ethernet Controller I225-V")));
    NRS_CHECK(!nrs::shouldIgnoreAdapter(71u, QStringLiteral("Intel(R) Wi-Fi 6E AX211 160MHz")));
    NRS_CHECK(!nrs::shouldIgnoreAdapter(53u, QStringLiteral("Wintun Userspace Tunnel")));

    // Kind detection
    NRS_CHECK_EQ(nrs::detectAdapterKind(71u, QStringLiteral("WLAN"), QStringLiteral("AX211")), nrs::AdapterKind::Wifi);
    NRS_CHECK_EQ(nrs::detectAdapterKind(6u, QStringLiteral("USB"), QStringLiteral("Wireless Dongle")), nrs::AdapterKind::Wifi);
    NRS_CHECK_EQ(nrs::detectAdapterKind(131u, QStringLiteral("tun0"), QStringLiteral("Generic")), nrs::AdapterKind::VirtualTunnel);
    NRS_CHECK_EQ(nrs::detectAdapterKind(23u, QStringLiteral("VPN"), QStringLiteral("Dial-up")), nrs::AdapterKind::VirtualTunnel);
    NRS_CHECK_EQ(nrs::detectAdapterKind(53u, QStringLiteral("wg0"), QStringLiteral("WireGuard Tunnel")), nrs::AdapterKind::VirtualTunnel);
    NRS_CHECK_EQ(nrs::detectAdapterKind(53u, QStringLiteral("ts0"), QStringLiteral("Tailscale Tunnel")), nrs::AdapterKind::VirtualTunnel);
    NRS_CHECK_EQ(nrs::detectAdapterKind(6u, QStringLiteral("zt0"), QStringLiteral("ZeroTier Virtual Port")), nrs::AdapterKind::VirtualTunnel);
    NRS_CHECK_EQ(nrs::detectAdapterKind(6u, QStringLiteral("vEth"), QStringLiteral("Hyper-V Virtual Ethernet Adapter")), nrs::AdapterKind::VirtualTunnel);
    NRS_CHECK_EQ(nrs::detectAdapterKind(6u, QStringLiteral("以太网"), QStringLiteral("Realtek PCIe 2.5GbE Family Controller")), nrs::AdapterKind::Ethernet);
    NRS_CHECK_EQ(nrs::detectAdapterKind(999u, QStringLiteral("Custom"), QStringLiteral("Unknown Device")), nrs::AdapterKind::Other);
    ++g_passedGroups;
}

void testEasyTierAndBettboxScenarios()
{
    // Scenario 1: EasyTier (et_ prefix) virtual LAN tunnel without default gateway
    nrs::AdapterInfo et;
    et.ifIndex = 42;
    et.name = QStringLiteral("et_office");
    et.description = QStringLiteral("EasyTier Adapter");
    et.ifType = 6u;
    et.isUp = true;
    et.ipv4Addresses = {QStringLiteral("10.144.144.8/24")};
    et.ipv4InterfaceMetric = 5;
    et.kind = nrs::detectAdapterKind(et.ifType, et.name, et.description);
    et.category = nrs::classifyAdapter(et);

    NRS_CHECK_EQ(et.kind, nrs::AdapterKind::VirtualTunnel);
    NRS_CHECK_EQ(et.category, nrs::AdapterCategory::LanOrVirtual);
    NRS_CHECK_EQ(nrs::categoryDisplayText(et), QStringLiteral("虚拟隧道"));

    // Scenario 2: Bettbox proxy TUN adapter with Wintun description and 0.0.0.0/0 default route (no gateway IP)
    nrs::AdapterInfo bettbox;
    bettbox.ifIndex = 55;
    bettbox.name = QStringLiteral("Bettbox");
    bettbox.description = QStringLiteral("Wintun Userspace Tunnel");
    bettbox.ifType = 53u;
    bettbox.isUp = true;
    bettbox.speedBps = 100000000000ULL;
    bettbox.ipv4Addresses = {QStringLiteral("172.19.0.1/30")};
    bettbox.hasIpv4DefaultRoute = true;
    bettbox.ipv4RouteMetric = 0;
    bettbox.ipv4InterfaceMetric = 5;
    bettbox.ipv4AutoMetric = false;
    bettbox.kind = nrs::detectAdapterKind(bettbox.ifType, bettbox.name, bettbox.description);
    bettbox.category = nrs::classifyAdapter(bettbox);

    NRS_CHECK_EQ(bettbox.kind, nrs::AdapterKind::VirtualTunnel);
    NRS_CHECK_EQ(bettbox.category, nrs::AdapterCategory::Internet);
    NRS_CHECK(bettbox.effectiveV4Metric().has_value() && *bettbox.effectiveV4Metric() == 5);

    // Physical Wi-Fi & disconnected Ethernet
    nrs::AdapterInfo wifi = makeInternetAdapter(12, QStringLiteral("WLAN"), 0);
    wifi.ifType = 71u;
    wifi.description = QStringLiteral("Intel Wi-Fi 6E");
    wifi.ipv4Gateways = {QStringLiteral("192.168.1.1")};
    wifi.ipv4InterfaceMetric = 35;
    wifi.kind = nrs::detectAdapterKind(wifi.ifType, wifi.name, wifi.description);
    wifi.category = nrs::classifyAdapter(wifi);

    nrs::AdapterInfo disc;
    disc.ifIndex = 3;
    disc.name = QStringLiteral("以太网 2");
    disc.ifType = 6u;
    disc.isUp = false;
    disc.kind = nrs::detectAdapterKind(disc.ifType, disc.name, disc.description);
    disc.category = nrs::classifyAdapter(disc);
    NRS_CHECK_EQ(disc.category, nrs::AdapterCategory::Disconnected);

    QList<nrs::AdapterInfo> list = {disc, et, wifi, bettbox};
    nrs::sortAdapters(list);
    NRS_CHECK_EQ(list[0].ifIndex, 55); // Internet, eff=5 (Bettbox)
    NRS_CHECK_EQ(list[1].ifIndex, 12); // Internet, eff=35 (WLAN)
    NRS_CHECK_EQ(list[2].ifIndex, 42); // LanOrVirtual (et_office)
    NRS_CHECK_EQ(list[3].ifIndex, 3);  // Disconnected

    nrs::RouteSnapshot snap;
    snap.adapters = list;
    snap.activeV4IfIndex = 55;
    NRS_CHECK_EQ(nrs::formatEgressBadge(et, snap), QStringLiteral("无默认网关 · 不抢外网"));
    NRS_CHECK_EQ(nrs::formatEgressBadge(bettbox, snap), QStringLiteral("当前 IPv4 出口"));
    ++g_passedGroups;
}

void testSpeedAndSummaryFormatting()
{
    NRS_CHECK_EQ(nrs::formatLinkSpeed(0), QStringLiteral("-"));
    NRS_CHECK_EQ(nrs::formatLinkSpeed(std::numeric_limits<quint64>::max()), QStringLiteral("-"));
    NRS_CHECK_EQ(nrs::formatLinkSpeed(100000000ULL), QStringLiteral("100 Mbps"));
    NRS_CHECK_EQ(nrs::formatLinkSpeed(866700000ULL), QStringLiteral("866.7 Mbps"));
    NRS_CHECK_EQ(nrs::formatLinkSpeed(1000000000ULL), QStringLiteral("1 Gbps"));
    NRS_CHECK_EQ(nrs::formatLinkSpeed(2500000000ULL), QStringLiteral("2.5 Gbps"));
    NRS_CHECK_EQ(nrs::formatLinkSpeed(1250000000ULL), QStringLiteral("1.25 Gbps"));

    nrs::AdapterInfo a = makeInternetAdapter(7, QStringLiteral("以太网"), 0, 256);
    a.kind = nrs::AdapterKind::Ethernet;
    a.speedBps = 2500000000ULL;
    a.ipv4Addresses = {QStringLiteral("192.168.10.20/24")};
    a.ipv4Gateways = {QStringLiteral("192.168.10.1")};
    a.dnsServers = {QStringLiteral("223.5.5.5"), QStringLiteral("119.29.29.29"), QStringLiteral("8.8.8.8")};
    a.ipv4InterfaceMetric = 10;
    a.ipv4AutoMetric = false;
    a.ipv6InterfaceMetric = 10;

    const QString netSummary = nrs::formatNetworkSummary(a);
    NRS_CHECK(netSummary.contains(QStringLiteral("192.168.10.20/24")));
    NRS_CHECK(netSummary.contains(QStringLiteral("192.168.10.1")));
    NRS_CHECK(netSummary.contains(QStringLiteral("2.5 Gbps")));
    NRS_CHECK(netSummary.contains(QStringLiteral("223.5.5.5, 119.29.29.29")));
    NRS_CHECK(!netSummary.contains(QStringLiteral("8.8.8.8")));

    const QString metricSummary = nrs::formatMetricSummary(a);
    NRS_CHECK(metricSummary.contains(QStringLiteral("跃点数 10 (手动)")));
    NRS_CHECK(metricSummary.contains(QStringLiteral("IPv4=10 / IPv6=266")));

    NRS_CHECK(nrs::matchesFilter(a, QStringLiteral("")));
    NRS_CHECK(nrs::matchesFilter(a, QStringLiteral("以太网 192.168.10")));
    NRS_CHECK(!nrs::matchesFilter(a, QStringLiteral("Wi-Fi")));

    const nrs::PluginSettings orig{false, true};
    const nrs::PluginSettings restored = nrs::PluginSettings::fromJson(orig.toJson());
    NRS_CHECK_EQ(restored.syncIpv6, false);
    NRS_CHECK_EQ(restored.showDisconnected, true);
    ++g_passedGroups;
}

void testActiveEgressAndSplitWarning()
{
    nrs::AdapterInfo wlan = makeInternetAdapter(10, QStringLiteral("WLAN"), 0, 0);
    wlan.ipv4Gateways = {QStringLiteral("192.168.1.1")};
    wlan.ipv4InterfaceMetric = 10;
    wlan.ipv6InterfaceMetric = 10;

    nrs::AdapterInfo eth = makeInternetAdapter(20, QStringLiteral("以太网"), 0, 0);
    eth.ipv4Gateways = {QStringLiteral("10.0.0.1")};
    eth.ipv4InterfaceMetric = 30;
    eth.ipv6InterfaceMetric = 5;

    // Unified v4 + v6 egress
    nrs::RouteSnapshot unified;
    unified.adapters = {wlan, eth};
    unified.activeV4IfIndex = 10;
    unified.activeV6IfIndex = 10;
    NRS_CHECK(!unified.hasSplitEgress());
    NRS_CHECK(nrs::formatSplitEgressWarning(unified).isEmpty());
    NRS_CHECK_EQ(nrs::formatEgressBadge(wlan, unified), QStringLiteral("当前 IPv4 + IPv6 出口"));
    NRS_CHECK_EQ(nrs::formatActiveV4Headline(unified), QStringLiteral("WLAN  (192.168.1.1 · 跃点 10)"));
    NRS_CHECK_EQ(nrs::formatActiveV6Headline(unified), QStringLiteral("WLAN  (跃点 10)"));

    // Split v4 != v6 egress
    nrs::RouteSnapshot split;
    split.adapters = {wlan, eth};
    split.activeV4IfIndex = 10;
    split.activeV6IfIndex = 20;
    NRS_CHECK(split.hasSplitEgress());
    const QString warning = nrs::formatSplitEgressWarning(split);
    NRS_CHECK(warning.contains(QStringLiteral("IPv4 (WLAN)")));
    NRS_CHECK(warning.contains(QStringLiteral("IPv6 (以太网)")));
    NRS_CHECK(warning.contains(QStringLiteral("同步 IPv6 优先级")));
    NRS_CHECK_EQ(nrs::formatEgressBadge(wlan, split), QStringLiteral("当前 IPv4 出口"));
    NRS_CHECK_EQ(nrs::formatEgressBadge(eth, split), QStringLiteral("仅 IPv6 出口"));

    // Empty snapshot
    nrs::RouteSnapshot empty;
    NRS_CHECK_EQ(nrs::formatActiveV4Headline(empty), QStringLiteral("无可用 IPv4 互联网出口"));
    NRS_CHECK_EQ(nrs::formatActiveV6Headline(empty), QStringLiteral("未启用 IPv6 公网路由"));
    ++g_passedGroups;
}

void testTunTakeoverResolvesPhysicalEgress()
{
    nrs::AdapterInfo wlan = makeInternetAdapter(17, QStringLiteral("WLAN"), 10, 16);
    wlan.kind = nrs::AdapterKind::Wifi;
    wlan.ipv4Gateways = {QStringLiteral("192.168.43.1")};
    wlan.ipv4InterfaceMetric = 10;
    wlan.ipv6InterfaceMetric = 10;

    nrs::AdapterInfo eth2 = makeInternetAdapter(16, QStringLiteral("以太网 2"), 30, 256);
    eth2.kind = nrs::AdapterKind::Ethernet;
    eth2.ipv4Gateways = {QStringLiteral("192.168.8.1")};
    eth2.ipv4InterfaceMetric = 30;
    eth2.ipv6InterfaceMetric = 30;

    nrs::AdapterInfo bettbox;
    bettbox.ifIndex = 19;
    bettbox.name = QStringLiteral("Bettbox");
    bettbox.description = QStringLiteral("Bettbox Tunnel");
    bettbox.ifType = 53u;
    bettbox.isUp = true;
    bettbox.kind = nrs::AdapterKind::VirtualTunnel;
    bettbox.category = nrs::AdapterCategory::LanOrVirtual;

    // Case 1: Bettbox TUN takes over both IPv4 & IPv6; lowest physical default route is WLAN (eff 20 / 26)
    nrs::RouteSnapshot snap1;
    snap1.adapters = {wlan, eth2, bettbox};
    snap1.osActiveV4IfIndex = 19;
    snap1.osActiveV6IfIndex = 19;
    nrs::resolvePhysicalEgress(snap1);

    NRS_CHECK(snap1.hasTunTakeover());
    NRS_CHECK(snap1.activeV4IfIndex == 17);
    NRS_CHECK(snap1.activeV6IfIndex == 17);
    NRS_CHECK(snap1.tunTakeoverV4IfIndex == 19);
    NRS_CHECK(snap1.tunTakeoverV6IfIndex == 19);
    NRS_CHECK_EQ(nrs::formatTunTakeoverBadge(snap1), QStringLiteral("TUN 接管: Bettbox"));
    NRS_CHECK_EQ(nrs::formatEgressBadge(wlan, snap1), QStringLiteral("当前 IPv4 + IPv6 出口"));
    NRS_CHECK_EQ(nrs::formatEgressBadge(bettbox, snap1), QStringLiteral("TUN 流量接管中"));

    // Case 2: Switch primary metric to 以太网 2 (metric 5 -> eff 35 < WLAN metric 30 -> eff 40)
    wlan.ipv4InterfaceMetric = 30;
    eth2.ipv4InterfaceMetric = 5;
    nrs::RouteSnapshot snap2;
    snap2.adapters = {wlan, eth2, bettbox};
    snap2.osActiveV4IfIndex = 19;
    snap2.osActiveV6IfIndex = 19;
    nrs::resolvePhysicalEgress(snap2);
    NRS_CHECK(snap2.activeV4IfIndex == 16);
    NRS_CHECK(snap2.activeV6IfIndex == 17);
    NRS_CHECK(snap2.hasSplitEgress());

    // Case 3: Pure VPN mode without any physical adapter -> keep OS virtual egress
    nrs::RouteSnapshot vpnOnly;
    vpnOnly.adapters = {bettbox};
    vpnOnly.osActiveV4IfIndex = 19;
    nrs::resolvePhysicalEgress(vpnOnly);
    NRS_CHECK(!vpnOnly.hasTunTakeover());
    NRS_CHECK(vpnOnly.activeV4IfIndex == 19);
    ++g_passedGroups;
}

void testThemeDataFromAppearance()
{
    // Dark theme defaults
    const nrs::ThemeData dark = nrs::ThemeData::fromAppearance(
        QStringLiteral("zh"),
        QStringLiteral("dark"),
        QJsonObject{});
    NRS_CHECK(dark.dark);
    NRS_CHECK(dark.rounded);
    NRS_CHECK(dark.cardRadius == 10.0);
    NRS_CHECK(QColor(dark.page).alpha() == 255);
    NRS_CHECK(QColor(dark.background).lightness() < 80);

    // Light theme + square corners
    const nrs::ThemeData lightSquare = nrs::ThemeData::fromAppearance(
        QStringLiteral("en"),
        QStringLiteral("light"),
        QJsonObject{{QStringLiteral("corners"), QStringLiteral("square")}});
    NRS_CHECK(!lightSquare.dark);
    NRS_CHECK(!lightSquare.rounded);
    NRS_CHECK(lightSquare.cardRadius == 0.0);
    NRS_CHECK(lightSquare.controlRadius == 0.0);
    NRS_CHECK_EQ(lightSquare.locale, QStringLiteral("en"));
    NRS_CHECK(QColor(lightSquare.page).lightness() > 220);

    // Light theme with translucent dark nativeLook colors gets blended & softened
    const QJsonObject look{
        {QStringLiteral("dark"), false},
        {QStringLiteral("rounded"), true},
        {QStringLiteral("cardRadius"), 12.0},
        {QStringLiteral("page"), QStringLiteral("#CCFFFFFF")},
        {QStringLiteral("divider"), QStringLiteral("#FF222222")},
        {QStringLiteral("hover"), QStringLiteral("#80000000")},
    };
    const nrs::ThemeData softened = nrs::ThemeData::fromAppearance(
        QStringLiteral("zh"),
        QStringLiteral("dark"),
        QJsonObject{{QStringLiteral("nativeLook"), look}});
    NRS_CHECK(!softened.dark);
    NRS_CHECK(softened.cardRadius == 12.0);
    NRS_CHECK(QColor(softened.page).alpha() == 255);
    NRS_CHECK(QColor(softened.divider).alpha() == 255);
    NRS_CHECK(QColor(softened.divider).lightness() > 190);
    NRS_CHECK(QColor(softened.hover).alpha() == 255);
    NRS_CHECK(QColor(softened.hover).lightness() > 200);
    ++g_passedGroups;
}

void testWifiAutoSwitchRules()
{
    // 1. SSID matching: exact (case-insensitive) & wildcards (* / ?) & edge cases
    NRS_CHECK(!nrs::ssidMatchesRule(QStringLiteral(""), QStringLiteral("Office")));
    NRS_CHECK(!nrs::ssidMatchesRule(QStringLiteral("   "), QStringLiteral("Office")));
    NRS_CHECK(!nrs::ssidMatchesRule(QStringLiteral("Office"), QStringLiteral("")));
    NRS_CHECK(!nrs::ssidMatchesRule(QStringLiteral("Office"), QStringLiteral("   ")));

    NRS_CHECK(nrs::ssidMatchesRule(QStringLiteral("Office-5G"), QStringLiteral("office-5g")));
    NRS_CHECK(nrs::ssidMatchesRule(QStringLiteral("Corp WiFi (5G)"), QStringLiteral("  corp wifi (5g)  ")));
    NRS_CHECK(!nrs::ssidMatchesRule(QStringLiteral("Office-5G"), QStringLiteral("Office-5")));
    NRS_CHECK(nrs::ssidMatchesRule(QStringLiteral("公司办公网"), QStringLiteral("公司办公网")));

    NRS_CHECK(nrs::ssidMatchesRule(QStringLiteral("Office-5G"), QStringLiteral("Office-*")));
    NRS_CHECK(nrs::ssidMatchesRule(QStringLiteral("Office-5G"), QStringLiteral("office-?g")));
    NRS_CHECK(!nrs::ssidMatchesRule(QStringLiteral("Office-5GHz"), QStringLiteral("Office-?G")));
    NRS_CHECK(nrs::ssidMatchesRule(QStringLiteral("Dept.(A)+Test"), QStringLiteral("dept.(a)+*")));
    NRS_CHECK(!nrs::ssidMatchesRule(QStringLiteral("MyOffice-5G"), QStringLiteral("Office-*")));

    // 2. Rule priority: exact > wildcard, earlier wins within same tier, skip disabled/empty
    const nrs::WifiRouteRule disabledExact{
        QStringLiteral("r-disabled"),
        QStringLiteral("Office-5G"),
        {QStringLiteral("USB")},
        false,
    };
    const nrs::WifiRouteRule emptySsid{
        QStringLiteral("r-empty-ssid"),
        QStringLiteral("   "),
        {QStringLiteral("WLAN")},
        true,
    };
    const nrs::WifiRouteRule emptyOrder{
        QStringLiteral("r-empty-order"),
        QStringLiteral("Office-5G"),
        {},
        true,
    };
    const nrs::WifiRouteRule wildcardFirst{
        QStringLiteral("r-wild-1"),
        QStringLiteral("Office-*"),
        {QStringLiteral("以太网 2"), QStringLiteral("WLAN")},
        true,
    };
    const nrs::WifiRouteRule wildcardSecond{
        QStringLiteral("r-wild-2"),
        QStringLiteral("Office-?G"),
        {QStringLiteral("WLAN")},
        true,
    };
    const nrs::WifiRouteRule exactRule{
        QStringLiteral("r-exact"),
        QStringLiteral("office-5g"),
        {QStringLiteral("WLAN"), QStringLiteral("以太网 2")},
        true,
    };

    const QList<nrs::WifiRouteRule> rules = {
        disabledExact, emptySsid, emptyOrder, wildcardFirst, wildcardSecond, exactRule,
    };
    NRS_CHECK(!nrs::findMatchingWifiRule(rules, QStringLiteral("")).has_value());
    NRS_CHECK(!nrs::findMatchingWifiRule(rules, QStringLiteral("HomeWiFi")).has_value());

    const auto matchedExact = nrs::findMatchingWifiRule(rules, QStringLiteral("Office-5G"));
    NRS_CHECK(matchedExact.has_value());
    NRS_CHECK_EQ(matchedExact->id, QStringLiteral("r-exact"));

    const auto matchedWild = nrs::findMatchingWifiRule(rules, QStringLiteral("Office-2G"));
    NRS_CHECK(matchedWild.has_value());
    NRS_CHECK_EQ(matchedWild->id, QStringLiteral("r-wild-1"));

    // 3. buildOrderedInternetAdaptersForRule
    nrs::AdapterInfo eth = makeInternetAdapter(10, QStringLiteral("以太网 2"), 0, 0);
    eth.ipv4InterfaceMetric = 10;
    eth.ipv6InterfaceMetric = 10;
    nrs::AdapterInfo wlan = makeInternetAdapter(20, QStringLiteral("WLAN"), 0, 0);
    wlan.ipv4InterfaceMetric = 30;
    wlan.ipv6InterfaceMetric = 30;
    nrs::AdapterInfo usb = makeInternetAdapter(30, QStringLiteral("USB 共享"), 0, std::nullopt);
    usb.ipv4InterfaceMetric = 50;

    const QList<nrs::AdapterInfo> currentInternet = {eth, wlan, usb};
    const nrs::WifiRouteRule reorderRule{
        QStringLiteral("r-order"),
        QStringLiteral("Office-5G"),
        {QStringLiteral("wlan"), QStringLiteral("不存在网卡"), QStringLiteral("WLAN")},
        true,
    };
    const QList<nrs::AdapterInfo> built = nrs::buildOrderedInternetAdaptersForRule(currentInternet, reorderRule);
    NRS_CHECK_EQ(built.size(), 3);
    NRS_CHECK_EQ(built[0].ifIndex, 20);
    NRS_CHECK_EQ(built[1].ifIndex, 10);
    NRS_CHECK_EQ(built[2].ifIndex, 30);

    const nrs::WifiRouteRule unmatchedRule{
        QStringLiteral("r-none"),
        QStringLiteral("Office-5G"),
        {QStringLiteral("离线网卡A"), QStringLiteral("离线网卡B")},
        true,
    };
    NRS_CHECK(nrs::buildOrderedInternetAdaptersForRule(currentInternet, unmatchedRule).isEmpty());

    // 4. isPriorityOrderAlreadySatisfied
    NRS_CHECK(nrs::isPriorityOrderAlreadySatisfied(currentInternet, {}, true));
    NRS_CHECK(!nrs::isPriorityOrderAlreadySatisfied(currentInternet, {eth, wlan}, true));
    NRS_CHECK(!nrs::isPriorityOrderAlreadySatisfied(currentInternet, built, true));
    NRS_CHECK(nrs::isPriorityOrderAlreadySatisfied(currentInternet, currentInternet, true));
    NRS_CHECK(nrs::isPriorityOrderAlreadySatisfied({eth}, {eth}, true));

    // Tie in effectiveV4Metric is not strictly satisfied
    nrs::AdapterInfo wlanTied = wlan;
    wlanTied.ipv4InterfaceMetric = 10;
    NRS_CHECK(!nrs::isPriorityOrderAlreadySatisfied({eth, wlanTied}, {eth, wlanTied}, false));

    // IPv4 strictly increasing (10 < 30), IPv6 reversed (300 > 10)
    nrs::AdapterInfo ethV6High = eth;
    ethV6High.ipv6RouteMetric = 290; // effV6 = 300
    nrs::AdapterInfo wlanV6Low = wlan;
    wlanV6Low.ipv6InterfaceMetric = 10; // effV6 = 10
    NRS_CHECK(nrs::isPriorityOrderAlreadySatisfied({ethV6High, wlanV6Low}, {ethV6High, wlanV6Low}, false));
    NRS_CHECK(!nrs::isPriorityOrderAlreadySatisfied({ethV6High, wlanV6Low}, {ethV6High, wlanV6Low}, true));

    // Non-adjacent IPv6 inversion (middle adapter has no IPv6 default route) & missing IPv4 default route
    nrs::AdapterInfo usbMid = usb;
    usbMid.ipv4InterfaceMetric = 20; // IPv4 strictly increasing: 10 < 20 < 30
    NRS_CHECK(nrs::isPriorityOrderAlreadySatisfied({ethV6High, usbMid, wlanV6Low}, {ethV6High, usbMid, wlanV6Low}, false));
    NRS_CHECK(!nrs::isPriorityOrderAlreadySatisfied({ethV6High, usbMid, wlanV6Low}, {ethV6High, usbMid, wlanV6Low}, true));
    nrs::AdapterInfo gwOnly = makeInternetAdapter(40, QStringLiteral("GWOnly"), 0);
    gwOnly.hasIpv4DefaultRoute = false;
    gwOnly.ipv4RouteMetric.reset();
    NRS_CHECK(nrs::isPriorityOrderAlreadySatisfied({eth, gwOnly}, {eth, gwOnly}, true));

    // 5. formatWifiRuleOrderSummary & serialization
    NRS_CHECK_EQ(
        nrs::formatWifiRuleOrderSummary({QStringLiteral("WLAN"), QStringLiteral("以太网 2")}),
        QStringLiteral("1. WLAN  →  2. 以太网 2"));
    NRS_CHECK_EQ(nrs::formatWifiRuleOrderSummary({QStringLiteral("WLAN")}), QStringLiteral("1. WLAN"));
    NRS_CHECK(nrs::formatWifiRuleOrderSummary({}).isEmpty());

    const QJsonObject dirtyRuleJson{
        {QStringLiteral("id"), QStringLiteral("rule-1")},
        {QStringLiteral("ssid"), QStringLiteral("Cafe-*")},
        {QStringLiteral("adapterOrder"), QJsonArray{
            QStringLiteral("WLAN"),
            QStringLiteral(""),
            QStringLiteral("   "),
            QStringLiteral("wlan"),
            QStringLiteral("以太网 2"),
            QStringLiteral("以太网 2"),
        }},
        {QStringLiteral("enabled"), false},
    };
    const nrs::WifiRouteRule parsedRule = nrs::WifiRouteRule::fromJson(dirtyRuleJson);
    NRS_CHECK_EQ(parsedRule.id, QStringLiteral("rule-1"));
    NRS_CHECK_EQ(parsedRule.ssid, QStringLiteral("Cafe-*"));
    NRS_CHECK_EQ(parsedRule.enabled, false);
    NRS_CHECK_EQ(parsedRule.adapterOrder, (QStringList{QStringLiteral("WLAN"), QStringLiteral("以太网 2")}));
    NRS_CHECK_EQ(nrs::WifiRouteRule::fromJson(parsedRule.toJson()), parsedRule);

    const nrs::PluginSettings legacySettings = nrs::PluginSettings::fromJson(QJsonObject{
        {QStringLiteral("syncIpv6"), false},
        {QStringLiteral("showDisconnected"), true},
    });
    NRS_CHECK_EQ(legacySettings.autoSwitchByWifi, true);

    const nrs::PluginSettings wifiOffSettings{true, false, false};
    const nrs::PluginSettings wifiOffRestored = nrs::PluginSettings::fromJson(wifiOffSettings.toJson());
    NRS_CHECK_EQ(wifiOffRestored.syncIpv6, true);
    NRS_CHECK_EQ(wifiOffRestored.showDisconnected, false);
    NRS_CHECK_EQ(wifiOffRestored.autoSwitchByWifi, false);

    nrs::RouteSnapshot snap;
    snap.connectedWifiSsid = QStringLiteral("Office-5G");
    NRS_CHECK_EQ(snap.connectedWifiSsid, QStringLiteral("Office-5G"));
    ++g_passedGroups;
}

} // namespace

int main()
{
    testComputePriorityMetricsBasicAndRounding();
    testComputePriorityMetricsIpv6AndClamp();
    testFilteringAndKindDetection();
    testEasyTierAndBettboxScenarios();
    testSpeedAndSummaryFormatting();
    testActiveEgressAndSplitWarning();
    testTunTakeoverResolvesPhysicalEgress();
    testThemeDataFromAppearance();
    testWifiAutoSwitchRules();

    if (g_failures > 0) {
        std::cerr << "CoreTests FAILED with " << g_failures << " failure(s).\n";
        return 1;
    }
    std::cout << "CoreTests PASSED (" << g_passedGroups << " groups).\n";
    return 0;
}
