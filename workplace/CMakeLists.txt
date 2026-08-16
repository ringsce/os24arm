# workplace/CMakeLists.txt

project(workplace LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Qt cross-compiled static build root
set(QT_ROOT ${CMAKE_SOURCE_DIR}/third_party/qt6-aarch64)

include_directories(
        ${QT_ROOT}/include
        ${QT_ROOT}/include/QtCore
        ${QT_ROOT}/include/QtGui
        ${QT_ROOT}/include/QtWidgets
)

file(GLOB WORKPLACE_SRC
        desktop/*.cpp
        qpa/*.cpp
)

add_library(workplace STATIC ${WORKPLACE_SRC})

target_compile_options(workplace PRIVATE
        -fno-exceptions
        -fno-rtti
)

target_link_libraries(workplace
        Qt6Core
        Qt6Gui
        Qt6Widgets
)

target_link_libraries(os2warp_kernel workplace)
