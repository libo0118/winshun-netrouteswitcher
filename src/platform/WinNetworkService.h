#pragma once

#include "core/RouteModels.h"

#include <QList>
#include <QString>

#include <functional>
#include <memory>
#include <optional>

namespace nrs {

class WinNetworkService {
public:
    WinNetworkService();
    ~WinNetworkService();

    WinNetworkService(const WinNetworkService&) = delete;
    WinNetworkService& operator=(const WinNetworkService&) = delete;
    WinNetworkService(WinNetworkService&&) = delete;
    WinNetworkService& operator=(WinNetworkService&&) = delete;

    static bool isCurrentProcessElevated();

    RouteSnapshot getSnapshot() const;

    OperationResult applyPriorityOrder(
        const QList<AdapterInfo>& orderedInternetAdapters,
        bool syncIpv6) const;

    OperationResult setSingleAdapterMetric(
        int ifIndex,
        std::optional<int> metric,
        bool applyIpv6) const;

    OperationResult restoreAllAutomatic(const RouteSnapshot& snapshot) const;

    QString testConnectivity(const RouteSnapshot& snapshot) const;

    static bool openNetworkConnections();

    void startWatching(std::function<void()> onChange);
    void stopWatching();

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

} // namespace nrs
