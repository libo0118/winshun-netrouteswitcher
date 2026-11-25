#include "ui/AppController.h"

#include "core/RouteLogic.h"
#include "protocol/Protocol.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileSystemWatcher>
#include <QGuiApplication>
#include <QIcon>
#include <QJsonDocument>
#include <QMetaObject>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickWindow>
#include <QRegularExpression>
#include <QSaveFile>
#include <QStandardPaths>
#include <QStyleHints>
#include <QTimer>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include <algorithm>
#include <utility>

namespace nrs {

namespace {

bool isWindowsPlatform()
{
    return QGuiApplication::platformName().compare(QLatin1String("windows"), Qt::CaseInsensitive) == 0;
}

void applyWindowChrome(QWindow* window, const ThemeData& theme)
{
    if (!window || !isWindowsPlatform()) {
        return;
    }
    const HWND hwnd = reinterpret_cast<HWND>(window->winId());
    if (!hwnd) {
        return;
    }
    using DwmSetAttrFn = HRESULT(WINAPI*)(HWND, DWORD, LPCVOID, DWORD);
    static const DwmSetAttrFn dwmSetAttr = []() -> DwmSetAttrFn {
        HMODULE mod = ::GetModuleHandleW(L"dwmapi.dll");
        if (!mod) {
            mod = ::LoadLibraryW(L"dwmapi.dll");
        }
        return mod ? reinterpret_cast<DwmSetAttrFn>(::GetProcAddress(mod, "DwmSetWindowAttribute")) : nullptr;
    }();
    if (!dwmSetAttr) {
        return;
    }
    const BOOL dark = theme.dark ? TRUE : FALSE;
    dwmSetAttr(hwnd, 20 /* DWMWA_USE_IMMERSIVE_DARK_MODE */, &dark, sizeof(dark));
    const DWORD corner = theme.rounded ? 2 /* DWMWCP_ROUND */ : 1 /* DWMWCP_DONOTROUND */;
    dwmSetAttr(hwnd, 33 /* DWMWA_WINDOW_CORNER_PREFERENCE */, &corner, sizeof(corner));
}

bool matchesKeywords(const QString& haystack, const QString& filterText)
{
    const QString trimmed = filterText.trimmed();
    if (trimmed.isEmpty()) {
        return true;
    }
    static const QRegularExpression kWhitespace(QStringLiteral("\\s+"));
    const QStringList tokens = trimmed.split(kWhitespace, Qt::SkipEmptyParts);
    for (const QString& token : tokens) {
        if (!haystack.contains(token, Qt::CaseInsensitive)) {
            return false;
        }
    }
    return true;
}

std::optional<int> parseSwitchIfIndex(const QString& id)
{
    if (!id.startsWith(QLatin1String("switch-"))) {
        return std::nullopt;
    }
    bool ok = false;
    const int val = id.mid(7).toInt(&ok);
    return (ok && val > 0) ? std::optional<int>(val) : std::nullopt;
}

ThemeData standaloneThemeFromSystem()
{
    const auto* hints = QGuiApplication::styleHints();
    const bool isDark = hints && hints->colorScheme() == Qt::ColorScheme::Dark;
    return isDark ? ThemeData{} : ThemeData::lightDefaults();
}

} // namespace

AppController::AppController(
    Protocol* protocol,
    QString dataDir,
    bool standaloneMode,
    QObject* parent)
    : QObject(parent)
    , m_protocol(protocol)
    , m_dataDir(std::move(dataDir))
    , m_standaloneMode(standaloneMode)
    , m_isElevated(WinNetworkService::isCurrentProcessElevated())
    , m_theme(new ThemeObject(this))
    , m_internetModel(new AdapterListModel(this))
    , m_lanModel(new AdapterListModel(this))
    , m_disconnectedModel(new AdapterListModel(this))
    , m_watchDebounceTimer(new QTimer(this))
{
    m_watchDebounceTimer->setSingleShot(true);
    m_watchDebounceTimer->setInterval(600);
    connect(m_watchDebounceTimer, &QTimer::timeout, this, [this]() {
        if (!m_shuttingDown && !busy()) {
            refresh();
        }
    });

    startWorker();

    if (m_protocol) {
        connect(m_protocol, &Protocol::requested, this,
            [this](const QJsonValue& id, const QString& method, const QJsonObject& params) {
                handleMessage(id, method, params, true);
            });
        connect(m_protocol, &Protocol::notified, this,
            [this](const QString& method, const QJsonObject& params) {
                handleMessage(QJsonValue{}, method, params, false);
            });
        connect(m_protocol, &Protocol::closed, this, [this]() {
            beginShutdown();
            QCoreApplication::quit();
        });
    } else if (m_standaloneMode) {
        if (m_dataDir.trimmed().isEmpty()) {
            m_dataDir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
        }
        m_theme->update(standaloneThemeFromSystem());
        if (auto* hints = QGuiApplication::styleHints()) {
            connect(hints, &QStyleHints::colorSchemeChanged, this, [this]() {
                m_theme->update(standaloneThemeFromSystem());
                if (m_window) {
                    applyWindowChrome(m_window.data(), m_theme->data());
                }
            });
        }
        loadFallbackSettings();
        emit settingsChanged();
        m_initialized = true;
        const QPointer<AppController> self(this);
        m_networkService.startWatching([self]() {
            if (!self) {
                return;
            }
            QMetaObject::invokeMethod(self.data(), [self]() {
                if (self && !self->m_shuttingDown && self->m_watchDebounceTimer) {
                    self->m_watchDebounceTimer->start();
                }
            }, Qt::QueuedConnection);
        });
        startShowTriggerWatch();
        refresh();
        showWindow();
    }
}

AppController::~AppController()
{
    beginShutdown();
}

void AppController::startWorker()
{
    m_workerStopping.store(false, std::memory_order_release);
    m_workerThread = std::thread([this]() {
        for (;;) {
            std::function<void()> task;
            {
                std::unique_lock<std::mutex> lock(m_queueMutex);
                m_queueCv.wait(lock, [this]() {
                    return m_workerStopping.load(std::memory_order_acquire) || !m_taskQueue.empty();
                });
                if (m_workerStopping.load(std::memory_order_acquire)) {
                    break;
                }
                task = std::move(m_taskQueue.front());
                m_taskQueue.pop_front();
            }
            if (task) {
                task();
            }
        }
    });
}

void AppController::stopWorker()
{
    m_workerStopping.store(true, std::memory_order_release);
    {
        std::lock_guard<std::mutex> lock(m_queueMutex);
        m_taskQueue.clear();
    }
    m_queueCv.notify_all();
    if (m_workerThread.joinable()) {
        m_workerThread.join();
    }
}

void AppController::enqueueTask(std::function<void()> task)
{
    if (m_shuttingDown || m_workerStopping.load(std::memory_order_acquire)) {
        return;
    }
    beginTask();
    {
        std::lock_guard<std::mutex> lock(m_queueMutex);
        m_taskQueue.push_back(std::move(task));
    }
    m_queueCv.notify_one();
}

void AppController::beginTask()
{
    const bool wasBusy = busy();
    ++m_pendingTasks;
    if (!wasBusy && busy()) {
        emit busyChanged();
    }
}

void AppController::endTask()
{
    if (m_pendingTasks > 0) {
        const bool wasBusy = busy();
        --m_pendingTasks;
        if (wasBusy && !busy()) {
            emit busyChanged();
        }
    }
}

void AppController::beginShutdown()
{
    if (!m_shuttingDown) {
        m_shuttingDown = true;
        emit shuttingDownChanged();
    }
    if (m_watchDebounceTimer && m_watchDebounceTimer->isActive()) {
        m_watchDebounceTimer->stop();
    }
    stopShowTriggerWatch();
    m_networkService.stopWatching();
    stopWorker();
    if (m_window) {
        m_window->hide();
    }
    m_engine.reset();
    m_window = nullptr;
}

bool AppController::ensureWindow()
{
    if (m_shuttingDown) {
        return false;
    }
    if (m_window) {
        return true;
    }
    QGuiApplication::setQuitOnLastWindowClosed(m_standaloneMode);
    m_engine = std::make_unique<QQmlApplicationEngine>(this);
    const QString hostQmlDir = qEnvironmentVariable("WS_QT_QML_DIR");
    if (!hostQmlDir.isEmpty()) {
        m_engine->addImportPath(hostQmlDir);
    }
    m_engine->rootContext()->setContextProperty(QStringLiteral("controller"), this);
    m_engine->rootContext()->setContextProperty(QStringLiteral("theme"), m_theme);
    m_engine->load(QUrl(QStringLiteral("qrc:/nrs/qml/MainWindow.qml")));
    if (m_engine->rootObjects().isEmpty()) {
        if (m_protocol) {
            m_protocol->log(QStringLiteral("error"), QStringLiteral("Failed to load main window resource"));
        }
        m_engine.reset();
        return false;
    }
    m_window = qobject_cast<QQuickWindow*>(m_engine->rootObjects().first());
    if (!m_window) {
        m_engine.reset();
        return false;
    }
    m_window->setIcon(QIcon(QStringLiteral(":/nrs/icon.png")));
    applyWindowChrome(m_window.data(), m_theme->data());
    return true;
}

void AppController::showWindow()
{
    if (!ensureWindow()) {
        return;
    }
    applyWindowChrome(m_window.data(), m_theme->data());
    m_window->showNormal();
    if (isWindowsPlatform()) {
        if (const HWND hwnd = reinterpret_cast<HWND>(m_window->winId())) {
            ::SetForegroundWindow(hwnd);
        }
        m_window->raise();
        m_window->requestActivate();
    }
    if (!m_hasSnapshot || !m_lastRefreshTimer.isValid() || m_lastRefreshTimer.elapsed() > 1200) {
        refresh();
    }
}

void AppController::hideWindow()
{
    if (m_dialogOpen) {
        setDialogOpen(false);
    }
    if (m_window) {
        m_window->hide();
    }
}

void AppController::setDialogOpen(bool open)
{
    if (m_dialogOpen == open) {
        return;
    }
    m_dialogOpen = open;
    emit dialogOpenChanged();
}

void AppController::startShowTriggerWatch()
{
    if (m_shuttingDown || m_dataDir.trimmed().isEmpty()) {
        return;
    }
    QDir().mkpath(m_dataDir);
    // Clear any stale trigger from a prior crash so startup stays silent.
    QFile::remove(QDir(m_dataDir).filePath(QStringLiteral("open.trigger")));

    if (!m_triggerWatcher) {
        m_triggerWatcher = new QFileSystemWatcher(this);
        connect(m_triggerWatcher, &QFileSystemWatcher::directoryChanged, this, [this]() {
            consumeShowTrigger();
        });
    }
    if (!m_triggerWatcher->directories().contains(m_dataDir)) {
        m_triggerWatcher->addPath(m_dataDir);
    }
}

void AppController::stopShowTriggerWatch()
{
    if (!m_triggerWatcher) {
        return;
    }
    const QStringList dirs = m_triggerWatcher->directories();
    if (!dirs.isEmpty()) {
        m_triggerWatcher->removePaths(dirs);
    }
}

void AppController::consumeShowTrigger()
{
    if (m_shuttingDown || m_dataDir.trimmed().isEmpty()) {
        return;
    }
    const QString triggerPath = QDir(m_dataDir).filePath(QStringLiteral("open.trigger"));
    if (!QFile::exists(triggerPath)) {
        return;
    }
    if (QFile::remove(triggerPath)) {
        showWindow();
    } else {
        QTimer::singleShot(30, this, &AppController::consumeShowTrigger);
    }
}

void AppController::applySnapshot(const RouteSnapshot& snapshot)
{
    m_snapshot = snapshot;
    m_hasSnapshot = true;
    m_lastRefreshTimer.restart();

    QList<AdapterInfo> internet;
    QList<AdapterInfo> lan;
    QList<AdapterInfo> disconnected;
    for (const AdapterInfo& adapter : snapshot.adapters) {
        switch (adapter.category) {
        case AdapterCategory::Internet:
            internet.append(adapter);
            break;
        case AdapterCategory::LanOrVirtual:
            lan.append(adapter);
            break;
        case AdapterCategory::Disconnected:
        default:
            disconnected.append(adapter);
            break;
        }
    }

    m_activeV4Headline = formatActiveV4Headline(m_snapshot);
    m_activeV6Headline = formatActiveV6Headline(m_snapshot);
    m_hasSplitEgress = m_snapshot.hasSplitEgress();
    m_splitEgressWarning = formatSplitEgressWarning(m_snapshot);

    m_internetModel->setAdapters(internet, snapshot.activeV4IfIndex, snapshot.activeV6IfIndex);
    m_lanModel->setAdapters(lan, snapshot.activeV4IfIndex, snapshot.activeV6IfIndex);
    m_disconnectedModel->setAdapters(disconnected, snapshot.activeV4IfIndex, snapshot.activeV6IfIndex);
    emit snapshotChanged();
}

void AppController::setStatus(const QString& message, bool isError)
{
    if (m_statusMessage == message && m_statusError == isError) {
        return;
    }
    m_statusMessage = message;
    m_statusError = isError;
    emit statusChanged();
}

void AppController::loadFallbackSettings()
{
    if (m_dataDir.trimmed().isEmpty()) {
        return;
    }
    QFile file(QDir(m_dataDir).filePath(QStringLiteral("settings.json")));
    if (!file.open(QIODevice::ReadOnly)) {
        return;
    }
    const QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
    if (doc.isObject()) {
        m_settings = PluginSettings::fromJson(doc.object());
    }
}

void AppController::saveFallbackSettings() const
{
    if (m_dataDir.trimmed().isEmpty()) {
        return;
    }
    QDir().mkpath(m_dataDir);
    QSaveFile file(QDir(m_dataDir).filePath(QStringLiteral("settings.json")));
    if (file.open(QIODevice::WriteOnly)) {
        file.write(QJsonDocument(m_settings.toJson()).toJson(QJsonDocument::Indented));
        file.commit();
    }
}

void AppController::pushSettingsUpdate()
{
    saveFallbackSettings();
    if (m_protocol) {
        m_protocol->sendSettingsUpdate(m_settings.toJson());
    }
}

void AppController::ensureSnapshotForRpc()
{
    if (!m_hasSnapshot || !m_lastRefreshTimer.isValid() || m_lastRefreshTimer.elapsed() > 1500) {
        applySnapshot(m_networkService.getSnapshot());
    }
}

void AppController::refresh()
{
    if (m_shuttingDown) {
        return;
    }
    const QPointer<AppController> self(this);
    enqueueTask([this, self]() {
        if (m_workerStopping.load(std::memory_order_acquire)) {
            return;
        }
        RouteSnapshot snap = m_networkService.getSnapshot();
        if (m_workerStopping.load(std::memory_order_acquire)) {
            return;
        }
        QMetaObject::invokeMethod(this, [self, snap = std::move(snap)]() {
            if (!self || self->m_shuttingDown) {
                return;
            }
            self->applySnapshot(snap);
            const QString timeStr = snap.capturedAtIso.size() >= 19
                ? snap.capturedAtIso.mid(11, 8)
                : QString();
            const QString msg = timeStr.isEmpty()
                ? QStringLiteral("已刷新 · 共 %1 个互联网出口网卡").arg(self->internetCount())
                : QStringLiteral("已刷新（%1）· 共 %2 个互联网出口网卡").arg(timeStr).arg(self->internetCount());
            self->setStatus(msg, false);
            self->endTask();
        }, Qt::QueuedConnection);
    });
}

void AppController::setAsPrimary(int ifIndex)
{
    performSetAsPrimary(ifIndex, {});
}

void AppController::moveUp(int ifIndex)
{
    performMoveAdapter(ifIndex, -1);
}

void AppController::moveDown(int ifIndex)
{
    performMoveAdapter(ifIndex, 1);
}

void AppController::setCustomMetric(int ifIndex, int metric)
{
    performSetSingleMetric(ifIndex, metric);
}

void AppController::restoreAdapterAuto(int ifIndex)
{
    performSetSingleMetric(ifIndex, std::nullopt);
}

void AppController::restoreAllAutomatic()
{
    performRestoreAllAutomatic({});
}

void AppController::testConnectivity()
{
    performTestConnectivity({});
}

void AppController::openNetworkConnections()
{
    if (!WinNetworkService::openNetworkConnections()) {
        setStatus(QStringLiteral("打开系统网络连接面板 (ncpa.cpl) 失败。"), true);
    }
}

void AppController::setSyncIpv6(bool enabled)
{
    if (m_settings.syncIpv6 == enabled) {
        return;
    }
    m_settings.syncIpv6 = enabled;
    emit settingsChanged();
    pushSettingsUpdate();
}

void AppController::setShowDisconnected(bool enabled)
{
    if (m_settings.showDisconnected == enabled) {
        return;
    }
    m_settings.showDisconnected = enabled;
    emit settingsChanged();
    pushSettingsUpdate();
}

void AppController::performSetAsPrimary(int ifIndex, std::function<void(const OperationResult&)> done)
{
    if (m_shuttingDown) {
        return;
    }
    setStatus(QStringLiteral("正在设置首选互联网出口..."), false);
    QList<AdapterInfo> initialOrder = m_internetModel->adapters();
    const bool syncIpv6 = m_settings.syncIpv6;

    const QPointer<AppController> self(this);
    enqueueTask([this, self, ifIndex, syncIpv6, ordered = std::move(initialOrder), done = std::move(done)]() mutable {
        if (m_workerStopping.load(std::memory_order_acquire)) {
            return;
        }
        auto findIndex = [ifIndex](const QList<AdapterInfo>& list) -> int {
            for (int i = 0; i < list.size(); ++i) {
                if (list.at(i).ifIndex == ifIndex) {
                    return i;
                }
            }
            return -1;
        };

        int idx = findIndex(ordered);
        if (idx < 0) {
            ordered.clear();
            const RouteSnapshot fresh = m_networkService.getSnapshot();
            for (const AdapterInfo& a : fresh.adapters) {
                if (a.category == AdapterCategory::Internet) {
                    ordered.append(a);
                }
            }
            idx = findIndex(ordered);
        }

        OperationResult res;
        QString targetName;
        if (idx >= 0) {
            targetName = ordered.at(idx).name;
            if (idx > 0) {
                ordered.move(idx, 0);
            }
            res = m_networkService.applyPriorityOrder(ordered, syncIpv6);
        } else {
            res = m_networkService.setSingleAdapterMetric(ifIndex, 10, syncIpv6);
        }

        RouteSnapshot updated = m_networkService.getSnapshot();
        QString msg = res.message;
        if (res.ok && !targetName.isEmpty()) {
            msg = QStringLiteral("已将 [%1] 设为首选互联网出口 · %2").arg(targetName, res.message);
        }

        if (m_workerStopping.load(std::memory_order_acquire)) {
            return;
        }
        QMetaObject::invokeMethod(this, [self, updated = std::move(updated), ok = res.ok, errCode = res.win32Error, msg, done = std::move(done)]() {
            if (!self || self->m_shuttingDown) {
                return;
            }
            self->applySnapshot(updated);
            self->setStatus(msg, !ok);
            if (done) {
                done(OperationResult{ok, msg, errCode});
            }
            self->endTask();
        }, Qt::QueuedConnection);
    });
}

void AppController::performMoveAdapter(int ifIndex, int delta)
{
    if (m_shuttingDown) {
        return;
    }
    QList<AdapterInfo> ordered = m_internetModel->adapters();
    int idx = -1;
    for (int i = 0; i < ordered.size(); ++i) {
        if (ordered.at(i).ifIndex == ifIndex) {
            idx = i;
            break;
        }
    }
    const int newIdx = idx + delta;
    if (idx < 0 || newIdx < 0 || newIdx >= ordered.size()) {
        return;
    }

    const QString adapterName = ordered.at(idx).name;
    ordered.move(idx, newIdx);
    const bool syncIpv6 = m_settings.syncIpv6;
    setStatus(QStringLiteral("正在调整 [%1] 至第 %2 顺位...").arg(adapterName).arg(newIdx + 1), false);

    const QPointer<AppController> self(this);
    enqueueTask([this, self, ordered = std::move(ordered), syncIpv6, adapterName, rank = newIdx + 1]() {
        if (m_workerStopping.load(std::memory_order_acquire)) {
            return;
        }
        const OperationResult res = m_networkService.applyPriorityOrder(ordered, syncIpv6);
        RouteSnapshot updated = m_networkService.getSnapshot();
        const QString msg = res.ok
            ? QStringLiteral("已调整 [%1] 至第 %2 顺位 · %3").arg(adapterName).arg(rank).arg(res.message)
            : res.message;

        if (m_workerStopping.load(std::memory_order_acquire)) {
            return;
        }
        QMetaObject::invokeMethod(this, [self, updated = std::move(updated), ok = res.ok, msg]() {
            if (!self || self->m_shuttingDown) {
                return;
            }
            self->applySnapshot(updated);
            self->setStatus(msg, !ok);
            self->endTask();
        }, Qt::QueuedConnection);
    });
}

void AppController::performSetSingleMetric(int ifIndex, std::optional<int> metric)
{
    if (m_shuttingDown) {
        return;
    }
    const bool syncIpv6 = m_settings.syncIpv6;
    setStatus(metric.has_value()
        ? QStringLiteral("正在设置接口 #%1 跃点数为 %2...").arg(ifIndex).arg(*metric)
        : QStringLiteral("正在恢复接口 #%1 自动跃点...").arg(ifIndex), false);

    const QPointer<AppController> self(this);
    enqueueTask([this, self, ifIndex, metric, syncIpv6]() {
        if (m_workerStopping.load(std::memory_order_acquire)) {
            return;
        }
        const OperationResult res = m_networkService.setSingleAdapterMetric(ifIndex, metric, syncIpv6);
        RouteSnapshot updated = m_networkService.getSnapshot();
        if (m_workerStopping.load(std::memory_order_acquire)) {
            return;
        }
        QMetaObject::invokeMethod(this, [self, updated = std::move(updated), res]() {
            if (!self || self->m_shuttingDown) {
                return;
            }
            self->applySnapshot(updated);
            self->setStatus(res.message, !res.ok);
            self->endTask();
        }, Qt::QueuedConnection);
    });
}

void AppController::performRestoreAllAutomatic(std::function<void(const OperationResult&)> done)
{
    if (m_shuttingDown) {
        return;
    }
    setStatus(QStringLiteral("正在恢复所有受管理网卡为系统自动跃点..."), false);
    const bool hasSnap = m_hasSnapshot;
    RouteSnapshot snapCopy = m_snapshot;

    const QPointer<AppController> self(this);
    enqueueTask([this, self, hasSnap, snapCopy = std::move(snapCopy), done = std::move(done)]() mutable {
        if (m_workerStopping.load(std::memory_order_acquire)) {
            return;
        }
        if (!hasSnap || snapCopy.adapters.isEmpty()) {
            snapCopy = m_networkService.getSnapshot();
        }
        const OperationResult res = m_networkService.restoreAllAutomatic(snapCopy);
        RouteSnapshot updated = m_networkService.getSnapshot();
        if (m_workerStopping.load(std::memory_order_acquire)) {
            return;
        }
        QMetaObject::invokeMethod(this, [self, updated = std::move(updated), res, done = std::move(done)]() {
            if (!self || self->m_shuttingDown) {
                return;
            }
            self->applySnapshot(updated);
            self->setStatus(res.message, !res.ok);
            if (done) {
                done(res);
            }
            self->endTask();
        }, Qt::QueuedConnection);
    });
}

void AppController::performTestConnectivity(std::function<void(const QString&)> done)
{
    if (m_shuttingDown) {
        return;
    }
    setStatus(QStringLiteral("正在测试当前出口网关与公网连通性..."), false);
    const bool hasSnap = m_hasSnapshot;
    RouteSnapshot snapCopy = m_snapshot;

    const QPointer<AppController> self(this);
    enqueueTask([this, self, hasSnap, snapCopy = std::move(snapCopy), done = std::move(done)]() mutable {
        if (m_workerStopping.load(std::memory_order_acquire)) {
            return;
        }
        if (!hasSnap || snapCopy.adapters.isEmpty()) {
            snapCopy = m_networkService.getSnapshot();
        }
        const QString result = m_networkService.testConnectivity(snapCopy);
        if (m_workerStopping.load(std::memory_order_acquire)) {
            return;
        }
        QMetaObject::invokeMethod(this, [self, result, done = std::move(done)]() {
            if (!self || self->m_shuttingDown) {
                return;
            }
            self->setStatus(result, false);
            if (done) {
                done(result);
            }
            self->endTask();
        }, Qt::QueuedConnection);
    });
}

QJsonArray AppController::buildQueryItems(const QString& text, int limit) const
{
    QJsonArray items;
    auto pushIfMatch = [&](const QJsonObject& obj, const QString& haystack) {
        if (items.size() >= limit) {
            return;
        }
        if (matchesKeywords(haystack, text)) {
            items.append(obj);
        }
    };

    const QString panelSub = QStringLiteral("IPv4: %1 · IPv6: %2").arg(m_activeV4Headline, m_activeV6Headline);
    pushIfMatch(
        QJsonObject{
            {QStringLiteral("id"), QStringLiteral("open-panel")},
            {QStringLiteral("title"), QStringLiteral("打开多网卡出口切换器")},
            {QStringLiteral("subtitle"), panelSub},
            {QStringLiteral("icon"), QStringLiteral("ph:arrows-left-right")},
            {QStringLiteral("score"), 100},
            {QStringLiteral("actions"), QJsonArray{
                QJsonObject{
                    {QStringLiteral("id"), QStringLiteral("open-panel")},
                    {QStringLiteral("title"), QStringLiteral("打开管理界面")},
                    {QStringLiteral("key"), QStringLiteral("Enter")},
                },
            }},
        },
        QStringLiteral("打开多网卡出口切换器 管理界面 面板 panel route net 网卡 路由 出口 ") + panelSub);

    int inetRank = 0;
    for (const AdapterInfo& a : m_snapshot.adapters) {
        if (a.category != AdapterCategory::Internet) {
            continue;
        }
        if (items.size() >= limit) {
            break;
        }
        const bool isActiveV4 = m_snapshot.activeV4IfIndex.has_value() && *m_snapshot.activeV4IfIndex == a.ifIndex;
        const bool match = text.isEmpty()
            || matchesFilter(a, text)
            || (isActiveV4 && QStringLiteral("当前首选").contains(text, Qt::CaseInsensitive));
        if (!match) {
            ++inetRank;
            continue;
        }
        const QString gw = a.ipv4Gateways.isEmpty() ? QStringLiteral("无网关") : a.ipv4Gateways.first();
        const QString title = isActiveV4
            ? QStringLiteral("当前首选：%1").arg(a.name)
            : QStringLiteral("设为首选：%1").arg(a.name);
        const QString sub = QStringLiteral("%1 · 跃点 %2 · 速率 %3")
            .arg(gw)
            .arg(a.ipv4InterfaceMetric)
            .arg(formatLinkSpeed(a.speedBps));
        const QString icon = a.kind == AdapterKind::Wifi
            ? QStringLiteral("ph:wifi-high")
            : QStringLiteral("ph:plugs-connected");

        items.append(QJsonObject{
            {QStringLiteral("id"), QStringLiteral("switch-%1").arg(a.ifIndex)},
            {QStringLiteral("title"), title},
            {QStringLiteral("subtitle"), sub},
            {QStringLiteral("icon"), icon},
            {QStringLiteral("score"), qMax(10, 90 - inetRank)},
            {QStringLiteral("actions"), QJsonArray{
                QJsonObject{
                    {QStringLiteral("id"), QStringLiteral("default")},
                    {QStringLiteral("title"), QStringLiteral("设为首选出口")},
                    {QStringLiteral("key"), QStringLiteral("Enter")},
                },
                QJsonObject{
                    {QStringLiteral("id"), QStringLiteral("open-panel")},
                    {QStringLiteral("title"), QStringLiteral("打开管理界面")},
                    {QStringLiteral("key"), QStringLiteral("Shift+Enter")},
                },
            }},
        });
        ++inetRank;
    }

    const QString testSub = QStringLiteral("测试当前默认出口网关与公网 (223.5.5.5) 延迟 · %1").arg(m_activeV4Headline);
    pushIfMatch(
        QJsonObject{
            {QStringLiteral("id"), QStringLiteral("test-connectivity")},
            {QStringLiteral("title"), QStringLiteral("出口连通性测速")},
            {QStringLiteral("subtitle"), testSub},
            {QStringLiteral("icon"), QStringLiteral("ph:gauge")},
            {QStringLiteral("score"), 30},
        },
        QStringLiteral("出口连通性测速 测速 ping latency test 延迟 探测 公网 ") + testSub);

    pushIfMatch(
        QJsonObject{
            {QStringLiteral("id"), QStringLiteral("restore-auto")},
            {QStringLiteral("title"), QStringLiteral("恢复系统自动跃点")},
            {QStringLiteral("subtitle"), QStringLiteral("将所有受管理网卡恢复为 Windows 自动跃点")},
            {QStringLiteral("icon"), QStringLiteral("ph:arrow-counter-clockwise")},
            {QStringLiteral("score"), 20},
        },
        QStringLiteral("恢复系统自动跃点 自动跃点 restore auto metric 重置"));

    pushIfMatch(
        QJsonObject{
            {QStringLiteral("id"), QStringLiteral("open-ncpa")},
            {QStringLiteral("title"), QStringLiteral("打开网络连接 (ncpa.cpl)")},
            {QStringLiteral("subtitle"), QStringLiteral("打开 Windows 控制面板网络适配器设置")},
            {QStringLiteral("icon"), QStringLiteral("ph:sliders-horizontal")},
            {QStringLiteral("score"), 10},
        },
        QStringLiteral("打开网络连接 ncpa.cpl 适配器 控制面板"));

    return items;
}

void AppController::handleMessage(const QJsonValue& id, const QString& method, const QJsonObject& params, bool hasId)
{
    if (method == QLatin1String("initialize")) {
        handleInitialize(id, params, hasId);
    } else if (method == QLatin1String("query")) {
        handleQuery(id, params, hasId);
    } else if (method == QLatin1String("action")) {
        handleAction(id, params, hasId);
    } else if (method == QLatin1String("trayMenu")) {
        handleTrayMenu(id, hasId);
    } else if (method == QLatin1String("menu")) {
        handleMenu(params);
    } else if (method == QLatin1String("showSettings")) {
        if (hasId && m_protocol) {
            m_protocol->respond(id, QJsonObject{});
        }
        showWindow();
    } else if (method == QLatin1String("settingsChanged")) {
        handleSettingsChanged(params);
        if (hasId && m_protocol) {
            m_protocol->respond(id, QJsonObject{});
        }
    } else if (method == QLatin1String("appearance")) {
        handleAppearance(params);
        if (hasId && m_protocol) {
            m_protocol->respond(id, QJsonObject{});
        }
    } else if (method == QLatin1String("cancel")) {
        if (hasId && m_protocol) {
            m_protocol->respond(id, QJsonObject{});
        }
    } else if (method == QLatin1String("shutdown")) {
        handleShutdown(id, hasId);
    } else if (hasId && m_protocol) {
        m_protocol->respondError(id, -32601, QStringLiteral("Method not found: %1").arg(method));
    }
}

void AppController::handleInitialize(const QJsonValue& id, const QJsonObject& params, bool hasId)
{
    m_pluginDir = params.value(QStringLiteral("pluginDir")).toString();
    const QString dataDir = params.value(QStringLiteral("dataDir")).toString();
    if (!dataDir.trimmed().isEmpty()) {
        m_dataDir = dataDir.trimmed();
    }
    if (m_dataDir.trimmed().isEmpty()) {
        m_dataDir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    }

    loadFallbackSettings();
    if (params.contains(QStringLiteral("settings")) && params.value(QStringLiteral("settings")).isObject()) {
        m_settings = PluginSettings::fromJson(params.value(QStringLiteral("settings")).toObject());
        saveFallbackSettings();
    }

    const QString locale = params.value(QStringLiteral("locale")).toString(QStringLiteral("zh"));
    const QString themeStr = params.value(QStringLiteral("theme")).toString(QStringLiteral("dark"));
    const QJsonObject appearance = params.value(QStringLiteral("appearance")).toObject();
    m_theme->update(ThemeData::fromAppearance(locale, themeStr, appearance));
    emit settingsChanged();

    m_initialized = true;

    if (hasId && m_protocol) {
        m_protocol->respond(id, QJsonObject{
            {QStringLiteral("protocol"), 1},
            {QStringLiteral("protocolVersion"), QStringLiteral("1")},
            {QStringLiteral("name"), QStringLiteral("多网卡出口切换")},
        });
    }

    const QPointer<AppController> self(this);
    m_networkService.startWatching([self]() {
        if (!self) {
            return;
        }
        QMetaObject::invokeMethod(self.data(), [self]() {
            if (self && !self->m_shuttingDown && self->m_watchDebounceTimer) {
                self->m_watchDebounceTimer->start();
            }
        }, Qt::QueuedConnection);
    });
    startShowTriggerWatch();
    refresh();
}

void AppController::handleQuery(const QJsonValue& id, const QJsonObject& params, bool hasId)
{
    if (!hasId || !m_protocol) {
        return;
    }
    ensureSnapshotForRpc();
    const QString text = params.value(QStringLiteral("text")).toString().trimmed();
    const int limit = qBound(1, params.value(QStringLiteral("limit")).toInt(30), 200);
    const QJsonArray items = buildQueryItems(text, limit);
    m_protocol->respond(id, QJsonObject{
        {QStringLiteral("items"), items},
        {QStringLiteral("rerun"), 0},
        {QStringLiteral("cache"), 0},
    });
}

void AppController::handleAction(const QJsonValue& id, const QJsonObject& params, bool hasId)
{
    auto replyEffects = [this, id, hasId](const QJsonArray& effects) {
        if (hasId && m_protocol) {
            m_protocol->respond(id, QJsonObject{{QStringLiteral("effects"), effects}});
        }
    };
    auto notifyEffect = [](const QString& text) {
        return QJsonObject{{QStringLiteral("type"), QStringLiteral("notify")}, {QStringLiteral("text"), text}};
    };
    auto hideEffect = []() {
        return QJsonObject{{QStringLiteral("type"), QStringLiteral("hide")}};
    };

    const QString act = params.value(QStringLiteral("action")).toString();
    const QString itemId = params.value(QStringLiteral("item")).toObject().value(QStringLiteral("id")).toString();

    if (act == QLatin1String("open-panel") || itemId == QLatin1String("open-panel")) {
        QMetaObject::invokeMethod(this, [this]() { showWindow(); }, Qt::QueuedConnection);
        replyEffects({hideEffect()});
        return;
    }

    if (const auto ifIdx = parseSwitchIfIndex(itemId)) {
        performSetAsPrimary(*ifIdx, [replyEffects, notifyEffect, hideEffect](const OperationResult& res) {
            replyEffects({notifyEffect(res.message), hideEffect()});
        });
        return;
    }

    if (itemId == QLatin1String("restore-auto")) {
        performRestoreAllAutomatic([replyEffects, notifyEffect, hideEffect](const OperationResult& res) {
            replyEffects({notifyEffect(res.message), hideEffect()});
        });
        return;
    }

    if (itemId == QLatin1String("test-connectivity")) {
        replyEffects({notifyEffect(QStringLiteral("正在测试当前出口连通性…")), hideEffect()});
        performTestConnectivity([this](const QString& result) {
            if (m_protocol && !result.isEmpty()) {
                m_protocol->notifyUser(result);
            }
        });
        return;
    }

    if (itemId == QLatin1String("open-ncpa")) {
        if (WinNetworkService::openNetworkConnections()) {
            replyEffects({hideEffect()});
        } else {
            replyEffects({notifyEffect(QStringLiteral("打开系统网络连接面板 (ncpa.cpl) 失败")), hideEffect()});
        }
        return;
    }

    replyEffects({notifyEffect(QStringLiteral("未知操作项：%1").arg(itemId)), hideEffect()});
}

void AppController::handleTrayMenu(const QJsonValue& id, bool hasId)
{
    if (!hasId || !m_protocol) {
        return;
    }

    const AdapterInfo* activeV4 = m_snapshot.activeV4Adapter();
    const QString gw = (activeV4 && !activeV4->ipv4Gateways.isEmpty())
        ? activeV4->ipv4Gateways.first()
        : QStringLiteral("-");
    const QString headerTitle = activeV4
        ? QStringLiteral("当前出口: %1 (%2)").arg(activeV4->name, gw)
        : QStringLiteral("当前出口: 无可用公网连接");

    QJsonArray items;
    items.append(QJsonObject{
        {QStringLiteral("title"), headerTitle},
        {QStringLiteral("enabled"), false},
    });
    items.append(QJsonObject{});

    int inetCount = 0;
    for (const AdapterInfo& a : m_snapshot.adapters) {
        if (a.category != AdapterCategory::Internet) {
            continue;
        }
        ++inetCount;
        const bool isActive = m_snapshot.activeV4IfIndex.has_value() && *m_snapshot.activeV4IfIndex == a.ifIndex;
        items.append(QJsonObject{
            {QStringLiteral("id"), QStringLiteral("switch-%1").arg(a.ifIndex)},
            {QStringLiteral("title"), QStringLiteral("设为首选: %1 (跃点 %2)").arg(a.name).arg(a.ipv4InterfaceMetric)},
            {QStringLiteral("checked"), isActive},
        });
    }
    if (inetCount == 0) {
        items.append(QJsonObject{
            {QStringLiteral("title"), QStringLiteral("未检测到互联网出口网卡")},
            {QStringLiteral("enabled"), false},
        });
    }

    items.append(QJsonObject{});
    items.append(QJsonObject{
        {QStringLiteral("id"), QStringLiteral("test")},
        {QStringLiteral("title"), QStringLiteral("出口测速")},
    });
    items.append(QJsonObject{
        {QStringLiteral("id"), QStringLiteral("restore-auto")},
        {QStringLiteral("title"), QStringLiteral("恢复系统自动跃点")},
    });
    items.append(QJsonObject{
        {QStringLiteral("id"), QStringLiteral("open")},
        {QStringLiteral("title"), QStringLiteral("打开多网卡出口切换器")},
    });

    m_protocol->respond(id, QJsonObject{{QStringLiteral("items"), items}});
}

void AppController::handleMenu(const QJsonObject& params)
{
    const QString menuId = params.value(QStringLiteral("id")).toString();
    if (menuId == QLatin1String("open") || menuId == QLatin1String("open-panel")) {
        showWindow();
        return;
    }
    if (menuId == QLatin1String("test") || menuId == QLatin1String("test-connectivity")) {
        performTestConnectivity([this](const QString& result) {
            if (m_protocol && !result.isEmpty()) {
                m_protocol->notifyUser(result);
            }
        });
        return;
    }
    if (menuId == QLatin1String("restore-auto")) {
        performRestoreAllAutomatic([this](const OperationResult& res) {
            if (m_protocol && !res.message.isEmpty()) {
                m_protocol->notifyUser(res.message);
            }
        });
        return;
    }
    if (const auto ifIdx = parseSwitchIfIndex(menuId)) {
        performSetAsPrimary(*ifIdx, [this](const OperationResult& res) {
            if (m_protocol && !res.message.isEmpty()) {
                m_protocol->notifyUser(res.message);
            }
        });
    }
}

void AppController::handleSettingsChanged(const QJsonObject& params)
{
    const QJsonObject obj = params.contains(QStringLiteral("settings"))
        ? params.value(QStringLiteral("settings")).toObject()
        : params;
    m_settings = PluginSettings::fromJson(obj);
    saveFallbackSettings();
    emit settingsChanged();
}

void AppController::handleAppearance(const QJsonObject& params)
{
    const QString locale = params.value(QStringLiteral("locale")).toString();
    const QString themeStr = params.value(QStringLiteral("theme")).toString();
    m_theme->update(ThemeData::fromAppearance(locale, themeStr, params));
    if (m_window) {
        applyWindowChrome(m_window.data(), m_theme->data());
    }
}

void AppController::handleShutdown(const QJsonValue& id, bool hasId)
{
    beginShutdown();
    if (hasId && m_protocol) {
        m_protocol->respond(id, QJsonObject{});
    }
    QCoreApplication::quit();
}

} // namespace nrs
