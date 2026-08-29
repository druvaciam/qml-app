cmake_minimum_required(VERSION 3.20)

project(QmlCommander VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

find_package(Qt6 6.5 REQUIRED COMPONENTS
    Core
    Gui
    Qml
    Quick
    QuickControls2
    Concurrent
)

# Suppress QTP0004 policy warning for QML subdirectories
if(COMMAND qt_policy)
    qt_policy(SET QTP0004 NEW)
endif()

set(APP_ICON_RESOURCE "")
if(WIN32)
    set(APP_ICON_RESOURCE "${CMAKE_CURRENT_SOURCE_DIR}/resources/app_icon.rc")
endif()

qt_add_executable(appQmlCommander
    src/main.cpp
    ${APP_ICON_RESOURCE}
)

target_include_directories(appQmlCommander PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/src"
)

qt_add_qml_module(appQmlCommander
    URI QmlCommander
    VERSION 1.0
    RESOURCE_PREFIX "/"
    SOURCES
        src/Theme.h
        src/DriveInfo.h
        src/DriveInfo.cpp
        src/FileListModel.h
        src/FileListModel.cpp
        src/FileOperationsService.h
        src/FileOperationsService.cpp
        src/FilePreviewService.h
        src/FilePreviewService.cpp
        src/PanelController.h
        src/PanelController.cpp
        src/AppController.h
        src/AppController.cpp
    QML_FILES
        qml/Main.qml
        qml/components/FilePanel.qml
        qml/components/FileListView.qml
        qml/components/BreadcrumbBar.qml
        qml/components/DriveSelector.qml
        qml/components/FunctionBar.qml
        qml/components/OperationProgressModal.qml
        qml/components/FilePreviewDialog.qml
        qml/components/NewFolderDialog.qml
        qml/components/ConfirmDeleteDialog.qml
        qml/components/ConflictDialog.qml
        qml/components/CopyMoveDialog.qml
    RESOURCES
        resources/app_icon.png
        resources/app_icon_32.png
)

target_link_libraries(appQmlCommander PRIVATE
    Qt6::Core
    Qt6::Gui
    Qt6::Qml
    Qt6::Quick
    Qt6::QuickControls2
    Qt6::Concurrent
)

if(WIN32)
    target_link_libraries(appQmlCommander PRIVATE ole32 shell32 uuid comctl32)
    if(NOT DEFINED CMAKE_WIN32_EXECUTABLE)
        if(CMAKE_BUILD_TYPE STREQUAL "Release" OR CMAKE_BUILD_TYPE STREQUAL "RelWithDebInfo")
            set_target_properties(appQmlCommander PROPERTIES WIN32_EXECUTABLE ON)
        else()
            set_target_properties(appQmlCommander PROPERTIES WIN32_EXECUTABLE OFF)
        endif()
    endif()
endif()
