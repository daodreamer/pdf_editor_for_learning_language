# Optional voice files live beside the CPU backend, without becoming application startup imports.
set(speech_sources "${PROJECT_SOURCE_DIR}/.deps/speech-source")
set(speech_models "${PROJECT_SOURCE_DIR}/.deps/supertonic3-fr")
set(speech_output "${PROJECT_BINARY_DIR}/voices/supertonic3-fr")
# tools/bootstrap-speech.py lays out the official Microsoft NuGet package as include/ + lib/.
if(WIN32)
    set(speech_ort "${speech_sources}/onnxruntime-win-x64-1.23.2-nuget")
else()
    set(speech_ort "${speech_sources}/onnxruntime-osx-arm64-1.23.2-nuget")
endif()

set(BUILD_SHARED_LIBS OFF)
set(SOUNDSTRETCH OFF CACHE BOOL "" FORCE)
set(SOUNDTOUCH_DLL ON CACHE BOOL "" FORCE)
set(OPENMP OFF CACHE BOOL "" FORCE)
set(CMAKE_POLICY_DEFAULT_CMP0068 NEW)  # SoundTouch declares an old minimum; keep install names independent of RPATH
add_subdirectory("${speech_sources}/soundtouch" "${CMAKE_CURRENT_BINARY_DIR}/soundtouch" EXCLUDE_FROM_ALL)
set_target_properties(SoundTouchDLL PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${speech_output}" LIBRARY_OUTPUT_DIRECTORY "${speech_output}")

add_library(langlern_speech SHARED supertonic_backend.cpp speechapi.h)
set_target_properties(langlern_speech PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${speech_output}" LIBRARY_OUTPUT_DIRECTORY "${speech_output}")
target_include_directories(langlern_speech SYSTEM PRIVATE "${speech_ort}/include")
add_dependencies(langlern_speech SoundTouchDLL)
if(WIN32)
    target_link_libraries(langlern_speech PRIVATE Qt6::Core "${speech_ort}/lib/onnxruntime.lib")
    target_compile_options(langlern_speech PRIVATE /utf-8 /EHsc)
    foreach(filename onnxruntime.dll onnxruntime_providers_shared.dll)
        configure_file("${speech_ort}/lib/${filename}" "${speech_output}/${filename}" COPYONLY)
    endforeach()
else()
    # The dylib's install name is @rpath/libonnxruntime.1.23.2.dylib; ship it under that name beside the
    # backend, which finds it through @loader_path. All three load lazily on the first natural read-aloud.
    configure_file("${speech_ort}/lib/libonnxruntime.dylib" "${speech_output}/libonnxruntime.1.23.2.dylib" COPYONLY)
    target_link_libraries(langlern_speech PRIVATE Qt6::Core "${speech_output}/libonnxruntime.1.23.2.dylib")
    set_target_properties(langlern_speech PROPERTIES BUILD_RPATH "@loader_path" INSTALL_RPATH "@loader_path"
        BUILD_WITH_INSTALL_RPATH ON)
    set_target_properties(SoundTouchDLL PROPERTIES BUILD_WITH_INSTALL_RPATH ON INSTALL_NAME_DIR "@rpath")
endif()
foreach(filename models/duration_predictor.onnx models/text_encoder.onnx models/vector_estimator.onnx models/vocoder.onnx
                 models/tts.json models/unicode_indexer.json voices/M1.json voices/F3.json)
    configure_file("${speech_models}/${filename}" "${speech_output}/${filename}" COPYONLY)
endforeach()
configure_file("${PROJECT_SOURCE_DIR}/third_party/speech.lock.json" "${speech_output}/manifest.json" COPYONLY)
file(COPY "${PROJECT_SOURCE_DIR}/third_party/notices/speech/" DESTINATION "${speech_output}/licenses")
configure_file("${speech_sources}/soundtouch-2.3.3.tar.gz" "${speech_output}/licenses/soundtouch-2.3.3-source.tar.gz" COPYONLY)
