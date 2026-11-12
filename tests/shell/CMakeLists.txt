# One executable for every GUI/integration suite: `langlern_tests <suite> [QtTest args]`.
qt_add_executable(langlern_tests
    main.cpp
    shell_tests.cpp
    reading_tests.cpp
    text_tests.cpp
    annotation_tests.cpp
    ink_tests.cpp
    storage_tests.cpp
    design_tests.cpp
    ocr_selection_tests.cpp
    natural_speech_tests.cpp)
set_target_properties(langlern_tests PROPERTIES WIN32_EXECUTABLE FALSE)
# pdfium: export layout headers used by the annotation suite.
target_link_libraries(langlern_tests PRIVATE shell_ui shell_uiplugin Qt6::Test Qt6::QuickControls2 pdfium)
target_compile_definitions(langlern_tests PRIVATE LANGLERN_FIXTURES="${PROJECT_SOURCE_DIR}/tests/fixtures")
langlern_warnings(langlern_tests)
if(WIN32)
    target_link_libraries(langlern_tests PRIVATE Psapi winmm)
    add_dependencies(langlern_tests langlern_speech)
    add_custom_command(TARGET langlern_tests POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different $<TARGET_FILE:pdfium> $<TARGET_FILE_DIR:langlern_tests>)
endif()

# CTest name, suite name, timeout (s). Each suite runs in its own process.
set(langlern_suites
    shell_interactions shell 120
    reading_invariants reading 120
    text_selection text 120
    text_annotations annotation 180
    ink_annotations ink 120
    storage_export storage 180
    design_workflows design 120
    ocr_selection ocr 180
    natural_speech speech 180)
list(LENGTH langlern_suites count)
math(EXPR last "${count} - 1")
foreach(index RANGE 0 ${last} 3)
    math(EXPR suite_index "${index} + 1")
    math(EXPR timeout_index "${index} + 2")
    list(GET langlern_suites ${index} test_name)
    list(GET langlern_suites ${suite_index} suite)
    list(GET langlern_suites ${timeout_index} timeout)
    add_test(NAME ${test_name} COMMAND langlern_tests ${suite} -o ${CMAKE_CURRENT_BINARY_DIR}/${suite}.txt,txt)
    set_tests_properties(${test_name} PROPERTIES TIMEOUT ${timeout})
endforeach()
add_test(NAME shell_startup COMMAND langlern --startup-probe --quit-after-ready)
set_tests_properties(shell_startup PROPERTIES TIMEOUT 20)
