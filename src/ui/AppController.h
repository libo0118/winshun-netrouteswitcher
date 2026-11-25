#pragma once

#include "core/RouteModels.h"
#include "platform/WinNetworkService.h"
#include "ui/AdapterListModel.h"
#include "ui/ThemeObject.h"
#include "ui/WifiRuleModel.h"

#include <QElapsedTimer>
#include <QJsonArray>
#include <QJsonObject>
#include <QJsonValue>
#include <QObject>
#include <QPointer>
#include <QString>
#include <QStringList>

#include <atomic>
#include <condition_variable>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <thread>

class QFileSystemWatcher;
class QQmlApplicationEngine;
class QQuickWindow;
class QTimer;

namespace nrs {

class Protocol;

class AppController : public QObject {
    Q_OBJECT
    Q_PROPERTY(nrs::ThemeObject* theme READ theme CONSTANT)
    Q_PROPERTY(nrs::AdapterListModel* internetModel READ internetModel CONSTANT)
    Q_PROPERTY(nrs::AdapterListModel* lanModel READ lanModel CONSTANT)
    Q_PROPERTY(nrs::AdapterListModel* disconnectedModel READ disconnectedModel CONSTANT)
    Q_PROPERTY(bool isElevated READ isElevated CONSTANT)
    Q_PROPERTY(bool busy READ busy NOTIFY busyChanged)
    Q_PROPERTY(QString activeV4Headline READ activeV4Headline NOTIFY snapshotChanged)
    Q_PROPERTY(QString activeV6Headline READ activeV6Headline NOTIFY snapshotChanged)
    Q_PROPERTY(bool hasSplitEgress READ hasSplitEgress NOTIFY snapshotChanged)
    Q_PROPERTY(QString splitEgressWarning READ splitEgressWarning NOTIFY snapshotChanged)
    Q_PROPERTY(bool hasTunTakeover READ hasTunTakeover NOTIFY snapshotChanged)
    Q_PROPERTY(QString tunTakeoverBadge READ tunTakeoverBadge NOTIFY snapshotChanged)
    Q_PROPERTY(int internetCount READ internetCount NOTIFY snapshotChanged)
    Q_PROPERTY(int lanCount READ lanCount NOTIFY snapshotChanged)
    Q_PROPERTY(int disconnectedCount READ disconnectedCount NOTIFY snapshotChanged)
    Q_PROPERTY(QString statusMessage READ statusMessage NOTIFY statusChanged)
    Q_PROPERTY(bool statusError READ statusError NOTIFY statusChanged)
    Q_PROPERTY(bool syncIpv6 READ syncIpv6 NOTIFY settingsChanged)
    Q_PROPERTY(bool showDisconnected READ showDisconnected NOTIFY settingsChanged)
    Q_PROPERTY(bool autoSwitchByWifi READ autoSwitchByWifi NOTIFY settingsChanged)
    Q_PROPERTY(QString connectedWifiSsid READ connectedWifiSsid NOTIFY snapshotChanged)
    Q_PROPERTY(nrs::WifiRuleModel* wifiRuleModel READ wifiRuleModel CONSTANT)
    Q_PROPERTY(int wifiRuleCount READ wifiRuleCount NOTIFY wifiRulesChanged)
    Q_PROPERTY(QStringList savedWifiProfiles READ savedWifiProfiles NOTIFY wifiProfilesChanged)
    Q_PROPERTY(QStringList knownAdapterNames READ knownAdapterNames NOTIFY knownAdapterNamesChanged)
    Q_PROPERTY(QStringList internetAdapterNames READ internetAdapterNames NOTIFY knownAdapterNamesChanged)
    Q_PROPERTY(bool standaloneMode READ standaloneMode CONSTANT)
    Q_PROPERTY(bool shuttingDown READ shuttingDown NOTIFY shuttingDownChanged)
    Q_PROPERTY(bool dialogOpen READ dialogOpen WRITE setDialogOpen NOTIFY dialogOpenChanged)

public:
    explicit AppController(
        Protocol* protocol,
        QString dataDir,
        bool standaloneMode,
        QObject* parent = nullptr);
    ~AppController() override;

