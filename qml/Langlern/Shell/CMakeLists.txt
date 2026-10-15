qt_add_qml_module(shell_ui STATIC
    URI Langlern.Shell
    VERSION 1.0
    CLASS_NAME LanglernShellPlugin
    DEPENDENCIES QtQuick QtQuick.Controls QtQuick.Layouts QtQuick.Dialogs
    QML_FILES Main.qml Theme.qml DocumentView.qml
    SOURCES ../../../src/app/shellstate.h ../../../src/app/shellstate.cpp
        ../../../src/app/pageitem.h ../../../src/app/pageitem.cpp
        ../../../src/app/readingmodel.h ../../../src/app/readingmodel.cpp
)
target_include_directories(shell_ui PUBLIC ../../../src/app)
target_link_libraries(shell_ui PUBLIC Qt6::Quick langlern_pdf PRIVATE Qt6::QuickControls2 Qt6::QuickDialogs2)
langlern_warnings(shell_ui)
