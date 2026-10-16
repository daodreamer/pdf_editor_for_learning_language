qt_add_executable(shell_tests shell_tests.cpp)
set_target_properties(shell_tests PROPERTIES WIN32_EXECUTABLE FALSE)
target_link_libraries(shell_tests PRIVATE shell_ui shell_uiplugin Qt6::Test Qt6::QuickControls2)
langlern_warnings(shell_tests)
target_compile_definitions(shell_tests PRIVATE LANGLERN_FIXTURES="${PROJECT_SOURCE_DIR}/tests/fixtures")
if(WIN32)
    target_link_libraries(shell_tests PRIVATE Psapi)
    add_custom_command(TARGET shell_tests POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            $<TARGET_FILE:pdfium> $<TARGET_FILE_DIR:shell_tests>)
endif()
add_test(NAME shell_interactions COMMAND shell_tests -o ${CMAKE_CURRENT_BINARY_DIR}/interactions.txt,txt)
set_tests_properties(shell_interactions PROPERTIES TIMEOUT 120)
add_test(NAME shell_startup COMMAND langlern --startup-probe --quit-after-ready)
set_tests_properties(shell_startup PROPERTIES TIMEOUT 20)

qt_add_executable(reading_tests reading_tests.cpp)
set_target_properties(reading_tests PROPERTIES WIN32_EXECUTABLE FALSE)
target_link_libraries(reading_tests PRIVATE shell_ui shell_uiplugin Qt6::Test Qt6::QuickControls2)
langlern_warnings(reading_tests)
target_compile_definitions(reading_tests PRIVATE LANGLERN_FIXTURES="${PROJECT_SOURCE_DIR}/tests/fixtures")
if(WIN32)
    target_link_libraries(reading_tests PRIVATE Psapi)
    add_custom_command(TARGET reading_tests POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            $<TARGET_FILE:pdfium> $<TARGET_FILE_DIR:reading_tests>)
endif()
add_test(NAME reading_invariants COMMAND reading_tests -o ${CMAKE_CURRENT_BINARY_DIR}/reading.txt,txt)
set_tests_properties(reading_invariants PROPERTIES TIMEOUT 120)

qt_add_executable(text_tests text_tests.cpp)
set_target_properties(text_tests PROPERTIES WIN32_EXECUTABLE FALSE)
target_link_libraries(text_tests PRIVATE shell_ui shell_uiplugin Qt6::Test Qt6::QuickControls2)
target_compile_definitions(text_tests PRIVATE LANGLERN_FIXTURES="${PROJECT_SOURCE_DIR}/tests/fixtures")
langlern_warnings(text_tests)
if(WIN32)
    add_custom_command(TARGET text_tests POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            $<TARGET_FILE:pdfium> $<TARGET_FILE_DIR:text_tests>)
endif()
add_test(NAME text_selection COMMAND text_tests -o ${CMAKE_CURRENT_BINARY_DIR}/text.txt,txt)
set_tests_properties(text_selection PROPERTIES TIMEOUT 120)

qt_add_executable(annotation_tests annotation_tests.cpp)
set_target_properties(annotation_tests PROPERTIES WIN32_EXECUTABLE FALSE)
target_link_libraries(annotation_tests PRIVATE shell_ui shell_uiplugin Qt6::Test Qt6::QuickControls2)
target_compile_definitions(annotation_tests PRIVATE LANGLERN_FIXTURES="${PROJECT_SOURCE_DIR}/tests/fixtures")
langlern_warnings(annotation_tests)
if(WIN32)
    add_custom_command(TARGET annotation_tests POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            $<TARGET_FILE:pdfium> $<TARGET_FILE_DIR:annotation_tests>)
endif()
add_test(NAME text_annotations COMMAND annotation_tests -o ${CMAKE_CURRENT_BINARY_DIR}/annotations.txt,txt)
set_tests_properties(text_annotations PROPERTIES TIMEOUT 120)

qt_add_executable(ink_tests ink_tests.cpp)
set_target_properties(ink_tests PROPERTIES WIN32_EXECUTABLE FALSE)
target_link_libraries(ink_tests PRIVATE shell_ui shell_uiplugin Qt6::Test Qt6::QuickControls2)
target_compile_definitions(ink_tests PRIVATE LANGLERN_FIXTURES="${PROJECT_SOURCE_DIR}/tests/fixtures")
langlern_warnings(ink_tests)
if(WIN32)
    target_link_libraries(ink_tests PRIVATE Psapi)
    add_custom_command(TARGET ink_tests POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            $<TARGET_FILE:pdfium> $<TARGET_FILE_DIR:ink_tests>)
endif()
add_test(NAME ink_annotations COMMAND ink_tests -o ${CMAKE_CURRENT_BINARY_DIR}/ink.txt,txt)
set_tests_properties(ink_annotations PROPERTIES TIMEOUT 120)

qt_add_executable(storage_tests storage_tests.cpp)
set_target_properties(storage_tests PROPERTIES WIN32_EXECUTABLE FALSE)
target_link_libraries(storage_tests PRIVATE shell_ui shell_uiplugin Qt6::Test Qt6::QuickControls2)
target_compile_definitions(storage_tests PRIVATE LANGLERN_FIXTURES="${PROJECT_SOURCE_DIR}/tests/fixtures")
langlern_warnings(storage_tests)
if(WIN32)
    add_custom_command(TARGET storage_tests POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            $<TARGET_FILE:pdfium> $<TARGET_FILE_DIR:storage_tests>)
endif()
add_test(NAME storage_export COMMAND storage_tests -o ${CMAKE_CURRENT_BINARY_DIR}/storage.txt,txt)
set_tests_properties(storage_export PROPERTIES TIMEOUT 180)

qt_add_executable(design_tests design_tests.cpp)
set_target_properties(design_tests PROPERTIES WIN32_EXECUTABLE FALSE)
target_link_libraries(design_tests PRIVATE shell_ui shell_uiplugin Qt6::Test Qt6::QuickControls2)
target_compile_definitions(design_tests PRIVATE LANGLERN_FIXTURES="${PROJECT_SOURCE_DIR}/tests/fixtures")
langlern_warnings(design_tests)
if(WIN32)
    target_link_libraries(design_tests PRIVATE winmm)
    add_custom_command(TARGET design_tests POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different $<TARGET_FILE:pdfium> $<TARGET_FILE_DIR:design_tests>)
endif()
add_test(NAME design_workflows COMMAND design_tests -o ${CMAKE_CURRENT_BINARY_DIR}/design.txt,txt)
set_tests_properties(design_workflows PROPERTIES TIMEOUT 120)
