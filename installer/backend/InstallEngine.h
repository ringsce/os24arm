#pragma once
#include <QObject>
#include <QStringList>

class InstallEngine : public QObject
{
    Q_OBJECT
public:
    explicit InstallEngine(QObject *parent = nullptr);

    // Call this to start - wraps your existing install logic
    void startInstall(const QString &targetDisk, const QStringList &components);

    signals:
        void progressChanged(int percent, const QString &statusMessage);
    void installFinished(bool success);

private:
    bool extractArchive(const QString &archive, const QString &dest);
    bool setupBootloader(const QString &disk);
    bool installKernel(const QString &dest);
    bool installComponents(const QStringList &components, const QString &dest);

    // Emit helpers (thread-safe via Qt::QueuedConnection)
    void reportProgress(int pct, const QString &msg);
};