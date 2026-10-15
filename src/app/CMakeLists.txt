qt_add_executable(langlern WIN32 main.cpp)
target_link_libraries(langlern PRIVATE shell_ui shell_uiplugin Qt6::QuickControls2)
target_compile_definitions(langlern PRIVATE LANGLERN_BUILD_TYPE="$<CONFIG>")
langlern_warnings(langlern)
