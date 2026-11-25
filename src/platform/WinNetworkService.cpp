#include "platform/WinNetworkService.h"

#include "core/RouteLogic.h"

#include <QDateTime>
#include <QSet>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <wlanapi.h>
#include <iphlpapi.h>
#include <netioapi.h>
#include <icmpapi.h>
#include <shellapi.h>

#include <algorithm>
#include <atomic>
#include <cstddef>
#include <cstring>
#include <limits>
#include <map>
#include <mutex>
#include <utility>
#include <vector>

namespace nrs {

struct WinNetworkService::Impl {
    std::mutex mutex;
    std::atomic<bool> stopping{false};
    std::function<void()> onChange;
    HANDLE interfaceNotifyHandle = nullptr;
    HANDLE routeNotifyHandle = nullptr;
    HANDLE wlanNotifyHandle = nullptr;

    void invokeWatchCallback()
    {
        if (stopping.load(std::memory_order_acquire)) {
            return;
        }
        std::function<void()> callback;
        {
            std::lock_guard<std::mutex> lock(mutex);
            if (stopping.load(std::memory_order_acquire)) {
                return;
            }
            callback = onChange;
        }
        if (callback && !stopping.load(std::memory_order_acquire)) {
            callback();
        }
    }

    static VOID NETIOAPI_API_ onInterfaceChanged(
        PVOID callerContext,
        PMIB_IPINTERFACE_ROW /*row*/,
        MIB_NOTIFICATION_TYPE /*notificationType*/)
    {
        if (auto* impl = static_cast<WinNetworkService::Impl*>(callerContext)) {
            impl->invokeWatchCallback();
        }
    }

    static VOID NETIOAPI_API_ onRouteChanged(
        PVOID callerContext,
        PMIB_IPFORWARD_ROW2 /*row*/,
        MIB_NOTIFICATION_TYPE /*notificationType*/)
    {
        if (auto* impl = static_cast<WinNetworkService::Impl*>(callerContext)) {
            impl->invokeWatchCallback();
        }
    }

    static VOID WINAPI onWlanNotification(
        PWLAN_NOTIFICATION_DATA data,
        PVOID context)
    {
        auto* impl = static_cast<WinNetworkService::Impl*>(context);
        if (!impl || !data) {
            return;
        }
        if (data->NotificationSource != WLAN_NOTIFICATION_SOURCE_ACM) {
            return;
        }
        switch (data->NotificationCode) {
        case wlan_notification_acm_connection_complete:
        case wlan_notification_acm_disconnected:
        case wlan_notification_acm_interface_arrival:
        case wlan_notification_acm_interface_removal:
            impl->invokeWatchCallback();
            break;
        default:
            break;
        }
    }
};

namespace {

struct WsaGuard {
    WsaGuard()
    {
        WSADATA wsa{};
        initialized = (::WSAStartup(MAKEWORD(2, 2), &wsa) == 0);
    }

    ~WsaGuard()
    {
        if (initialized) {
            ::WSACleanup();
        }
    }

