get_filename_component(LIBS_INCLUDE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/.." ABSOLUTE)

if(NOT APPLE OR NOT CMAKE_SYSTEM_PROCESSOR MATCHES "^(arm64|aarch64)$")
    message(FATAL_ERROR "The native runtime currently requires Apple ARM64")
endif()
if(CMAKE_OSX_ARCHITECTURES AND NOT CMAKE_OSX_ARCHITECTURES STREQUAL "arm64")
    message(FATAL_ERROR "The native runtime currently requires a single arm64 architecture")
endif()

add_library(anyps5_native_graphics STATIC)
add_dependencies(anyps5_native_graphics agc_shader_headers agc_trace_revision)
set(nativeComponents libc libkernel libSceAgc libSceAgcDriver
    libSceVideoOut libScePad libSceMouse libSceKeyboard)
set(nativeUnsupportedSources
    "${LIBS_INCLUDE_DIR}/prx/libc/src/ApplicationProcessParameters.cpp"
    "${LIBS_INCLUDE_DIR}/prx/libc/src/Strings.cpp"
    "${LIBS_INCLUDE_DIR}/prx/libc/src/Math.cpp"
    "${LIBS_INCLUDE_DIR}/prx/libc/src/Formatting.cpp"
    "${LIBS_INCLUDE_DIR}/prx/libc/src/ScanningWide.cpp"
    "${LIBS_INCLUDE_DIR}/prx/libc/src/FormattingWide.cpp"
    "${LIBS_INCLUDE_DIR}/prx/libc/src/JumpBuffer.cpp"
    "${LIBS_INCLUDE_DIR}/prx/libkernel/System/src/Exception.cpp"
    "${LIBS_INCLUDE_DIR}/prx/libkernel/System/src/UserContext.cpp"
    "${LIBS_INCLUDE_DIR}/prx/libc/src/exception/Exports.cpp")
foreach(component IN LISTS nativeComponents)
    get_target_property(componentSources ${component} SOURCES)
    get_target_property(componentDirectory ${component} SOURCE_DIR)
    foreach(source IN LISTS componentSources)
        if(NOT IS_ABSOLUTE "${source}")
            set(source "${componentDirectory}/${source}")
        endif()
        if(NOT source IN_LIST nativeUnsupportedSources)
            target_sources(anyps5_native_graphics PRIVATE "${source}")
        endif()
    endforeach()
    get_target_property(componentIncludes ${component} INCLUDE_DIRECTORIES)
    if(componentIncludes)
        target_include_directories(anyps5_native_graphics PRIVATE ${componentIncludes})
    endif()
    get_target_property(componentDefines ${component} COMPILE_DEFINITIONS)
    if(componentDefines)
        target_compile_definitions(anyps5_native_graphics PRIVATE ${componentDefines})
    endif()
endforeach()
add_library(native_runtime_platform INTERFACE)
target_link_libraries(native_runtime_platform INTERFACE "-framework CoreFoundation")
add_png_decoder(decoder_png_native native_runtime_platform)
file(GLOB_RECURSE nativeShaderGuards CONFIGURE_DEPENDS
    "${LIBS_INCLUDE_DIR}/prx/libSceAgcDriver/*.cpp"
    "${LIBS_INCLUDE_DIR}/prx/libSceAgcDriver/*.hpp"
    "${LIBS_INCLUDE_DIR}/prx/libSceAgcDriver/*.h")
list(FILTER nativeShaderGuards EXCLUDE REGEX "/(tests|tools)/")
add_shader_recompiler(shader_recompiler_native native_runtime_platform GUARD_SOURCES ${nativeShaderGuards})
target_link_libraries(anyps5_native_graphics PUBLIC jpeg_turbo SDL2-static shader_recompiler_native decoder_png_native native_runtime_platform anyps5_native_image)
if(ANYPS5_ENABLE_SPIRV_TOOLS)
    target_link_libraries(anyps5_native_graphics PUBLIC SPIRV-Tools-opt SPIRV-Tools-static)
endif()
target_link_options(anyps5_native_graphics INTERFACE LINKER:-dead_strip)
target_include_directories(anyps5_native_graphics PUBLIC "${LIBS_INCLUDE_DIR}")
set_target_properties(anyps5_native_graphics PROPERTIES POSITION_INDEPENDENT_CODE ON CXX_EXTENSIONS OFF)

add_test_executable(native_shared_memory_alias_tests NativeSharedMemoryAliasTests.cpp)
target_link_libraries(native_shared_memory_alias_tests PRIVATE anyps5_native_graphics)
add_test(NAME native_shared_memory_alias COMMAND native_shared_memory_alias_tests)
set_tests_properties(native_shared_memory_alias PROPERTIES TIMEOUT 20)

add_test_executable(native_runtime_smoke NativeRuntimeSmoke.cpp)
target_link_libraries(native_runtime_smoke PRIVATE anyps5_native_graphics)

target_include_directories(native_runtime_smoke PRIVATE "${CMAKE_SOURCE_DIR}/core/shader/recompiler" "${CMAKE_SOURCE_DIR}/3rdparty/Vulkan-Headers/include")

add_test(NAME native_runtime_smoke COMMAND native_runtime_smoke)
set_tests_properties(native_runtime_smoke PROPERTIES TIMEOUT 30)

find_package(Python3 REQUIRED COMPONENTS Interpreter)
set(cubeExample "${CMAKE_SOURCE_DIR}/core/aot/examples/gpu_cube")
file(GLOB cubeBuildInputs CONFIGURE_DEPENDS "${cubeExample}/*.py" "${cubeExample}/contract-recipe.json" "${CMAKE_SOURCE_DIR}/core/aot/*.py")
set(cubeInputHashes "${CMAKE_C_COMPILER_ID}-${CMAKE_C_COMPILER_VERSION}")
foreach(input IN LISTS cubeBuildInputs)
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${input}")
    file(SHA256 "${input}" inputHash)
    string(APPEND cubeInputHashes "${inputHash}")
endforeach()
string(SHA256 cubeInputKey "${cubeInputHashes}")
set(cubeArtifacts "${CMAKE_CURRENT_BINARY_DIR}/gpu-cube-${cubeInputKey}")
set(cubePythonPath "${CMAKE_SOURCE_DIR}/core/aot:${CMAKE_SOURCE_DIR}/3rdparty/capstone/bindings/python:${CMAKE_SOURCE_DIR}/3rdparty/pyelftools")
add_custom_command(OUTPUT "${cubeArtifacts}/module.o" "${cubeArtifacts}/reference.o" "${cubeArtifacts}/build.json"
    COMMAND ${CMAKE_COMMAND} -E env "PYTHONPATH=${cubePythonPath}" "LIBCAPSTONE_PATH=$<TARGET_FILE_DIR:capstone_shared>"
        ${Python3_EXECUTABLE} "${cubeExample}/build.py" --sdk "${CMAKE_SOURCE_DIR}/3rdparty/ps5link-sdk" --output "${cubeArtifacts}"
    DEPENDS capstone_shared ${cubeBuildInputs}
    VERBATIM)
set_source_files_properties("${cubeArtifacts}/module.o" "${cubeArtifacts}/reference.o" PROPERTIES GENERATED TRUE EXTERNAL_OBJECT TRUE)
add_executable(native_gpu_cube EXCLUDE_FROM_ALL NativeCubeLauncher.cpp "${cubeArtifacts}/module.o")
add_executable(native_gpu_cube_reference EXCLUDE_FROM_ALL NativeCubeReference.cpp "${cubeArtifacts}/reference.o")
target_link_libraries(native_gpu_cube PRIVATE anyps5_native_graphics)
target_link_libraries(native_gpu_cube_reference PRIVATE anyps5_native_graphics)
add_custom_target(native_gpu_cube_build DEPENDS native_gpu_cube native_gpu_cube_reference)
file(GENERATE OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/gpu-cube-paths.json"
    CONTENT "{\"aot\":\"$<TARGET_FILE:native_gpu_cube>\",\"reference\":\"$<TARGET_FILE:native_gpu_cube_reference>\",\"build\":\"${cubeArtifacts}\"}\n")

add_test_executable(native_atomic_shared_pointer_tests ../tests/AtomicSharedPointer.cpp)
target_include_directories(native_atomic_shared_pointer_tests PRIVATE "${LIBS_INCLUDE_DIR}")
target_compile_options(native_atomic_shared_pointer_tests PRIVATE -UNDEBUG)
add_test(NAME native_atomic_shared_pointer COMMAND native_atomic_shared_pointer_tests)
set_tests_properties(native_atomic_shared_pointer PROPERTIES TIMEOUT 20)
add_test(NAME native_graphics_image_comparison COMMAND ${Python3_EXECUTABLE} "${cubeExample}/test_images.py")
set_tests_properties(native_graphics_image_comparison PROPERTIES TIMEOUT 20)

add_test_executable(native_host_counter_tests ../tests/HostCounter.cpp)
target_include_directories(native_host_counter_tests PRIVATE "${LIBS_INCLUDE_DIR}")
add_test(NAME native_host_counter COMMAND native_host_counter_tests)
set_tests_properties(native_host_counter PROPERTIES TIMEOUT 30)
add_test_executable(native_guest_tls_tests ../tests/GuestTlsMacOs.cpp ../prx/libkernel/Pthread/src/GuestTlsMacOs.cpp)
target_include_directories(native_guest_tls_tests PRIVATE "${LIBS_INCLUDE_DIR}")
add_test(NAME native_guest_tls COMMAND native_guest_tls_tests)
set_tests_properties(native_guest_tls PROPERTIES TIMEOUT 30)

add_test(NAME native_graphics_reference_policy COMMAND ${Python3_EXECUTABLE} "${cubeExample}/test_reference.py")
set_tests_properties(native_graphics_reference_policy PROPERTIES TIMEOUT 20)
add_custom_target(native_gpu_cube_check
    COMMAND ${Python3_EXECUTABLE} "${cubeExample}/verify.py"
        --paths "${CMAKE_CURRENT_BINARY_DIR}/gpu-cube-paths.json" --output-parent "${CMAKE_CURRENT_BINARY_DIR}/graphics-runs"
    DEPENDS native_gpu_cube native_gpu_cube_reference
    USES_TERMINAL VERBATIM)

foreach(test IN ITEMS GuestSocketEvents GuestSocketPair GuestEqueueEvents GuestKernelFcntl)
    string(TOLOWER "${test}" lower)
    add_test_executable(native_${lower}_tests "${LIBS_INCLUDE_DIR}/tests/${test}.cpp")
    target_link_libraries(native_${lower}_tests PRIVATE anyps5_native_graphics)
    add_test(NAME native_${lower} COMMAND native_${lower}_tests)
    set_tests_properties(native_${lower} PROPERTIES TIMEOUT 30)
endforeach()

add_test_executable(native_writewatch_tests "${LIBS_INCLUDE_DIR}/tests/WriteWatch.cpp")
target_link_libraries(native_writewatch_tests PRIVATE anyps5_native_graphics)
foreach(mode IN ITEMS collect fresh armed register mapped host protection threads)
    add_test(NAME native_writewatch_${mode} COMMAND native_writewatch_tests ${mode})
    set_tests_properties(native_writewatch_${mode} PROPERTIES TIMEOUT 30)
endforeach()

foreach(target IN ITEMS native_gpu_cube native_gpu_cube_reference)
    if(target STREQUAL "native_gpu_cube")
        set(cubeObject module.o)
    else()
        set(cubeObject reference.o)
    endif()
    add_custom_command(TARGET ${target} POST_BUILD
        COMMAND ${Python3_EXECUTABLE} "${cubeExample}/artifact.py"
            --binary "$<TARGET_FILE:${target}>" --object "${cubeArtifacts}/${cubeObject}" --build "${cubeArtifacts}/build.json"
            --archive "$<TARGET_FILE:anyps5_native_graphics>" --archive "$<TARGET_FILE:anyps5_native_image>"
            --archive "$<TARGET_FILE:shader_recompiler_native>" --archive "$<TARGET_FILE:decoder_png_native>"
            --archive "$<TARGET_FILE:SDL2-static>"
        VERBATIM)
endforeach()

add_test_executable(native_constant_bit_tests ../prx/libSceAgcDriver/tests/ConstantBitTest.cpp)
target_link_libraries(native_constant_bit_tests PRIVATE anyps5_native_graphics)
add_test(NAME native_constant_bit COMMAND native_constant_bit_tests)
set_tests_properties(native_constant_bit PROPERTIES TIMEOUT 30)

add_test_executable(native_direct_memory_alias_tests ../tests/NativeDirectMemoryAliases.cpp)
target_link_libraries(native_direct_memory_alias_tests PRIVATE anyps5_native_graphics)
add_test(NAME native_direct_memory_aliases COMMAND native_direct_memory_alias_tests)
set_tests_properties(native_direct_memory_aliases PROPERTIES TIMEOUT 30)
set(padExample "${CMAKE_SOURCE_DIR}/core/aot/examples/pad_input")
enable_language(C)
set(padSdk "${CMAKE_SOURCE_DIR}/3rdparty/ps5link-sdk")
add_executable(native_pad_linker EXCLUDE_FROM_ALL
    "${padSdk}/linker/link_real.c" "${padSdk}/linker/elf_object.c"
    "${padSdk}/linker/linker.c" "${padSdk}/linker/dynwriter.c"
    "${padSdk}/linker/catalog.c" "${padSdk}/linker/catalog_extra.c"
    "${padSdk}/linker/catalog_lookup.c" "${padSdk}/linker/nid.c" "${padSdk}/linker/sha1.c")
file(GLOB padBuildInputs CONFIGURE_DEPENDS "${padExample}/*.py" "${CMAKE_SOURCE_DIR}/core/aot/*.py"
    "${CMAKE_SOURCE_DIR}/core/aot/catalogs/*.json")
set(padInputHashes "${CMAKE_C_COMPILER_ID}-${CMAKE_C_COMPILER_VERSION}")
foreach(input IN LISTS padBuildInputs)
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${input}")
    file(SHA256 "${input}" inputHash)
    string(APPEND padInputHashes "${inputHash}")
endforeach()
string(SHA256 padInputKey "${padInputHashes}")
set(padArtifacts "${CMAKE_CURRENT_BINARY_DIR}/pad-input-${padInputKey}")
add_custom_command(OUTPUT "${padArtifacts}/module.o" "${padArtifacts}/reference.o" "${padArtifacts}/build.json"
    COMMAND ${CMAKE_COMMAND} -E env "PYTHONPATH=${cubePythonPath}" "LIBCAPSTONE_PATH=$<TARGET_FILE_DIR:capstone_shared>"
        ${Python3_EXECUTABLE} "${padExample}/build.py" --sdk "${padSdk}" --linker $<TARGET_FILE:native_pad_linker>
        --output "${padArtifacts}"
    DEPENDS native_pad_linker capstone_shared ${padBuildInputs} VERBATIM)
set_source_files_properties("${padArtifacts}/module.o" "${padArtifacts}/reference.o" PROPERTIES GENERATED TRUE EXTERNAL_OBJECT TRUE)
add_executable(native_pad_input EXCLUDE_FROM_ALL "${padExample}/launcher.cpp" "${padArtifacts}/module.o"
    "${LIBS_INCLUDE_DIR}/prx/libSceUserService/Export.cpp")
target_link_libraries(native_pad_input PRIVATE anyps5_native_graphics)
add_library(native_pad_test_providers STATIC EXCLUDE_FROM_ALL
    "${LIBS_INCLUDE_DIR}/prx/libScePad/Export.cpp"
    "${LIBS_INCLUDE_DIR}/prx/libSceUserService/Export.cpp"
    "${LIBS_INCLUDE_DIR}/prx/libkernel/Time/Time.cpp")
target_include_directories(native_pad_test_providers PRIVATE "${LIBS_INCLUDE_DIR}")
target_compile_definitions(native_pad_test_providers PRIVATE
    sceUserServiceGetInitialUser=PadInputProviderGetUser scePadInit_nid_postfix=PadInputProviderInit
    scePadOpen_nid_postfix=PadInputProviderOpen scePadReadState=PadInputProviderRead
    scePadClose_nid_postfix=PadInputProviderClose sceKernelUsleep_nid_postfix=PadInputProviderUsleep)
foreach(mode IN ITEMS aot reference)
    if(mode STREQUAL "aot")
        set(padObject module.o)
    else()
        set(padObject reference.o)
    endif()
    add_executable(native_pad_${mode}_test EXCLUDE_FROM_ALL "${padExample}/launcher.cpp"
        "${padExample}/input_test.cpp" "${padArtifacts}/${padObject}")
    target_compile_definitions(native_pad_${mode}_test PRIVATE PAD_INPUT_TEST)
    if(mode STREQUAL "reference")
        target_compile_definitions(native_pad_${mode}_test PRIVATE PAD_INPUT_REFERENCE)
    endif()
    target_link_libraries(native_pad_${mode}_test PRIVATE native_pad_test_providers anyps5_native_graphics)
    add_custom_command(TARGET native_pad_${mode}_test POST_BUILD
        COMMAND ${Python3_EXECUTABLE} "${cubeExample}/artifact.py" --binary "$<TARGET_FILE:native_pad_${mode}_test>"
            --object "${padArtifacts}/${padObject}" --build "${padArtifacts}/build.json"
            --archive "$<TARGET_FILE:native_pad_test_providers>" --archive "$<TARGET_FILE:anyps5_native_graphics>"
            --archive "$<TARGET_FILE:anyps5_native_image>" VERBATIM)
endforeach()
add_custom_command(TARGET native_pad_input POST_BUILD
    COMMAND ${Python3_EXECUTABLE} "${cubeExample}/artifact.py" --binary "$<TARGET_FILE:native_pad_input>"
        --object "${padArtifacts}/module.o" --build "${padArtifacts}/build.json"
        --archive "$<TARGET_FILE:anyps5_native_graphics>" --archive "$<TARGET_FILE:anyps5_native_image>" VERBATIM)
add_custom_target(native_pad_input_check
    COMMAND ${Python3_EXECUTABLE} "${padExample}/verify.py" --aot $<TARGET_FILE:native_pad_aot_test>
        --reference $<TARGET_FILE:native_pad_reference_test> --production $<TARGET_FILE:native_pad_input>
        --build "${padArtifacts}/build.json" --output-parent "${CMAKE_CURRENT_BINARY_DIR}/pad-runs"
    DEPENDS native_pad_input native_pad_aot_test native_pad_reference_test USES_TERMINAL VERBATIM)
add_test(NAME native_pad_input COMMAND ${Python3_EXECUTABLE} "${padExample}/verify.py"
    --aot $<TARGET_FILE:native_pad_aot_test> --reference $<TARGET_FILE:native_pad_reference_test>
    --build "${padArtifacts}/build.json" --output-parent "${CMAKE_CURRENT_BINARY_DIR}/pad-runs")
set_tests_properties(native_pad_input PROPERTIES TIMEOUT 30)
