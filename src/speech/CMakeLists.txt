# Optional voice files live beside the CPU backend, without becoming application startup imports.
set(pocket_sources "${PROJECT_SOURCE_DIR}/.deps/pocket-source")
set(pocket_output "${PROJECT_BINARY_DIR}/voices/pocket-fr-estelle-v2")
set(SPM_ENABLE_SHARED OFF CACHE BOOL "" FORCE)
set(SPM_ENABLE_TCMALLOC OFF CACHE BOOL "" FORCE)
set(SPM_ENABLE_NFKC_COMPILE OFF CACHE BOOL "" FORCE)
set(SPM_BUILD_TEST OFF CACHE BOOL "" FORCE)
add_subdirectory("${pocket_sources}/sentencepiece-0.2.1" "${CMAKE_CURRENT_BINARY_DIR}/sentencepiece" EXCLUDE_FROM_ALL)
target_compile_definitions(sentencepiece-static PRIVATE UNICODE _UNICODE)

set(BUILD_SHARED_LIBS OFF)
set(SOUNDSTRETCH OFF CACHE BOOL "" FORCE)
set(SOUNDTOUCH_DLL ON CACHE BOOL "" FORCE)
set(OPENMP OFF CACHE BOOL "" FORCE)
add_subdirectory("${pocket_sources}/soundtouch" "${CMAKE_CURRENT_BINARY_DIR}/soundtouch" EXCLUDE_FROM_ALL)
set_target_properties(SoundTouchDLL PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${pocket_output}")

add_library(langlern_pocket SHARED pocket_backend.cpp pocketapi.h)
set_target_properties(langlern_pocket PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${pocket_output}")
target_include_directories(langlern_pocket SYSTEM PRIVATE
    "${pocket_sources}"
    "${pocket_sources}/onnxruntime-win-x64-1.23.2/include"
    "${pocket_sources}/sentencepiece-0.2.1/src")
target_link_libraries(langlern_pocket PRIVATE Qt6::Core sentencepiece-static
    "${pocket_sources}/onnxruntime-win-x64-1.23.2/lib/onnxruntime.lib")
target_compile_options(langlern_pocket PRIVATE /utf-8 /EHsc)
add_dependencies(langlern_pocket SoundTouchDLL)
foreach(filename onnxruntime.dll onnxruntime_providers_shared.dll)
    configure_file("${pocket_sources}/onnxruntime-win-x64-1.23.2/lib/${filename}" "${pocket_output}/${filename}" COPYONLY)
endforeach()
foreach(filename manifest.json flow_lm_flow_int8.onnx flow_lm_main_int8.onnx mimi_decoder_int8.onnx text_embeddings.f16 tokenizer.json tokenizer.model)
    configure_file("${PROJECT_SOURCE_DIR}/.deps/pocket-estelle/models/${filename}" "${pocket_output}/models/${filename}" COPYONLY)
endforeach()
configure_file("${PROJECT_SOURCE_DIR}/.deps/pocket-estelle/voices/estelle.f16" "${pocket_output}/voices/estelle.f16" COPYONLY)
configure_file("${PROJECT_SOURCE_DIR}/third_party/pocket.lock.json" "${pocket_output}/manifest.json" COPYONLY)
file(COPY "${PROJECT_SOURCE_DIR}/third_party/notices/pocket/" DESTINATION "${pocket_output}/licenses")
configure_file("${pocket_sources}/soundtouch-2.3.3.tar.gz" "${pocket_output}/licenses/soundtouch-2.3.3-source.tar.gz" COPYONLY)
