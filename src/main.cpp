#include "protocol/Protocol.h"
#include "ui/AppController.h"

#include <QByteArray>
#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QGuiApplication>
#include <QIcon>
#include <QJsonDocument>
#include <QJsonObject>
#include <QStandardPaths>
#include <QString>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <vector>

namespace {

void stderrMessageHandler(QtMsgType type, const QMessageLogContext& ctx, const QString& msg)
{
    Q_UNUSED(ctx);
    const char* tag = "info";
    switch (type) {
    case QtDebugMsg: tag = "debug"; break;
    case QtInfoMsg: tag = "info"; break;
    case QtWarningMsg: tag = "warn"; break;
    case QtCriticalMsg: tag = "crit"; break;
    case QtFatalMsg: tag = "fatal"; break;
    }
    const QByteArray utf8 = msg.toUtf8();
    std::fprintf(stderr, "[netrouteswitcher][%s] %s\n", tag, utf8.constData());
    std::fflush(stderr);
    if (type == QtFatalMsg) {
        std::abort();
    }
}

bool hasArg(int argc, char* argv[], const char* flag)
{
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], flag) == 0) {
            return true;
        }
    }
    return false;
}

QString argValue(int argc, char* argv[], const char* prefix)
{
    const std::size_t len = std::strlen(prefix);
    for (int i = 1; i < argc; ++i) {
        if (std::strncmp(argv[i], prefix, len) == 0) {
            return QString::fromUtf8(argv[i] + len);
        }
    }
    return {};
}

QString resolveShowDataDir(const QString& explicitDir)
{
    if (!explicitDir.trimmed().isEmpty()) {
        return QDir::cleanPath(explicitDir.trimmed());
    }

    std::vector<wchar_t> buf(MAX_PATH, L'\0');
    for (;;) {
        const DWORD len = ::GetModuleFileNameW(nullptr, buf.data(), static_cast<DWORD>(buf.size()));
        if (len == 0) {
            break;
        }
        if (len < buf.size() - 1) {
            const QString exePath = QString::fromWCharArray(buf.data(), static_cast<int>(len));
            const QDir exeDir = QFileInfo(exePath).dir();
            const QString folderName = exeDir.dirName();
            QDir parentDir = exeDir;
            if (!folderName.isEmpty() && parentDir.cdUp()) {
                return parentDir.filePath(folderName + QStringLiteral(".data"));
            }
            break;
        }
        if (buf.size() >= 32768) {
            break;
        }
        buf.resize(buf.size() * 2, L'\0');
    }

    const QString localAppData = qEnvironmentVariable("LOCALAPPDATA");
    if (!localAppData.trimmed().isEmpty()) {
        return QDir(localAppData).filePath(QStringLiteral("libo0118/NetRouteSwitcher"));
    }
    return QDir::current().filePath(QStringLiteral("data"));
}

int triggerExistingInstanceShow(const QString& explicitDir)
{
    const QString dataDir = resolveShowDataDir(explicitDir);
    if (!QDir().mkpath(dataDir)) {
        std::fprintf(stderr, "[netrouteswitcher][warn] Failed to create data dir for --show.\n");
        std::fflush(stderr);
        return 1;
    }

    ::AllowSetForegroundWindow(ASFW_ANY);

    const QString triggerPath = QDir(dataDir).filePath(QStringLiteral("open.trigger"));
    const QString tmpPath = QDir(dataDir).filePath(QStringLiteral("open.trigger.tmp"));
    QFile::remove(tmpPath);
    QFile tmpFile(tmpPath);
    if (tmpFile.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        const QByteArray payload = QByteArray::number(QDateTime::currentMSecsSinceEpoch()) + '\n';
        tmpFile.write(payload);
        tmpFile.flush();
        tmpFile.close();
        QFile::remove(triggerPath);
        if (QFile::rename(tmpPath, triggerPath)) {
            return 0;
        }
        QFile::remove(tmpPath);
    }

    QFile::remove(triggerPath);
    QFile directFile(triggerPath);
    if (directFile.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        directFile.write(QByteArray::number(QDateTime::currentMSecsSinceEpoch()) + '\n');
        directFile.flush();
        directFile.close();
        return 0;
    }

    std::fprintf(stderr, "[netrouteswitcher][warn] Failed to write open.trigger.\n");
    std::fflush(stderr);
    return 1;
}

} // namespace

int main(int argc, char* argv[])
{
    qInstallMessageHandler(stderrMessageHandler);

    QString initialDataDir = argValue(argc, argv, "--data-dir=");
    if (initialDataDir.isEmpty()) {
        initialDataDir = qEnvironmentVariable("WS_DATA_DIR");
    }

    if (hasArg(argc, argv, "--show")) {
        return triggerExistingInstanceShow(initialDataDir);
    }

    const bool standalone = hasArg(argc, argv, "--standalone");
    QByteArray pending;

    if (!standalone) {
        const auto firstObj = nrs::Protocol::readFirst(&pending);
        if (!firstObj) {
            std::fprintf(stderr, "[netrouteswitcher][fatal] Missing initialize message on stdin.\n");
            std::fflush(stderr);
            return 1;
        }
        const QJsonObject params = firstObj->value(QStringLiteral("params")).toObject();
        const QString renderer = params.value(QStringLiteral("appearance")).toObject()
            .value(QStringLiteral("renderer")).toString();
        if (renderer == QLatin1String("software")) {
            qputenv("QT_QUICK_BACKEND", "software");
        }
        if (initialDataDir.isEmpty()) {
            initialDataDir = params.value(QStringLiteral("dataDir")).toString();
        }
        QByteArray firstLine = QJsonDocument(*firstObj).toJson(QJsonDocument::Compact);
        firstLine.append('\n');
        pending.prepend(firstLine);
    }

    if (qEnvironmentVariableIsEmpty("QT_QUICK_BACKEND")) {
        qputenv("QT_QUICK_BACKEND", "software");
    }

    if (qEnvironmentVariableIsEmpty("QT_QUICK_CONTROLS_STYLE")) {
        qputenv("QT_QUICK_CONTROLS_STYLE", "Basic");
    }

    QGuiApplication app(argc, argv);
    QCoreApplication::setApplicationName(QStringLiteral("NetRouteSwitcher"));
    QCoreApplication::setOrganizationName(QStringLiteral("libo0118"));
    app.setWindowIcon(QIcon(QStringLiteral(":/nrs/icon.png")));
    app.setQuitOnLastWindowClosed(standalone);

    std::unique_ptr<nrs::Protocol> protocol;
    if (!standalone) {
        protocol = std::make_unique<nrs::Protocol>(std::move(pending));
    }
    if (initialDataDir.isEmpty()) {
        initialDataDir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    }

    nrs::AppController controller(protocol.get(), initialDataDir, standalone);
    if (protocol) {
        protocol->start();
    }
    return app.exec();
}
