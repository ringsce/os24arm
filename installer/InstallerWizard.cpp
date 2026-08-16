#include "InstallerWizard.h"
#include "pages/WelcomePage.h"
#include "pages/LicensePage.h"
#include "pages/DiskSetupPage.h"
#include "pages/ComponentsPage.h"
#include "pages/InstallProgressPage.h"
#include "pages/FinishPage.h"

InstallerWizard::InstallerWizard(QWidget *parent)
    : QWizard(parent), m_engine(new InstallEngine(this))
{
    setWindowTitle("OS/2 Warp 4.52 Installation - ARM64/macOS");
    setWizardStyle(QWizard::ModernStyle);
    setMinimumSize(800, 600);
    setPixmap(QWizard::LogoPixmap, QPixmap(":/os2logo.png").scaled(80, 80, Qt::KeepAspectRatio));

    setupPages();
    applyOS2Style();
}

void InstallerWizard::setupPages()
{
    setPage(Page_Welcome,    new WelcomePage(this));
    setPage(Page_License,    new LicensePage(this));
    setPage(Page_DiskSetup,  new DiskSetupPage(this));
    setPage(Page_Components, new ComponentsPage(this));
    setPage(Page_Progress,   new InstallProgressPage(this));
    setPage(Page_Finish,     new FinishPage(this));
    setStartId(Page_Welcome);
}

void InstallerWizard::applyOS2Style()
{
    // OS/2 Warp classic colors: teal/cyan header
    QPalette pal = palette();
    pal.setColor(QPalette::Highlight, QColor(0, 128, 128));   // OS/2 teal
    pal.setColor(QPalette::Window,    QColor(204, 204, 204)); // Classic grey
    setPalette(pal);
}