    ThemeObject* theme() const { return m_theme; }
    AdapterListModel* internetModel() const { return m_internetModel; }
    AdapterListModel* lanModel() const { return m_lanModel; }
    AdapterListModel* disconnectedModel() const { return m_disconnectedModel; }

    bool isElevated() const { return m_isElevated; }
    bool busy() const { return m_pendingTasks > 0; }
    QString activeV4Headline() const { return m_activeV4Headline; }
    QString activeV6Headline() const { return m_activeV6Headline; }
    bool hasSplitEgress() const { return m_hasSplitEgress; }
    QString splitEgressWarning() const { return m_splitEgressWarning; }
    bool hasTunTakeover() const { return m_hasTunTakeover; }
    QString tunTakeoverBadge() const { return m_tunTakeoverBadge; }
    int internetCount() const { return m_internetModel->count(); }
    int lanCount() const { return m_lanModel->count(); }
    int disconnectedCount() const { return m_disconnectedModel->count(); }
    QString statusMessage() const { return m_statusMessage; }
    bool statusError() const { return m_statusError; }
    bool syncIpv6() const { return m_settings.syncIpv6; }
    bool showDisconnected() const { return m_settings.showDisconnected; }
    bool autoSwitchByWifi() const { return m_settings.autoSwitchByWifi; }
    QString connectedWifiSsid() const { return m_snapshot.connectedWifiSsid; }
    WifiRuleModel* wifiRuleModel() const { return m_wifiRuleModel; }
    int wifiRuleCount() const { return m_wifiRuleModel->count(); }
    QStringList savedWifiProfiles() const { return m_savedWifiProfiles; }
    QStringList knownAdapterNames() const;
    QStringList internetAdapterNames() const;
    bool standaloneMode() const { return m_standaloneMode; }
    bool shuttingDown() const { return m_shuttingDown; }
    bool dialogOpen() const { return m_dialogOpen; }
    Q_INVOKABLE void refresh();
    Q_INVOKABLE void setAsPrimary(int ifIndex);
    Q_INVOKABLE void moveUp(int ifIndex);
    Q_INVOKABLE void moveDown(int ifIndex);
    Q_INVOKABLE void setCustomMetric(int ifIndex, int metric);
    Q_INVOKABLE void restoreAdapterAuto(int ifIndex);
    Q_INVOKABLE void restoreAllAutomatic();
    Q_INVOKABLE void testConnectivity();
    Q_INVOKABLE void openNetworkConnections();
    Q_INVOKABLE void setSyncIpv6(bool enabled);
    Q_INVOKABLE void setShowDisconnected(bool enabled);
    Q_INVOKABLE void setAutoSwitchByWifi(bool enabled);
    Q_INVOKABLE void refreshWifiProfiles();
    Q_INVOKABLE void saveWifiRule(
        const QString& ruleId,
        const QString& ssid,
        const QStringList& adapterOrder,
        bool enabled,
        bool applyImmediately);
    Q_INVOKABLE void deleteWifiRule(const QString& ruleId);
    Q_INVOKABLE void setWifiRuleEnabled(const QString& ruleId, bool enabled);
    Q_INVOKABLE void moveWifiRule(const QString& ruleId, int delta);
    Q_INVOKABLE void applyWifiRuleNow(const QString& ruleId);
    Q_INVOKABLE void showWindow();
    Q_INVOKABLE void hideWindow();
    Q_INVOKABLE void setDialogOpen(bool open);

signals:
    void busyChanged();
    void snapshotChanged();
    void statusChanged();
    void settingsChanged();
    void shuttingDownChanged();
    void dialogOpenChanged();
    void wifiRulesChanged();
    void wifiProfilesChanged();
    void knownAdapterNamesChanged();
    void openWifiRulesRequested();

private:
    using CompletionFn = std::function<void()>;

