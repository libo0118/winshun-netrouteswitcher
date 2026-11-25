#pragma once

#include "core/RouteModels.h"

#include <QList>
#include <QString>

namespace nrs {

bool shouldIgnoreAdapter(unsigned int ifType, const QString& description);

AdapterKind detectAdapterKind(
    unsigned int ifType,
    const QString& name,
    const QString& description);

AdapterCategory classifyAdapter(const AdapterInfo& info);

void sortAdapters(QList<AdapterInfo>& adapters);

QList<int> computePriorityMetrics(
    const QList<AdapterInfo>& orderedInternetAdapters,
    bool syncIpv6);

QString formatLinkSpeed(quint64 speedBps);

QString kindDisplayText(AdapterKind kind);

QString categoryDisplayText(const AdapterInfo& a);

QString formatNetworkSummary(const AdapterInfo& a);

QString formatMetricSummary(const AdapterInfo& a);

QString formatEgressBadge(const AdapterInfo& a, const RouteSnapshot& snap);

QString formatActiveV4Headline(const RouteSnapshot& snap);

QString formatActiveV6Headline(const RouteSnapshot& snap);

QString formatSplitEgressWarning(const RouteSnapshot& snap);

bool matchesFilter(const AdapterInfo& a, const QString& keyword);

} // namespace nrs
