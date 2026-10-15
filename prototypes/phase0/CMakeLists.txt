qt_add_library(phase0_engine STATIC worker.cpp worker.h)
target_link_libraries(phase0_engine PUBLIC Qt6::Gui PRIVATE pdfium)
target_include_directories(phase0_engine PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
if(WIN32)
    target_compile_definitions(phase0_engine PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
endif()
qt_add_executable(phase0_pdf_probe cli.cpp)
target_link_libraries(phase0_pdf_probe PRIVATE phase0_engine)
qt_add_executable(phase0_quick_probe quick.cpp pageitem.cpp pageitem.h)
set_target_properties(phase0_quick_probe PROPERTIES WIN32_EXECUTABLE FALSE)
qt_add_qml_module(phase0_quick_probe
    URI PhaseZero
    VERSION 1.0
    DEPENDENCIES QtQuick
    QML_FILES Main.qml
    SOURCES pageitem.h pageitem.cpp
)
target_link_libraries(phase0_quick_probe PRIVATE phase0_engine Qt6::Quick Qt6::Qml)
qt_add_executable(phase0_contract_tests contracts.cpp)
target_link_libraries(phase0_contract_tests PRIVATE Qt6::Test Qt6::Gui)
add_test(NAME phase0_contracts COMMAND phase0_contract_tests
    -o ${CMAKE_CURRENT_BINARY_DIR}/contracts.txt,txt
)
foreach(target phase0_engine phase0_pdf_probe phase0_quick_probe phase0_contract_tests)
    if(MSVC)
        target_compile_options(${target} PRIVATE /W4 /permissive- /utf-8)
    else()
        target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic)
    endif()
endforeach()
if(WIN32)
    foreach(target phase0_pdf_probe phase0_quick_probe)
        add_custom_command(TARGET ${target} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                $<TARGET_FILE:pdfium> $<TARGET_FILE_DIR:${target}>
        )
    endforeach()
endif()
