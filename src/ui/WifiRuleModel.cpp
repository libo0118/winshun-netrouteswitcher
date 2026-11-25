#include "ui/WifiRuleModel.h"

#include "core/RouteLogic.h"

namespace nrs {

WifiRuleModel::WifiRuleModel(QObject* parent)
    : QAbstractListModel(parent)
{
}

int WifiRuleModel::rowCount(const QModelIndex& parent) const
{
    return parent.isValid() ? 0 : static_cast<int>(m_rules.size());
}

QVariant WifiRuleModel::data(const QModelIndex& index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_rules.size()) {
        return {};
    }

    const int row = index.row();
    const WifiRouteRule& rule = m_rules.at(row);

    switch (role) {
    case RuleIdRole:
        return rule.id;
    case SsidRole:
    case Qt::DisplayRole:
        return rule.ssid;
    case AdapterOrderRole:
        return rule.adapterOrder;
    case OrderSummaryRole:
        return formatWifiRuleOrderSummary(rule.adapterOrder);
    case EnabledRole:
        return rule.enabled;
    case IsMatchedRole:
        return row == m_matchedRow;
    default:
        return {};
    }
}

QHash<int, QByteArray> WifiRuleModel::roleNames() const
{
    return {
        {RuleIdRole, "ruleId"},
        {SsidRole, "ssid"},
        {AdapterOrderRole, "adapterOrder"},
        {OrderSummaryRole, "orderSummary"},
        {EnabledRole, "enabled"},
        {IsMatchedRole, "isMatched"},
    };
}

void WifiRuleModel::setRules(const QList<WifiRouteRule>& rules, const QString& currentSsid)
{
    beginResetModel();
    m_rules = rules;
    m_currentSsid = currentSsid.trimmed();
    m_matchedRow = computeMatchedRow();
    endResetModel();
    emit countChanged();
}

void WifiRuleModel::setCurrentSsid(const QString& currentSsid)
{
    const QString trimmed = currentSsid.trimmed();
    const int prevMatched = m_matchedRow;
    m_currentSsid = trimmed;
    m_matchedRow = computeMatchedRow();
    if (prevMatched != m_matchedRow && !m_rules.isEmpty()) {
        emit dataChanged(
            index(0),
            index(static_cast<int>(m_rules.size()) - 1),
            {IsMatchedRole});
    }
}

QVariantMap WifiRuleModel::get(int row) const
{
    if (row < 0 || row >= m_rules.size()) {
        return {};
    }
    const WifiRouteRule& rule = m_rules.at(row);
    return {
        {QStringLiteral("ruleId"), rule.id},
        {QStringLiteral("ssid"), rule.ssid},
        {QStringLiteral("adapterOrder"), rule.adapterOrder},
        {QStringLiteral("orderSummary"), formatWifiRuleOrderSummary(rule.adapterOrder)},
        {QStringLiteral("enabled"), rule.enabled},
        {QStringLiteral("isMatched"), row == m_matchedRow},
    };
}

int WifiRuleModel::computeMatchedRow() const
{
    if (m_currentSsid.isEmpty() || m_rules.isEmpty()) {
        return -1;
    }
    const std::optional<WifiRouteRule> matched = findMatchingWifiRule(m_rules, m_currentSsid);
    if (!matched.has_value()) {
        return -1;
    }
    for (int i = 0; i < m_rules.size(); ++i) {
        const WifiRouteRule& rule = m_rules.at(i);
        if (!matched->id.isEmpty() && rule.id == matched->id) {
            return i;
        }
    }
    for (int i = 0; i < m_rules.size(); ++i) {
        const WifiRouteRule& rule = m_rules.at(i);
        if (rule.enabled && rule.ssid == matched->ssid) {
            return i;
        }
    }
    return -1;
}

} // namespace nrs
