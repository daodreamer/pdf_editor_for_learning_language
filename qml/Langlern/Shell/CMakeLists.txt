qt_add_qml_module(shell_ui STATIC
    URI Langlern.Shell
    VERSION 1.0
    CLASS_NAME LanglernShellPlugin
    DEPENDENCIES QtQuick QtQuick.Shapes QtQuick.Controls QtQuick.Layouts QtQuick.Dialogs
    QML_FILES Main.qml Theme.qml DocumentView.qml TextNote.qml TextFormatPopover.qml SelectionActions.qml ActionButton.qml LineIcon.qml DesignButton.qml StudyTools.qml WorkspaceFrame.qml LibraryPage.qml BookCover.qml ReaderPane.qml PageSidebar.qml WordBookPage.qml ImportPage.qml AudioStrip.qml StudyProgress.qml DesignSwitch.qml DesignCombo.qml HoverTip.qml LookupCard.qml
    SOURCES ../../../src/app/offlineservices.h ../../../src/app/offlineservices.cpp
        ../../../src/app/naturalspeech.h ../../../src/app/naturalspeech.cpp
        ../../../src/app/frenchdictionary.h ../../../src/app/frenchdictionary.cpp
        ../../../src/app/annotationtext.h ../../../src/app/annotationtext.cpp
        ../../../src/app/workspacestate.h ../../../src/app/workspacestate.cpp
        ../../../src/app/legacydata.h ../../../src/app/legacydata.cpp
        ../../../src/app/windowchrome.h ../../../src/app/windowchrome.cpp
        ../../../src/app/storageworker.h ../../../src/app/storageworker.cpp
        ../../../src/app/inkstroke.h ../../../src/app/inksmoothing.h
        ../../../src/app/inkitem.h ../../../src/app/inkitem.cpp
        ../../../src/app/inkinput.h ../../../src/app/inkinput.cpp
        ../../../src/app/annotationmodel.h ../../../src/app/annotationmodel.cpp
        ../../../src/app/shellstate.h ../../../src/app/shellstate.cpp
        ../../../src/app/pageitem.h ../../../src/app/pageitem.cpp
        ../../../src/app/readingmodel.h ../../../src/app/readingmodel.cpp
        ../../../src/app/textselection.h ../../../src/app/textselection.cpp
        ../../../src/app/languagedetect.h ../../../src/app/languagedetect.cpp
        ../../../src/app/inputsources.h
        ../../../src/app/inputsourceswitcher.h ../../../src/app/inputsourceswitcher.cpp
        ../../../src/app/aiplatform.h ../../../src/app/aihandoff.h ../../../src/app/aihandoff.cpp
        ../../../src/app/aiprompt.h ../../../src/app/aiprompt.cpp
)
target_include_directories(shell_ui PUBLIC ../../../src/app)
target_link_libraries(shell_ui PUBLIC Qt6::Quick langlern_pdf langlern_ocr PRIVATE Qt6::QuickControls2 Qt6::QuickDialogs2 Qt6::Sql)
if(WIN32)
    target_link_libraries(shell_ui PRIVATE windowsapp sapi ole32 winmm dwmapi shlwapi)
    target_sources(shell_ui PRIVATE ../../../src/app/inputsources_win.cpp ../../../src/app/aiplatform_win.cpp)
elseif(NOT APPLE)
    target_sources(shell_ui PRIVATE ../../../src/app/inputsources_other.cpp ../../../src/app/aiplatform_other.cpp)
endif()
if(APPLE)
    # System audio, speech and text recognition (AVFoundation, AudioToolbox, Vision); no extra dependencies.
    # Input sources (keyboard follows the document language): Carbon Text Input Sources.
    # "Ask AI" hand-off: AppKit (installed/frontmost apps), ApplicationServices (paste keystroke).
    target_sources(shell_ui PRIVATE ../../../src/app/platformmedia.h ../../../src/app/platformmedia_mac.mm
        ../../../src/app/inputsources_mac.mm ../../../src/app/aiplatform_mac.mm)
    set_source_files_properties(../../../src/app/platformmedia_mac.mm ../../../src/app/inputsources_mac.mm
        ../../../src/app/aiplatform_mac.mm PROPERTIES COMPILE_OPTIONS "-fobjc-arc")
    target_link_libraries(shell_ui PRIVATE "-framework Foundation" "-framework AVFoundation" "-framework AudioToolbox"
        "-framework Vision" "-framework CoreGraphics" "-framework Carbon" "-framework AppKit"
        "-framework ApplicationServices")
endif()
langlern_warnings(shell_ui)
qt_add_resources(shell_ui "interface_translations"
    PREFIX "/"
    BASE "../../../assets"
    FILES ../../../assets/i18n/en.json
)
qt_add_resources(shell_ui "speech_manifest"
    PREFIX "/speech"
    BASE "../../../third_party"
    FILES ../../../third_party/speech.lock.json
)
