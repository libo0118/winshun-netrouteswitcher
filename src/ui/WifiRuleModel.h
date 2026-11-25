#pragma once

#include "core/RouteModels.h"

#include <QAbstractListModel>
#include <QList>
#include <QString>
#include <QVariantMap>

namespace nrs {

class WifiRuleModel : public QAbstractListModel {
    Q_OBJECT
    Q_PROPERTY(int count READ count NOTIFY countChanged)

public:
    enum Roles {
        RuleIdRole = Qt::UserRole + 1,
        SsidRole,
        AdapterOrderRole,
        OrderSummaryRole,
        EnabledRole,
        IsMatchedRole,
    };
    Q_ENUM(Roles)

    explicit WifiRuleModel(QObject* parent = nullptr);

    int rowCount(const QModelIndex& parent = QModelIndex()) const override;
    QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override;
    QHash<int, QByteArray> roleNames() const override;

    int count() const { return static_cast<int>(m_rules.size()); }
    const QList<WifiRouteRule>& rules() const { return m_rules; }

    void setRules(const QList<WifiRouteRule>& rules, const QString& currentSsid);
    void setCurrentSsid(const QString& currentSsid);

    Q_INVOKABLE QVariantMap get(int row) const;

signals:
    void countChanged();

private:
    int computeMatchedRow() const;

    QList<WifiRouteRule> m_rules;
    QString m_currentSsid;
    int m_matchedRow = -1;
};

} // namespace nrs
