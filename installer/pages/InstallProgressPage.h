#pragma once
#include <QWizardPage>
#include <QProgressBar>
#include <QLabel>
#include <QTextEdit>
#include <QtConcurrent>

class InstallProgressPage : public QWizardPage
{
    Q_OBJECT
public:
    explicit InstallProgressPage(QWidget *parent = nullptr);
    void initializePage() override;
    bool isComplete() const override { return m_done; }

private slots:
    void onProgress(int percent, const QString &status);
    void onInstallFinished(bool success);

private:
    QProgressBar *m_progressBar;
    QLabel       *m_statusLabel;
    QTextEdit    *m_logView;
    bool          m_done = false;
};