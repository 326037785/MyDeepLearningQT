cmake_minimum_required(VERSION 3.20)
project(DeepLearningStudio LANGUAGES CXX)

set(STUDIO_REQUIRED_BUILD_DIRECTORY "${CMAKE_SOURCE_DIR}/build" CACHE PATH
    "Single CMake build directory for this Studio checkout")
file(TO_CMAKE_PATH "${STUDIO_REQUIRED_BUILD_DIRECTORY}" STUDIO_REQUIRED_BUILD_DIRECTORY)
if(NOT CMAKE_BINARY_DIR STREQUAL STUDIO_REQUIRED_BUILD_DIRECTORY)
    message(FATAL_ERROR
        "DeepLearningStudio must be configured in ${STUDIO_REQUIRED_BUILD_DIRECTORY}")
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
option(BUILD_TESTING "Build DeepLearningStudio test targets" ON)
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/.build-artifacts/lib/$<CONFIG>")
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/.build-artifacts/lib/$<CONFIG>")
if(MSVC)
    add_compile_options(/utf-8 /bigobj /FS)
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL")
endif()

# Optional runtime hints for older OpenCV/LibTorch installations.
set(VTK_DIR "C:/CLib/vtkQt/lib/cmake/vtk-9.2" CACHE PATH
    "Optional VTK package location used to infer its runtime directory")
set(TBB_DIR "C:/CLib/TBB/lib/cmake/tbb" CACHE PATH
    "Optional oneTBB package location used to infer its runtime directory")
set(libuv_DIR "C:/CLib/libuv/lib/cmake/libuv" CACHE PATH
    "Optional libuv package location used to infer its runtime directory")

find_package(Qt5 5.15 REQUIRED COMPONENTS Widgets Concurrent Svg LinguistTools)
# Older OpenCV exports can name imported VTK/TBB targets as DLL dependencies.
if(EXISTS "${TBB_DIR}/TBBConfig.cmake" OR EXISTS "${TBB_DIR}/tbb-config.cmake")
    find_package(TBB QUIET CONFIG COMPONENTS tbb)
endif()
if(EXISTS "${libuv_DIR}/libuvConfig.cmake" OR EXISTS "${libuv_DIR}/libuv-config.cmake")
    find_package(libuv QUIET CONFIG)
endif()
if(EXISTS "${VTK_DIR}/vtk-config.cmake" OR EXISTS "${VTK_DIR}/VTKConfig.cmake")
    find_package(VTK QUIET COMPONENTS
        CommonCore CommonDataModel InteractionStyle RenderingCore RenderingOpenGL2
        RenderingQt GUISupportQt)
endif()
find_package(DeepLearning CONFIG REQUIRED)

get_filename_component(LIBTORCH_INSTALL_PREFIX "${Torch_DIR}/../../.." ABSOLUTE)
set(LIBTORCH_RUNTIME_DIR "${LIBTORCH_INSTALL_PREFIX}/lib" CACHE PATH
    "Directory containing LibTorch runtime DLLs")
set(LIBIOMP5_RUNTIME_DLL "${LIBTORCH_RUNTIME_DIR}/libiomp5md.dll" CACHE FILEPATH
    "Intel OpenMP runtime shipped with the selected LibTorch package")
get_filename_component(VTK_INSTALL_PREFIX "${VTK_DIR}/../../.." ABSOLUTE)
set(VTK_RUNTIME_DIR "${VTK_INSTALL_PREFIX}/bin" CACHE PATH
    "Directory containing VTK runtime DLLs")
get_filename_component(TBB_INSTALL_PREFIX "${TBB_DIR}/../../.." ABSOLUTE)
set(TBB_RUNTIME_DIR "${TBB_INSTALL_PREFIX}/redist/intel64/vc14" CACHE PATH
    "Directory containing oneTBB x64 runtime DLLs")