    void startWorker();
    void stopWorker();
    void enqueueTask(std::function<void()> task, bool trackBusy = true);
    void beginTask();
    void endTask();
    void beginShutdown();
    bool ensureWindow();
    void releaseWindowResources();
    void startShowTriggerWatch();
    void stopShowTriggerWatch();
    void consumeShowTrigger();

    void applySnapshot(const RouteSnapshot& snapshot);
    void setStatus(const QString& message, bool isError);
    void loadFallbackSettings();
    void saveFallbackSettings() const;
    void loadWifiRules();
    void saveWifiRules() const;
    void pushSettingsUpdate();
    void ensureSnapshotForRpc();
    void evaluateWifiAutoSwitch(bool forceReevaluate = false);
    void preserveManualOverrideOnCurrentWifi(const RouteSnapshot& snapshot);
    void applyWifiRuleInternal(
        const WifiRouteRule& rule,
        const QList<AdapterInfo>& desired,
        bool isAutoSwitch,
        const QString& labelSsid);

    void performSetAsPrimary(int ifIndex, std::function<void(const OperationResult&)> done);
    void performMoveAdapter(int ifIndex, int delta);
    void performSetSingleMetric(int ifIndex, std::optional<int> metric);
    void performRestoreAllAutomatic(std::function<void(const OperationResult&)> done);
    void performTestConnectivity(std::function<void(const QString&)> done);

    void handleMessage(const QJsonValue& id, const QString& method, const QJsonObject& params, bool hasId);
    void handleInitialize(const QJsonValue& id, const QJsonObject& params, bool hasId);
    void handleQuery(const QJsonValue& id, const QJsonObject& params, bool hasId);
    void handleAction(const QJsonValue& id, const QJsonObject& params, bool hasId);
    void handleTrayMenu(const QJsonValue& id, bool hasId);
    void handleMenu(const QJsonObject& params);
    void handleSettingsChanged(const QJsonObject& params);
    void handleAppearance(const QJsonObject& params);
    void handleShutdown(const QJsonValue& id, bool hasId);

    QJsonArray buildQueryItems(const QString& text, int limit) const;

    QPointer<Protocol> m_protocol;
    QString m_pluginDir;
    QString m_dataDir;
    bool m_standaloneMode = false;
    bool m_isElevated = false;
    bool m_initialized = false;
    bool m_shuttingDown = false;
    bool m_dialogOpen = false;
    bool m_hasSnapshot = false;
    int m_pendingTasks = 0;

    ThemeObject* m_theme = nullptr;
    AdapterListModel* m_internetModel = nullptr;
    AdapterListModel* m_lanModel = nullptr;
    AdapterListModel* m_disconnectedModel = nullptr;
    WifiRuleModel* m_wifiRuleModel = nullptr;
    WinNetworkService m_networkService;
    std::unique_ptr<QQmlApplicationEngine> m_engine;
    QPointer<QQuickWindow> m_window;
    QTimer* m_watchDebounceTimer = nullptr;
    QTimer* m_wifiRetryTimer = nullptr;
    QTimer* m_uiReleaseTimer = nullptr;
    QFileSystemWatcher* m_triggerWatcher = nullptr;

    RouteSnapshot m_snapshot;
    PluginSettings m_settings;
    QList<WifiRouteRule> m_wifiRules;
    QStringList m_savedWifiProfiles;
    QString m_lastObservedWifiSsid;
    QString m_lastAutoEvaluatedSsid;
    int m_wifiRetryCount = 0;
    QString m_activeV4Headline = QStringLiteral("正在检测...");
    QString m_activeV6Headline = QStringLiteral("正在检测...");
    bool m_hasSplitEgress = false;
    QString m_splitEgressWarning;
    bool m_hasTunTakeover = false;
    QString m_tunTakeoverBadge;
    QString m_statusMessage = QStringLiteral("就绪");
    bool m_statusError = false;
    QElapsedTimer m_lastRefreshTimer;

    std::mutex m_queueMutex;
    std::condition_variable m_queueCv;
    std::deque<std::function<void()>> m_taskQueue;
    std::atomic<bool> m_workerStopping{false};
    std::thread m_workerThread;
};

} // namespace nrs
