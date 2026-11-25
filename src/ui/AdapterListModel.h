#pragma once

#include "core/RouteModels.h"

#include <QAbstractListModel>
#include <QList>
#include <QVariantMap>

#include <optional>

namespace nrs {

class AdapterListModel : public QAbstractListModel {
    Q_OBJECT
    Q_PROPERTY(int count READ count NOTIFY countChanged)

public:
    enum Roles {
        IfIndexRole = Qt::UserRole + 1,
        NameRole,
        DescriptionRole,
        KindTextRole,
        CategoryTextRole,
        RankTextRole,
        RankIndexRole,
        IsFirstRole,
        IsLastRole,
        IsActiveV4Role,
        IsActiveV6Role,
        EgressBadgeRole,
        NetworkSummaryRole,
        MetricSummaryRole,
        Ipv4InterfaceMetricRole,
        Ipv4AutoMetricRole,
        HasDefaultRouteRole,
        IsInternetRole,
        IsVirtualTunnelRole,
        IsTunTakeoverRole,
    };
    Q_ENUM(Roles)

    explicit AdapterListModel(QObject* parent = nullptr);

    int rowCount(const QModelIndex& parent = QModelIndex()) const override;
    QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override;
    QHash<int, QByteArray> roleNames() const override;

    int count() const { return static_cast<int>(m_adapters.size()); }
    const QList<AdapterInfo>& adapters() const { return m_adapters; }

    void setAdapters(
        const QList<AdapterInfo>& adapters,
        std::optional<int> activeV4IfIndex,
        std::optional<int> activeV6IfIndex,
        std::optional<int> tunTakeoverV4IfIndex = std::nullopt,
        std::optional<int> tunTakeoverV6IfIndex = std::nullopt);

    Q_INVOKABLE QVariantMap get(int row) const;

signals:
    void countChanged();

private:
    QList<AdapterInfo> m_adapters;
    std::optional<int> m_activeV4IfIndex;
    std::optional<int> m_activeV6IfIndex;
    std::optional<int> m_tunTakeoverV4IfIndex;
    std::optional<int> m_tunTakeoverV6IfIndex;
};

} // namespace nrs
