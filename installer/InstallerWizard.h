#pragma once
#include <QWizard>
#include "backend/InstallEngine.h"

class InstallerWizard : public QWizard
{
    Q_OBJECT
public:
    enum PageId {
        Page_Welcome,
        Page_License,
        Page_DiskSetup,
        Page_Components,
        Page_Progress,
        Page_Finish
    };

    explicit InstallerWizard(QWidget *parent = nullptr);
    InstallEngine* engine() { return m_engine; }

private:
    InstallEngine *m_engine;
    void setupPages();
    void applyOS2Style();
};