qt_add_executable(langlern WIN32 main.cpp)
target_link_libraries(langlern PRIVATE shell_ui shell_uiplugin Qt6::QuickControls2)
target_compile_definitions(langlern PRIVATE LANGLERN_BUILD_TYPE="$<CONFIG>")
langlern_warnings(langlern)
if(WIN32)
    add_dependencies(langlern langlern_pocket)
endif()
if(WIN32)
    add_custom_command(TARGET langlern POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            $<TARGET_FILE:pdfium> $<TARGET_FILE_DIR:langlern>)
endif()