    bool initialized = false;
};

void ensureWsaInitialized()
{
    static WsaGuard guard;
    (void)guard;
}

struct InterfaceMetricData {
    bool autoMetric = true;
    int interfaceMetric = 0;
};

bool isNotFoundError(DWORD err)
{
    return err == ERROR_NOT_FOUND;
}

QString formatSystemError(DWORD err)
{
    LPWSTR buffer = nullptr;
    const DWORD flags = FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS;
    const DWORD len = ::FormatMessageW(
        flags,
        nullptr,
        err,
        MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
        reinterpret_cast<LPWSTR>(&buffer),
        0,
        nullptr);
    QString msg;
    if (len > 0 && buffer != nullptr) {
        msg = QString::fromWCharArray(buffer, static_cast<qsizetype>(len)).trimmed();
    }
    if (buffer != nullptr) {
        ::LocalFree(buffer);
    }
    return msg;
}

QString formatOperationError(const QString& action, DWORD err)
{
    if (err == ERROR_ACCESS_DENIED) {
        return QStringLiteral("%1失败：需要管理员权限，请以管理员身份运行后重试（错误码 5）。").arg(action);
    }
    const QString detail = formatSystemError(err);
    if (!detail.isEmpty()) {
        return QStringLiteral("%1失败：%2（错误码 %3）。").arg(action, detail).arg(err);
    }
    return QStringLiteral("%1失败（错误码 %2）。").arg(action).arg(err);
}

QString formatIpv4(const IN_ADDR& addr)
{
    wchar_t buf[INET_ADDRSTRLEN]{};
    if (!::InetNtopW(AF_INET, &addr, buf, INET_ADDRSTRLEN)) {
        return {};
    }
    return QString::fromWCharArray(buf);
}

QString formatIpv6(const IN6_ADDR& addr)
{
    wchar_t buf[INET6_ADDRSTRLEN]{};
    if (!::InetNtopW(AF_INET6, &addr, buf, INET6_ADDRSTRLEN)) {
        return {};
    }
    return QString::fromWCharArray(buf);
}

bool isUnspecifiedIpv6(const IN6_ADDR& addr)
{
    static constexpr unsigned char kZero[16]{};
    return std::memcmp(&addr, kZero, sizeof(kZero)) == 0;
}

std::optional<int> getBestInterfaceIndex(ADDRESS_FAMILY family, const wchar_t* probeIp)
{
    ensureWsaInitialized();
    DWORD bestIfIndex = 0;

    if (family == AF_INET) {
        SOCKADDR_IN dest{};
        dest.sin_family = AF_INET;
        if (::InetPtonW(AF_INET, probeIp, &dest.sin_addr) != 1) {
            return std::nullopt;
        }
        if (::GetBestInterfaceEx(reinterpret_cast<SOCKADDR*>(&dest), &bestIfIndex) == NO_ERROR && bestIfIndex > 0) {
            return static_cast<int>(bestIfIndex);
        }
        return std::nullopt;
    }

    if (family == AF_INET6) {
        SOCKADDR_IN6 dest{};
        dest.sin6_family = AF_INET6;
        if (::InetPtonW(AF_INET6, probeIp, &dest.sin6_addr) != 1) {
            return std::nullopt;
        }
        if (::GetBestInterfaceEx(reinterpret_cast<SOCKADDR*>(&dest), &bestIfIndex) == NO_ERROR && bestIfIndex > 0) {
            return static_cast<int>(bestIfIndex);
        }
        return std::nullopt;
    }

    return std::nullopt;
}

using FamilyKey = std::pair<int, ADDRESS_FAMILY>;

std::map<FamilyKey, InterfaceMetricData> queryInterfaceMetrics()
{
    std::map<FamilyKey, InterfaceMetricData> result;
    PMIB_IPINTERFACE_TABLE table = nullptr;
    if (::GetIpInterfaceTable(AF_UNSPEC, &table) != NO_ERROR || table == nullptr) {
        return result;
    }

    for (ULONG i = 0; i < table->NumEntries; ++i) {
        const MIB_IPINTERFACE_ROW& row = table->Table[i];
        if (row.InterfaceIndex == 0 || (row.Family != AF_INET && row.Family != AF_INET6)) {
            continue;
        }
        InterfaceMetricData data;
        data.autoMetric = (row.UseAutomaticMetric != FALSE);
        data.interfaceMetric = static_cast<int>(row.Metric);
        result[FamilyKey{static_cast<int>(row.InterfaceIndex), row.Family}] = data;
    }

    ::FreeMibTable(table);
    return result;
}

std::map<FamilyKey, int> queryDefaultRoutes()
{
    std::map<FamilyKey, int> result;
    PMIB_IPFORWARD_TABLE2 table = nullptr;
    if (::GetIpForwardTable2(AF_UNSPEC, &table) != NO_ERROR || table == nullptr) {
        return result;
    }

    for (ULONG i = 0; i < table->NumEntries; ++i) {
        const MIB_IPFORWARD_ROW2& row = table->Table[i];
        if (row.InterfaceIndex == 0 || row.DestinationPrefix.PrefixLength != 0) {
            continue;
        }
        const ADDRESS_FAMILY family = row.DestinationPrefix.Prefix.si_family;
        bool isDefault = false;
        if (family == AF_INET) {
            isDefault = (row.DestinationPrefix.Prefix.Ipv4.sin_addr.S_un.S_addr == 0);
        } else if (family == AF_INET6) {
            isDefault = isUnspecifiedIpv6(row.DestinationPrefix.Prefix.Ipv6.sin6_addr);
        }
        if (!isDefault) {
            continue;
        }

        const FamilyKey key{static_cast<int>(row.InterfaceIndex), family};
        const int routeMetric = static_cast<int>(row.Metric);
        const auto it = result.find(key);
        if (it == result.end() || routeMetric < it->second) {
            result[key] = routeMetric;
        }
    }

    ::FreeMibTable(table);
    return result;
}

DWORD setInterfaceMetricForFamily(int ifIndex, ADDRESS_FAMILY family, std::optional<int> metric)
{
    MIB_IPINTERFACE_ROW row{};
    ::InitializeIpInterfaceEntry(&row);
    row.Family = family;
    row.InterfaceIndex = static_cast<NET_IFINDEX>(ifIndex);

    const DWORD getRc = ::GetIpInterfaceEntry(&row);
    if (getRc != NO_ERROR) {
        return getRc;
    }

    if (family == AF_INET) {
        row.SitePrefixLength = 0;
    }

    if (metric.has_value()) {
        row.UseAutomaticMetric = FALSE;
        row.Metric = static_cast<ULONG>(std::clamp(*metric, 1, 9999));
    } else {
        row.UseAutomaticMetric = TRUE;
        row.Metric = 0;
    }

    return ::SetIpInterfaceEntry(&row);
}

OperationResult applyMetricToAdapter(
    int ifIndex,
    std::optional<int> metric,
    bool applyIpv6,
    const QString& action)
{
    const DWORD v4Rc = setInterfaceMetricForFamily(ifIndex, AF_INET, metric);
    if (v4Rc != NO_ERROR && !isNotFoundError(v4Rc)) {
        return OperationResult{false, formatOperationError(action, v4Rc), static_cast<int>(v4Rc)};
    }

    if (applyIpv6) {
        const DWORD v6Rc = setInterfaceMetricForFamily(ifIndex, AF_INET6, metric);
        if (v6Rc != NO_ERROR && !isNotFoundError(v6Rc)) {
            return OperationResult{false, formatOperationError(action, v6Rc), static_cast<int>(v6Rc)};
        }
        if (v4Rc != NO_ERROR && v6Rc != NO_ERROR) {
            return OperationResult{false, formatOperationError(action, v4Rc), static_cast<int>(v4Rc)};
        }
    } else if (v4Rc != NO_ERROR) {
        return OperationResult{false, formatOperationError(action, v4Rc), static_cast<int>(v4Rc)};
    }

    return OperationResult{true, {}, 0};
}

QString formatIcmpStatus(ULONG status)
{
    switch (status) {
    case IP_REQ_TIMED_OUT:
        return QStringLiteral("超时");
    case IP_DEST_NET_UNREACHABLE:
        return QStringLiteral("网络不可达");
    case IP_DEST_HOST_UNREACHABLE:
        return QStringLiteral("主机不可达");
    case IP_TTL_EXPIRED_TRANSIT:
        return QStringLiteral("TTL 超时");
    default:
        return QStringLiteral("失败(状态 %1)").arg(status);
    }
}

QString pingIpv4Host(const QString& host, const QString& label)
{
    ensureWsaInitialized();
    IN_ADDR destAddr{};
    const std::wstring hostWide = host.toStdWString();
    if (::InetPtonW(AF_INET, hostWide.c_str(), &destAddr) != 1) {
        return QStringLiteral("%1: 无效地址(%2)").arg(label, host);
    }

    const HANDLE icmpHandle = ::IcmpCreateFile();
    if (icmpHandle == INVALID_HANDLE_VALUE || icmpHandle == nullptr) {
        return QStringLiteral("%1: 创建 ICMP 句柄失败(%2)").arg(label).arg(::GetLastError());
    }

    static constexpr char kPayload[] = "NetRouteSwitcherPing0123456789AB";
    constexpr WORD kPayloadLen = static_cast<WORD>(sizeof(kPayload) - 1);
    std::vector<std::byte> replyBuf(sizeof(ICMP_ECHO_REPLY) + kPayloadLen + 64);

    const DWORD replyCount = ::IcmpSendEcho(
        icmpHandle,
        destAddr.S_un.S_addr,
        const_cast<char*>(kPayload),
        kPayloadLen,
        nullptr,
        replyBuf.data(),
        static_cast<DWORD>(replyBuf.size()),
        2000);
    const DWORD lastErr = ::GetLastError();
    ::IcmpCloseHandle(icmpHandle);

    if (replyCount > 0) {
        const auto* reply = reinterpret_cast<const ICMP_ECHO_REPLY*>(replyBuf.data());
        if (reply->Status == IP_SUCCESS) {
            return QStringLiteral("%1: %2ms").arg(label).arg(reply->RoundTripTime);
        }
        return QStringLiteral("%1: %2").arg(label, formatIcmpStatus(reply->Status));
    }

    return QStringLiteral("%1: %2").arg(label, formatIcmpStatus(lastErr));
}

struct WlanApi {
    using OpenHandleFn = DWORD(WINAPI*)(DWORD, PVOID, PDWORD, PHANDLE);
    using CloseHandleFn = DWORD(WINAPI*)(HANDLE, PVOID);
    using EnumInterfacesFn = DWORD(WINAPI*)(HANDLE, PVOID, PWLAN_INTERFACE_INFO_LIST*);
    using QueryInterfaceFn = DWORD(WINAPI*)(
        HANDLE,
        CONST GUID*,
        WLAN_INTF_OPCODE,
        PVOID,
        PDWORD,
        PVOID*,
        PWLAN_OPCODE_VALUE_TYPE);
    using GetProfileListFn = DWORD(WINAPI*)(HANDLE, CONST GUID*, PVOID, PWLAN_PROFILE_INFO_LIST*);
    using RegisterNotificationFn = DWORD(WINAPI*)(
        HANDLE,
        DWORD,
        BOOL,
        WLAN_NOTIFICATION_CALLBACK,
        PVOID,
        PVOID,
        PDWORD);
    using FreeMemoryFn = VOID(WINAPI*)(PVOID);

