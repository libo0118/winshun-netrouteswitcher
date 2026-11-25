#include "ui/AdapterListModel.h"

#include "core/RouteLogic.h"

namespace nrs {

namespace {

QString makeRankText(const AdapterInfo& a, int row)
{
    if (a.category == AdapterCategory::Internet) {
        return row == 0
            ? QStringLiteral("#1 首选")
            : QStringLiteral("#%1 备选").arg(row + 1);
    }
    return categoryDisplayText(a);
}

RouteSnapshot makeBadgeSnapshot(
    std::optional<int> activeV4IfIndex,
    std::optional<int> activeV6IfIndex)
{
    RouteSnapshot snap;
    snap.activeV4IfIndex = activeV4IfIndex;
    snap.activeV6IfIndex = activeV6IfIndex;
    return snap;
}

} // namespace

AdapterListModel::AdapterListModel(QObject* parent)
    : QAbstractListModel(parent)
{
}

int AdapterListModel::rowCount(const QModelIndex& parent) const
{
    return parent.isValid() ? 0 : static_cast<int>(m_adapters.size());
}

QVariant AdapterListModel::data(const QModelIndex& index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_adapters.size()) {
        return {};
    }

    const int row = index.row();
    const AdapterInfo& a = m_adapters.at(row);
    const bool activeV4 = m_activeV4IfIndex.has_value() && *m_activeV4IfIndex == a.ifIndex;
    const bool activeV6 = m_activeV6IfIndex.has_value() && *m_activeV6IfIndex == a.ifIndex;

    switch (role) {
    case IfIndexRole:
        return a.ifIndex;
    case NameRole:
    case Qt::DisplayRole:
        return a.name;
    case DescriptionRole:
        return a.description;
    case KindTextRole:
        return kindDisplayText(a.kind);
    case CategoryTextRole:
        return categoryDisplayText(a);
    case RankTextRole:
        return makeRankText(a, row);
    case RankIndexRole:
        return row;
    case IsFirstRole:
        return row == 0;
    case IsLastRole:
        return row + 1 == m_adapters.size();
    case IsActiveV4Role:
        return activeV4;
    case IsActiveV6Role:
        return activeV6;
    case EgressBadgeRole:
        return formatEgressBadge(a, makeBadgeSnapshot(m_activeV4IfIndex, m_activeV6IfIndex));
    case NetworkSummaryRole:
        return formatNetworkSummary(a);
    case MetricSummaryRole:
        return formatMetricSummary(a);
    case Ipv4InterfaceMetricRole:
        return a.ipv4InterfaceMetric;
    case Ipv4AutoMetricRole:
        return a.ipv4AutoMetric;
    case HasDefaultRouteRole:
        return a.hasIpv4DefaultRoute || a.hasIpv6DefaultRoute;
    case IsInternetRole:
        return a.category == AdapterCategory::Internet;
    case IsVirtualTunnelRole:
        return a.kind == AdapterKind::VirtualTunnel;
    default:
        return {};
    }
}

QHash<int, QByteArray> AdapterListModel::roleNames() const
{
    return {
        {IfIndexRole, "ifIndex"},
        {NameRole, "name"},
        {DescriptionRole, "description"},
        {KindTextRole, "kindText"},
        {CategoryTextRole, "categoryText"},
        {RankTextRole, "rankText"},
        {RankIndexRole, "rankIndex"},
        {IsFirstRole, "isFirst"},
        {IsLastRole, "isLast"},
        {IsActiveV4Role, "isActiveV4"},
        {IsActiveV6Role, "isActiveV6"},
        {EgressBadgeRole, "egressBadge"},
        {NetworkSummaryRole, "networkSummary"},
        {MetricSummaryRole, "metricSummary"},
        {Ipv4InterfaceMetricRole, "ipv4InterfaceMetric"},
        {Ipv4AutoMetricRole, "ipv4AutoMetric"},
        {HasDefaultRouteRole, "hasDefaultRoute"},
        {IsInternetRole, "isInternet"},
        {IsVirtualTunnelRole, "isVirtualTunnel"},
    };
}

void AdapterListModel::setAdapters(
    const QList<AdapterInfo>& adapters,
    std::optional<int> activeV4IfIndex,
    std::optional<int> activeV6IfIndex)
{
    beginResetModel();
    m_adapters = adapters;
    m_activeV4IfIndex = activeV4IfIndex;
    m_activeV6IfIndex = activeV6IfIndex;
    endResetModel();
    emit countChanged();
}

QVariantMap AdapterListModel::get(int row) const
{
    if (row < 0 || row >= m_adapters.size()) {
        return {};
    }
    const AdapterInfo& a = m_adapters.at(row);
    const bool activeV4 = m_activeV4IfIndex.has_value() && *m_activeV4IfIndex == a.ifIndex;
    const bool activeV6 = m_activeV6IfIndex.has_value() && *m_activeV6IfIndex == a.ifIndex;
    return {
        {QStringLiteral("ifIndex"), a.ifIndex},
        {QStringLiteral("name"), a.name},
        {QStringLiteral("description"), a.description},
        {QStringLiteral("kindText"), kindDisplayText(a.kind)},
        {QStringLiteral("categoryText"), categoryDisplayText(a)},
        {QStringLiteral("rankText"), makeRankText(a, row)},
        {QStringLiteral("rankIndex"), row},
        {QStringLiteral("isFirst"), row == 0},
        {QStringLiteral("isLast"), row + 1 == m_adapters.size()},
        {QStringLiteral("isActiveV4"), activeV4},
        {QStringLiteral("isActiveV6"), activeV6},
        {QStringLiteral("egressBadge"), formatEgressBadge(a, makeBadgeSnapshot(m_activeV4IfIndex, m_activeV6IfIndex))},
        {QStringLiteral("networkSummary"), formatNetworkSummary(a)},
        {QStringLiteral("metricSummary"), formatMetricSummary(a)},
        {QStringLiteral("ipv4InterfaceMetric"), a.ipv4InterfaceMetric},
        {QStringLiteral("ipv4AutoMetric"), a.ipv4AutoMetric},
        {QStringLiteral("hasDefaultRoute"), a.hasIpv4DefaultRoute || a.hasIpv6DefaultRoute},
        {QStringLiteral("isInternet"), a.category == AdapterCategory::Internet},
        {QStringLiteral("isVirtualTunnel"), a.kind == AdapterKind::VirtualTunnel},
    };
}

} // namespace nrs
