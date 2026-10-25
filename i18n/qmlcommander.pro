## Not a build file - only a list for lupdate, which needs one place to be
## told which sources carry translatable text. The build itself is CMake.
##
## Refresh the catalogue with:
##     lupdate -locations none qmlcommander.pro
## The same flag is set in CMakeLists.txt for the update_translations target.
SOURCES += ../src/AppController.cpp \
           ../src/FileListModel.cpp \
           ../src/FileOperationsService.cpp \
           ../src/FilePreviewService.cpp \
           ../src/PanelController.cpp \
           ../src/Localization.cpp \
           ../src/PdfDocument.cpp \
           ../src/main.cpp

lupdate_only {
SOURCES += ../qml/Main.qml \
           ../qml/components/BreadcrumbBar.qml \
           ../qml/components/ConfirmDeleteDialog.qml \
           ../qml/components/ConflictDialog.qml \
           ../qml/components/CopyMoveDialog.qml \
           ../qml/components/DriveSelector.qml \
           ../qml/components/FileListView.qml \
           ../qml/components/FilePanel.qml \
           ../qml/components/FilePreviewDialog.qml \
           ../qml/components/FunctionBar.qml \
           ../qml/components/MediaPlayerPanel.qml \
           ../qml/components/PdfViewPanel.qml \
           ../qml/components/NewFolderDialog.qml \
           ../qml/components/OperationProgressModal.qml
}

TRANSLATIONS = qmlcommander_be.ts
CODECFORTR = UTF-8
