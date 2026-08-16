cmake_minimum_required(VERSION 3.22)
project(OS2WarpInstaller LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

# macOS ARM64 target
set(CMAKE_OSX_ARCHITECTURES "arm64")
set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0")

find_package(Qt6 REQUIRED COMPONENTS
        Core Widgets Concurrent
)

qt_add_executable(OS2WarpInstaller
        main.cpp
        installer/InstallerWizard.cpp
        installer/pages/WelcomePage.cpp
        installer/pages/LicensePage.cpp
        installer/pages/DiskSetupPage.cpp
        installer/pages/ComponentsPage.cpp
        installer/pages/InstallProgressPage.cpp
        installer/pages/FinishPage.cpp
        installer/backend/InstallEngine.cpp
        installer/resources/assets.qrc
)

target_link_libraries(OS2WarpInstaller PRIVATE
        Qt6::Core
        Qt6::Widgets
        Qt6::Concurrent
)

# macOS bundle
set_target_properties(OS2WarpInstaller PROPERTIES
        MACOSX_BUNDLE TRUE
        MACOSX_BUNDLE_BUNDLE_NAME "OS/2 Warp 4.52 Installer"
        MACOSX_BUNDLE_BUNDLE_VERSION "4.52"
        MACOSX_BUNDLE_SHORT_VERSION_STRING "4.52"
)