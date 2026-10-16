qt_add_qml_module(shell_ui STATIC
    URI Langlern.Shell
    VERSION 1.0
    CLASS_NAME LanglernShellPlugin
    DEPENDENCIES QtQuick QtQuick.Shapes QtQuick.Controls QtQuick.Layouts QtQuick.Dialogs
    QML_FILES Main.qml Theme.qml DocumentView.qml TextNote.qml ActionButton.qml LineIcon.qml DesignButton.qml StudyTools.qml WorkspaceFrame.qml LibraryPage.qml BookCover.qml ReaderPane.qml PageSidebar.qml WordBookPage.qml ImportPage.qml AudioStrip.qml StudyProgress.qml DesignSwitch.qml
    SOURCES ../../../src/app/offlineservices.h ../../../src/app/offlineservices.cpp
        ../../../src/app/workspacestate.h ../../../src/app/workspacestate.cpp
        ../../../src/app/storageworker.h ../../../src/app/storageworker.cpp
        ../../../src/app/inkstroke.h
        ../../../src/app/inkitem.h ../../../src/app/inkitem.cpp
        ../../../src/app/inkinput.h ../../../src/app/inkinput.cpp
        ../../../src/app/annotationmodel.h ../../../src/app/annotationmodel.cpp
        ../../../src/app/shellstate.h ../../../src/app/shellstate.cpp
        ../../../src/app/pageitem.h ../../../src/app/pageitem.cpp
        ../../../src/app/readingmodel.h ../../../src/app/readingmodel.cpp
        ../../../src/app/textselection.h ../../../src/app/textselection.cpp
)
target_include_directories(shell_ui PUBLIC ../../../src/app)
target_link_libraries(shell_ui PUBLIC Qt6::Quick langlern_pdf PRIVATE Qt6::QuickControls2 Qt6::QuickDialogs2 Qt6::Sql)
if(WIN32)
    target_link_libraries(shell_ui PRIVATE windowsapp sapi ole32 winmm)
endif()
langlern_warnings(shell_ui)
qt_add_resources(shell_ui "interface_translations"
    PREFIX "/"
    BASE "../../../assets"
    FILES ../../../assets/i18n/en.json
)