    HMODULE module = nullptr;
    OpenHandleFn openHandle = nullptr;
    CloseHandleFn closeHandle = nullptr;
    EnumInterfacesFn enumInterfaces = nullptr;
    QueryInterfaceFn queryInterface = nullptr;
    GetProfileListFn getProfileList = nullptr;
    RegisterNotificationFn registerNotification = nullptr;
    FreeMemoryFn freeMemory = nullptr;

    bool isAvailable() const
    {
        return module != nullptr
            && openHandle != nullptr
            && closeHandle != nullptr
            && enumInterfaces != nullptr
            && queryInterface != nullptr
            && getProfileList != nullptr
            && registerNotification != nullptr
            && freeMemory != nullptr;
    }
};

const WlanApi& loadWlanApi()
{
    static const WlanApi api = []() -> WlanApi {
        WlanApi loaded;
        loaded.module = ::LoadLibraryW(L"wlanapi.dll");
        if (!loaded.module) {
            return loaded;
        }
        loaded.openHandle = reinterpret_cast<WlanApi::OpenHandleFn>(
            ::GetProcAddress(loaded.module, "WlanOpenHandle"));
        loaded.closeHandle = reinterpret_cast<WlanApi::CloseHandleFn>(
            ::GetProcAddress(loaded.module, "WlanCloseHandle"));
        loaded.enumInterfaces = reinterpret_cast<WlanApi::EnumInterfacesFn>(
            ::GetProcAddress(loaded.module, "WlanEnumInterfaces"));
        loaded.queryInterface = reinterpret_cast<WlanApi::QueryInterfaceFn>(
            ::GetProcAddress(loaded.module, "WlanQueryInterface"));
        loaded.getProfileList = reinterpret_cast<WlanApi::GetProfileListFn>(
            ::GetProcAddress(loaded.module, "WlanGetProfileList"));
        loaded.registerNotification = reinterpret_cast<WlanApi::RegisterNotificationFn>(
            ::GetProcAddress(loaded.module, "WlanRegisterNotification"));
        loaded.freeMemory = reinterpret_cast<WlanApi::FreeMemoryFn>(
            ::GetProcAddress(loaded.module, "WlanFreeMemory"));
        if (!loaded.isAvailable()) {
            ::FreeLibrary(loaded.module);
            loaded = WlanApi{};
        }
        return loaded;
    }();
    return api;
}

qsizetype boundedWideLength(const wchar_t* text, qsizetype maxChars)
{
    qsizetype len = 0;
    while (len < maxChars && text[len] != L'\0') {
        ++len;
    }
    return len;
}

} // namespace

WinNetworkService::WinNetworkService()
    : m_impl(std::make_unique<Impl>())
{
    ensureWsaInitialized();
}

WinNetworkService::~WinNetworkService()
{
    stopWatching();
}

bool WinNetworkService::isCurrentProcessElevated()
{
    HANDLE token = nullptr;
    if (!::OpenProcessToken(::GetCurrentProcess(), TOKEN_QUERY, &token)) {
        return false;
    }

    TOKEN_ELEVATION elevation{};
    DWORD returnedBytes = 0;
    const BOOL ok = ::GetTokenInformation(
        token,
        TokenElevation,
        &elevation,
        sizeof(elevation),
        &returnedBytes);
    ::CloseHandle(token);
    return ok && elevation.TokenIsElevated != 0;
}

RouteSnapshot WinNetworkService::getSnapshot() const
{
    ensureWsaInitialized();

    RouteSnapshot snap;
    snap.capturedAtIso = QDateTime::currentDateTime().toString(Qt::ISODate);
    snap.connectedWifiSsid = getConnectedWifiSsid();
    snap.osActiveV4IfIndex = getBestInterfaceIndex(AF_INET, L"223.5.5.5");
    snap.osActiveV6IfIndex = getBestInterfaceIndex(AF_INET6, L"2400:3200::1");
    snap.activeV4IfIndex = snap.osActiveV4IfIndex;
    snap.activeV6IfIndex = snap.osActiveV6IfIndex;

    const auto metricsMap = queryInterfaceMetrics();
    const auto routesMap = queryDefaultRoutes();

    const ULONG flags = GAA_FLAG_INCLUDE_PREFIX
        | GAA_FLAG_INCLUDE_GATEWAYS
        | GAA_FLAG_SKIP_ANYCAST
        | GAA_FLAG_SKIP_MULTICAST;
    ULONG bufSize = 65536;
    std::vector<std::byte> buffer;
    DWORD rc = ERROR_BUFFER_OVERFLOW;

    for (int attempt = 0; attempt < 4 && rc == ERROR_BUFFER_OVERFLOW; ++attempt) {
        buffer.resize(bufSize);
        rc = ::GetAdaptersAddresses(
            AF_UNSPEC,
            flags,
            nullptr,
            reinterpret_cast<PIP_ADAPTER_ADDRESSES>(buffer.data()),
            &bufSize);
    }

    if (rc != NO_ERROR) {
        return snap;
    }

    constexpr quint64 kUnknownSpeed = std::numeric_limits<quint64>::max();
    for (PIP_ADAPTER_ADDRESSES nic = reinterpret_cast<PIP_ADAPTER_ADDRESSES>(buffer.data());
         nic != nullptr;
         nic = nic->Next) {
        const unsigned int ifType = nic->IfType;
        const QString name = nic->FriendlyName ? QString::fromWCharArray(nic->FriendlyName) : QString{};
        const QString desc = nic->Description ? QString::fromWCharArray(nic->Description) : QString{};
        if (shouldIgnoreAdapter(ifType, desc)) {
            continue;
        }

        int ifIndex = static_cast<int>(nic->IfIndex);
        if (ifIndex <= 0) {
            ifIndex = static_cast<int>(nic->Ipv6IfIndex);
        }
        if (ifIndex <= 0) {
            continue;
        }

        AdapterInfo info;
        info.ifIndex = ifIndex;
        info.name = name;
        info.description = desc;
        info.ifType = ifType;
        info.isUp = (nic->OperStatus == IfOperStatusUp);

        if (nic->TransmitLinkSpeed != 0 && nic->TransmitLinkSpeed != kUnknownSpeed) {
            info.speedBps = static_cast<quint64>(nic->TransmitLinkSpeed);
        } else if (nic->ReceiveLinkSpeed != 0 && nic->ReceiveLinkSpeed != kUnknownSpeed) {
            info.speedBps = static_cast<quint64>(nic->ReceiveLinkSpeed);
        }

        for (auto* ua = nic->FirstUnicastAddress; ua != nullptr; ua = ua->Next) {
            if (!ua->Address.lpSockaddr || ua->Address.lpSockaddr->sa_family != AF_INET) {
                continue;
            }
            const auto* sin = reinterpret_cast<const SOCKADDR_IN*>(ua->Address.lpSockaddr);
            const QString ip = formatIpv4(sin->sin_addr);
            if (!ip.isEmpty()) {
                info.ipv4Addresses.append(QStringLiteral("%1/%2").arg(ip).arg(ua->OnLinkPrefixLength));
            }
        }

        for (auto* gw = nic->FirstGatewayAddress; gw != nullptr; gw = gw->Next) {
            if (!gw->Address.lpSockaddr) {
                continue;
            }
            if (gw->Address.lpSockaddr->sa_family == AF_INET) {
                const auto* sin = reinterpret_cast<const SOCKADDR_IN*>(gw->Address.lpSockaddr);
                if (sin->sin_addr.S_un.S_addr != 0) {
                    const QString ip = formatIpv4(sin->sin_addr);
                    if (!ip.isEmpty() && !info.ipv4Gateways.contains(ip)) {
                        info.ipv4Gateways.append(ip);
                    }
                }
            } else if (gw->Address.lpSockaddr->sa_family == AF_INET6) {
                const auto* sin6 = reinterpret_cast<const SOCKADDR_IN6*>(gw->Address.lpSockaddr);
                if (!isUnspecifiedIpv6(sin6->sin6_addr)) {
                    const QString ip = formatIpv6(sin6->sin6_addr);
                    if (!ip.isEmpty() && !info.ipv6Gateways.contains(ip)) {
                        info.ipv6Gateways.append(ip);
                    }
                }
            }
        }

        for (auto* dns = nic->FirstDnsServerAddress; dns != nullptr; dns = dns->Next) {
            if (!dns->Address.lpSockaddr) {
                continue;
            }
            QString ip;
            if (dns->Address.lpSockaddr->sa_family == AF_INET) {
                const auto* sin = reinterpret_cast<const SOCKADDR_IN*>(dns->Address.lpSockaddr);
                ip = formatIpv4(sin->sin_addr);
            } else if (dns->Address.lpSockaddr->sa_family == AF_INET6) {
                const auto* sin6 = reinterpret_cast<const SOCKADDR_IN6*>(dns->Address.lpSockaddr);
                ip = formatIpv6(sin6->sin6_addr);
            }
            if (ip.isEmpty() || ip.startsWith(QLatin1String("fec0:0:0:ffff::"), Qt::CaseInsensitive)) {
                continue;
            }
            if (!info.dnsServers.contains(ip)) {
                info.dnsServers.append(ip);
            }
        }

        if (const auto it = metricsMap.find(FamilyKey{ifIndex, AF_INET}); it != metricsMap.end()) {
            info.ipv4AutoMetric = it->second.autoMetric;
            info.ipv4InterfaceMetric = it->second.interfaceMetric;
        }
        if (const auto it = metricsMap.find(FamilyKey{ifIndex, AF_INET6}); it != metricsMap.end()) {
            info.ipv6AutoMetric = it->second.autoMetric;
            info.ipv6InterfaceMetric = it->second.interfaceMetric;
        }
        if (const auto it = routesMap.find(FamilyKey{ifIndex, AF_INET}); it != routesMap.end()) {
            info.hasIpv4DefaultRoute = true;
            info.ipv4RouteMetric = it->second;
        }
        if (const auto it = routesMap.find(FamilyKey{ifIndex, AF_INET6}); it != routesMap.end()) {
            info.hasIpv6DefaultRoute = true;
            info.ipv6RouteMetric = it->second;
        }

        info.kind = detectAdapterKind(info.ifType, info.name, info.description);
        info.category = classifyAdapter(info);
        snap.adapters.append(std::move(info));
    }

    sortAdapters(snap.adapters);
    resolvePhysicalEgress(snap);
    return snap;
}

QString WinNetworkService::getConnectedWifiSsid() const
{
    const WlanApi& api = loadWlanApi();
    if (!api.isAvailable()) {
        return {};
    }

    DWORD negotiatedVersion = 0;
    HANDLE clientHandle = nullptr;
    if (api.openHandle(WLAN_API_VERSION_2_0, nullptr, &negotiatedVersion, &clientHandle) != ERROR_SUCCESS
        || clientHandle == nullptr) {
        return {};
    }

    QString ssidText;
    PWLAN_INTERFACE_INFO_LIST ifList = nullptr;
    if (api.enumInterfaces(clientHandle, nullptr, &ifList) == ERROR_SUCCESS && ifList != nullptr) {
        for (DWORD i = 0; i < ifList->dwNumberOfItems; ++i) {
            const WLAN_INTERFACE_INFO& iface = ifList->InterfaceInfo[i];
            if (iface.isState != wlan_interface_state_connected) {
                continue;
            }

            DWORD dataSize = 0;
            PVOID rawData = nullptr;
            WLAN_OPCODE_VALUE_TYPE valueType = wlan_opcode_value_type_invalid;
            const DWORD qRc = api.queryInterface(
                clientHandle,
                &iface.InterfaceGuid,
                wlan_intf_opcode_current_connection,
                nullptr,
                &dataSize,
                &rawData,
                &valueType);
            if (qRc == ERROR_SUCCESS && rawData != nullptr) {
                if (dataSize >= sizeof(WLAN_CONNECTION_ATTRIBUTES)) {
                    const auto* conn = static_cast<const WLAN_CONNECTION_ATTRIBUTES*>(rawData);
                    if (conn->isState == wlan_interface_state_connected) {
                        const DOT11_SSID& ssid = conn->wlanAssociationAttributes.dot11Ssid;
                        const ULONG ssidLen = std::min<ULONG>(
                            ssid.uSSIDLength,
                            static_cast<ULONG>(sizeof(ssid.ucSSID)));
                        if (ssidLen > 0) {
                            ssidText = QString::fromUtf8(
                                reinterpret_cast<const char*>(ssid.ucSSID),
                                static_cast<int>(ssidLen));
                        } else {
                            const qsizetype profileLen = boundedWideLength(
                                conn->strProfileName,
                                static_cast<qsizetype>(WLAN_MAX_NAME_LENGTH));
                            if (profileLen > 0) {
                                ssidText = QString::fromWCharArray(conn->strProfileName, profileLen);
                            }
                        }
                    }
                }
                api.freeMemory(rawData);
                if (!ssidText.isEmpty()) {
                    break;
                }
            } else if (rawData != nullptr) {
                api.freeMemory(rawData);
            }
        }
        api.freeMemory(ifList);
    }

    api.closeHandle(clientHandle, nullptr);
    return ssidText;
}

QStringList WinNetworkService::getSavedWifiProfiles() const
{
    QStringList profiles;
    const WlanApi& api = loadWlanApi();
    if (!api.isAvailable()) {
        return profiles;
    }

    DWORD negotiatedVersion = 0;
    HANDLE clientHandle = nullptr;
    if (api.openHandle(WLAN_API_VERSION_2_0, nullptr, &negotiatedVersion, &clientHandle) != ERROR_SUCCESS
        || clientHandle == nullptr) {
        return profiles;
    }

    QSet<QString> seen;
    PWLAN_INTERFACE_INFO_LIST ifList = nullptr;
    if (api.enumInterfaces(clientHandle, nullptr, &ifList) == ERROR_SUCCESS && ifList != nullptr) {
        for (DWORD i = 0; i < ifList->dwNumberOfItems; ++i) {
            const WLAN_INTERFACE_INFO& iface = ifList->InterfaceInfo[i];
            PWLAN_PROFILE_INFO_LIST profileList = nullptr;
            const DWORD pRc = api.getProfileList(
                clientHandle,
                &iface.InterfaceGuid,
                nullptr,
                &profileList);
            if (pRc == ERROR_SUCCESS && profileList != nullptr) {
                for (DWORD j = 0; j < profileList->dwNumberOfItems; ++j) {
                    const WLAN_PROFILE_INFO& info = profileList->ProfileInfo[j];
                    const qsizetype len = boundedWideLength(
                        info.strProfileName,
                        static_cast<qsizetype>(WLAN_MAX_NAME_LENGTH));
                    const QString name = QString::fromWCharArray(info.strProfileName, len);
                    if (name.trimmed().isEmpty()) {
                        continue;
                    }
                    const QString key = name.toLower();
                    if (seen.contains(key)) {
                        continue;
                    }
                    seen.insert(key);
                    profiles.append(name);
                }
                api.freeMemory(profileList);
            } else if (profileList != nullptr) {
                api.freeMemory(profileList);
            }
        }
        api.freeMemory(ifList);
    }

    api.closeHandle(clientHandle, nullptr);
    return profiles;
}

OperationResult WinNetworkService::applyPriorityOrder(
    const QList<AdapterInfo>& orderedInternetAdapters,
    bool syncIpv6) const
{
    if (orderedInternetAdapters.isEmpty()) {
        return OperationResult{true, QStringLiteral("当前没有可调整优先级的互联网出口网卡。"), 0};
    }

    const QList<int> targetMetrics = computePriorityMetrics(orderedInternetAdapters, syncIpv6);
    QStringList parts;
    parts.reserve(orderedInternetAdapters.size());

    for (qsizetype i = 0; i < orderedInternetAdapters.size(); ++i) {
        const AdapterInfo& adapter = orderedInternetAdapters[i];
        const int targetMetric = targetMetrics[i];
        const OperationResult res = applyMetricToAdapter(
            adapter.ifIndex,
            targetMetric,
            syncIpv6,
            QStringLiteral("应用出口优先级"));
        if (!res.ok) {
            return res;
        }
        parts.append(QStringLiteral("%1=%2").arg(adapter.name).arg(targetMetric));
    }

    return OperationResult{
        true,
        QStringLiteral("已应用出口优先级（跃点数: %1）").arg(parts.join(QStringLiteral("，"))),
        0,
    };
}

OperationResult WinNetworkService::setSingleAdapterMetric(
    int ifIndex,
    std::optional<int> metric,
    bool applyIpv6) const
{
    if (ifIndex <= 0) {
        return OperationResult{false, QStringLiteral("无效的网卡接口索引。"), ERROR_INVALID_PARAMETER};
    }

    std::optional<int> clampedMetric = metric;
    if (clampedMetric.has_value()) {
        clampedMetric = std::clamp(*clampedMetric, 1, 9999);
    }

    const OperationResult res = applyMetricToAdapter(
        ifIndex,
        clampedMetric,
        applyIpv6,
        QStringLiteral("设置接口跃点数"));
    if (!res.ok) {
        return res;
    }

    if (clampedMetric.has_value()) {
        return OperationResult{
            true,
            QStringLiteral("已手动设置接口 #%1 跃点数为 %2。").arg(ifIndex).arg(*clampedMetric),
            0,
        };
    }
    return OperationResult{
        true,
        QStringLiteral("已恢复接口 #%1 为系统自动跃点。").arg(ifIndex),
        0,
    };
}

OperationResult WinNetworkService::restoreAllAutomatic(const RouteSnapshot& snapshot) const
{
    QSet<int> visited;
    QList<int> targetIds;
    for (const AdapterInfo& adapter : snapshot.adapters) {
        if (adapter.ifIndex <= 0 || visited.contains(adapter.ifIndex)) {
            continue;
        }
        if (adapter.category == AdapterCategory::Internet
            || !adapter.ipv4AutoMetric
            || !adapter.ipv6AutoMetric) {
            visited.insert(adapter.ifIndex);
            targetIds.append(adapter.ifIndex);
        }
    }

    if (targetIds.isEmpty()) {
        return OperationResult{true, QStringLiteral("当前没有需要恢复自动跃点的网卡。"), 0};
    }

    for (const int ifIndex : targetIds) {
        const OperationResult res = applyMetricToAdapter(
            ifIndex,
            std::nullopt,
            true,
            QStringLiteral("恢复自动跃点"));
        if (!res.ok) {
            return res;
        }
    }

    return OperationResult{
        true,
        QStringLiteral("已恢复 %1 个网卡为 Windows 系统自动跃点。").arg(targetIds.size()),
        0,
    };
}

QString WinNetworkService::testConnectivity(const RouteSnapshot& snapshot) const
{
    const AdapterInfo* active = snapshot.activeV4Adapter();
    if (!active) {
        if (const std::optional<int> bestV4 = getBestInterfaceIndex(AF_INET, L"223.5.5.5"); bestV4.has_value()) {
            active = snapshot.findByIfIndex(*bestV4);
        }
    }
    if (!active) {
        return QStringLiteral("当前未检测到可用的 IPv4 互联网出口。");
    }

    const QString gw = active->ipv4Gateways.isEmpty() ? QString{} : active->ipv4Gateways.first();
    const QString gwResult = gw.isEmpty()
        ? QStringLiteral("网关: 无")
        : pingIpv4Host(gw, QStringLiteral("网关"));
    const QString pubResult = pingIpv4Host(QStringLiteral("223.5.5.5"), QStringLiteral("公网(223.5.5.5)"));

    if (const AdapterInfo* tun = snapshot.tunTakeoverAdapter()) {
        return QStringLiteral("当前物理出口 [%1] (TUN: %2) · %3 · %4")
            .arg(active->name, tun->name, gwResult, pubResult);
    }
    return QStringLiteral("当前出口 [%1] · %2 · %3").arg(active->name, gwResult, pubResult);
}

bool WinNetworkService::openNetworkConnections()
{
    const HINSTANCE rc = ::ShellExecuteW(
        nullptr,
        L"open",
        L"ncpa.cpl",
        nullptr,
        nullptr,
        SW_SHOWNORMAL);
    return reinterpret_cast<INT_PTR>(rc) > 32;
}

void WinNetworkService::startWatching(std::function<void()> onChange)
{
    stopWatching();
    if (!onChange) {
        return;
    }

    {
        std::lock_guard<std::mutex> lock(m_impl->mutex);
        m_impl->onChange = std::move(onChange);
        m_impl->stopping.store(false, std::memory_order_release);
    }

    HANDLE ifHandle = nullptr;
    const DWORD ifRc = ::NotifyIpInterfaceChange(
        AF_UNSPEC,
        &Impl::onInterfaceChanged,
        m_impl.get(),
        FALSE,
        &ifHandle);

    HANDLE routeHandle = nullptr;
    const DWORD routeRc = ::NotifyRouteChange2(
        AF_UNSPEC,
        &Impl::onRouteChanged,
        m_impl.get(),
        FALSE,
        &routeHandle);

    HANDLE wlanHandle = nullptr;
    const WlanApi& wlanApi = loadWlanApi();
    if (wlanApi.isAvailable()) {
        DWORD negotiatedVersion = 0;
        if (wlanApi.openHandle(WLAN_API_VERSION_2_0, nullptr, &negotiatedVersion, &wlanHandle) == ERROR_SUCCESS
            && wlanHandle != nullptr) {
            const DWORD regRc = wlanApi.registerNotification(
                wlanHandle,
                WLAN_NOTIFICATION_SOURCE_ACM,
                TRUE,
                &Impl::onWlanNotification,
                m_impl.get(),
                nullptr,
                nullptr);
            if (regRc != ERROR_SUCCESS) {
                wlanApi.closeHandle(wlanHandle, nullptr);
                wlanHandle = nullptr;
            }
        } else {
            wlanHandle = nullptr;
        }
    }

    bool cancelledEarly = false;
    {
        std::lock_guard<std::mutex> lock(m_impl->mutex);
        if (m_impl->stopping.load(std::memory_order_acquire)) {
            cancelledEarly = true;
        } else {
            if (ifRc == NO_ERROR) {
                m_impl->interfaceNotifyHandle = ifHandle;
            }
            if (routeRc == NO_ERROR) {
                m_impl->routeNotifyHandle = routeHandle;
            }
            if (wlanHandle != nullptr) {
                m_impl->wlanNotifyHandle = wlanHandle;
            }
        }
    }

    if (cancelledEarly) {
        if (ifRc == NO_ERROR && ifHandle != nullptr) {
            ::CancelMibChangeNotify2(ifHandle);
        }
        if (routeRc == NO_ERROR && routeHandle != nullptr) {
            ::CancelMibChangeNotify2(routeHandle);
        }
        if (wlanHandle != nullptr && wlanApi.isAvailable()) {
            wlanApi.registerNotification(
                wlanHandle,
                WLAN_NOTIFICATION_SOURCE_NONE,
                TRUE,
                nullptr,
                nullptr,
                nullptr,
                nullptr);
            wlanApi.closeHandle(wlanHandle, nullptr);
        }
    }
}

void WinNetworkService::stopWatching()
{
    if (!m_impl) {
        return;
    }

    m_impl->stopping.store(true, std::memory_order_release);

    HANDLE ifHandle = nullptr;
    HANDLE routeHandle = nullptr;
    HANDLE wlanHandle = nullptr;
    {
        std::lock_guard<std::mutex> lock(m_impl->mutex);
        ifHandle = std::exchange(m_impl->interfaceNotifyHandle, nullptr);
        routeHandle = std::exchange(m_impl->routeNotifyHandle, nullptr);
        wlanHandle = std::exchange(m_impl->wlanNotifyHandle, nullptr);
    }

    if (ifHandle != nullptr) {
        ::CancelMibChangeNotify2(ifHandle);
    }
    if (routeHandle != nullptr) {
        ::CancelMibChangeNotify2(routeHandle);
    }
    if (wlanHandle != nullptr) {
        const WlanApi& wlanApi = loadWlanApi();
        if (wlanApi.isAvailable()) {
            wlanApi.registerNotification(
                wlanHandle,
                WLAN_NOTIFICATION_SOURCE_NONE,
                TRUE,
                nullptr,
                nullptr,
                nullptr,
                nullptr);
            wlanApi.closeHandle(wlanHandle, nullptr);
        }
    }

    std::lock_guard<std::mutex> lock(m_impl->mutex);
    m_impl->onChange = nullptr;
}

} // namespace nrs