get_filename_component(LIBUV_INSTALL_PREFIX "${libuv_DIR}/../../.." ABSOLUTE)
set(LIBUV_RUNTIME_DIR "${LIBUV_INSTALL_PREFIX}/bin" CACHE PATH
    "Directory containing libuv runtime DLLs")

if(WIN32 AND NOT EXISTS "${LIBIOMP5_RUNTIME_DLL}")
    message(FATAL_ERROR "LibTorch Intel OpenMP runtime does not exist: ${LIBIOMP5_RUNTIME_DLL}")
endif()

# Include DLLs loaded internally by Torch/OpenCV, beyond CMake's target graph.
file(GLOB STUDIO_TORCH_RUNTIME_DLLS CONFIGURE_DEPENDS "${LIBTORCH_RUNTIME_DIR}/*.dll")
file(GLOB STUDIO_OPTIONAL_RUNTIME_DLLS CONFIGURE_DEPENDS
    "${VTK_RUNTIME_DIR}/*.dll"
    "${TBB_RUNTIME_DIR}/*.dll"
    "${LIBUV_RUNTIME_DIR}/*.dll")
foreach(opencv_target IN LISTS OpenCV_LIBRARIES)
    if(TARGET ${opencv_target})
        get_target_property(opencv_configurations ${opencv_target} IMPORTED_CONFIGURATIONS)
        foreach(opencv_configuration IN LISTS opencv_configurations)
            string(TOUPPER "${opencv_configuration}" opencv_configuration)
            get_target_property(opencv_runtime ${opencv_target} "IMPORTED_LOCATION_${opencv_configuration}")
            if(opencv_runtime AND opencv_runtime MATCHES "\\.dll$")
                get_filename_component(opencv_runtime_directory "${opencv_runtime}" DIRECTORY)
                file(GLOB opencv_runtime_dlls CONFIGURE_DEPENDS "${opencv_runtime_directory}/*.dll")
                list(APPEND STUDIO_OPTIONAL_RUNTIME_DLLS ${opencv_runtime_dlls})
            endif()
        endforeach()
    endif()
endforeach()
list(REMOVE_DUPLICATES STUDIO_OPTIONAL_RUNTIME_DLLS)

function(studio_copy_runtime target)
    if(NOT WIN32)
        return()
    endif()
    add_custom_command(TARGET ${target} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            $<TARGET_RUNTIME_DLLS:${target}>
            ${STUDIO_OPTIONAL_RUNTIME_DLLS}
            $<TARGET_FILE_DIR:${target}>
        # Prefer the CUDA/cuDNN DLLs bundled with the selected LibTorch SDK.
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            ${STUDIO_TORCH_RUNTIME_DLLS}
            "${LIBIOMP5_RUNTIME_DLL}"
            $<TARGET_FILE_DIR:${target}>
        COMMAND ${CMAKE_COMMAND} -E make_directory
            $<TARGET_FILE_DIR:${target}>/platforms
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${Qt5_DIR}/../../../plugins/platforms/qwindows.dll"
            $<TARGET_FILE_DIR:${target}>/platforms
        COMMAND ${CMAKE_COMMAND} -E make_directory
            $<TARGET_FILE_DIR:${target}>/imageformats
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${Qt5_DIR}/../../../plugins/imageformats/qico.dll"
            "${Qt5_DIR}/../../../plugins/imageformats/qjpeg.dll"
            "${Qt5_DIR}/../../../plugins/imageformats/qtiff.dll"
            "${Qt5_DIR}/../../../plugins/imageformats/qwebp.dll"
            $<TARGET_FILE_DIR:${target}>/imageformats
        COMMAND_EXPAND_LISTS
        VERBATIM)
endfunction()

include(CTest)
if(BUILD_TESTING)
    find_package(Qt5 5.15 REQUIRED COMPONENTS Test)
endif()

add_subdirectory(src)
if(BUILD_TESTING)
    add_subdirectory(test)
endif()
