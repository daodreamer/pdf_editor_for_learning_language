qt_add_executable(langlern WIN32 main.cpp)
# Window/taskbar icon at runtime; the .rc below gives Explorer and the taskbar pin the same icon.
qt_add_resources(langlern "app_icon"
    PREFIX "/"
    BASE "../../assets"
    FILES ../../assets/icons/linguapdf-16.png ../../assets/icons/linguapdf-24.png
        ../../assets/icons/linguapdf-32.png ../../assets/icons/linguapdf-48.png
        ../../assets/icons/linguapdf-64.png ../../assets/icons/linguapdf-128.png
        ../../assets/icons/linguapdf-256.png
)
# The CMake target keeps its internal name; the shipped executable is LinguaPDF.exe.
set_target_properties(langlern PROPERTIES OUTPUT_NAME LinguaPDF)
if(WIN32)
    configure_file(linguapdf.rc.in "${CMAKE_CURRENT_BINARY_DIR}/linguapdf.rc" @ONLY)
    target_sources(langlern PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/linguapdf.rc")
endif()
target_link_libraries(langlern PRIVATE shell_ui shell_uiplugin Qt6::QuickControls2)
target_compile_definitions(langlern PRIVATE LANGLERN_BUILD_TYPE="$<CONFIG>" LINGUAPDF_VERSION="${PROJECT_VERSION}")
langlern_warnings(langlern)
if(WIN32)
    add_dependencies(langlern langlern_speech)
endif()
if(WIN32)
    add_custom_command(TARGET langlern POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            $<TARGET_FILE:pdfium> $<TARGET_FILE_DIR:langlern>)
endif()
