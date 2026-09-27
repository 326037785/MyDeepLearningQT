cmake_minimum_required(VERSION 3.20)
project(DeepLearningStudio LANGUAGES CXX)

file(TO_CMAKE_PATH "${CMAKE_SOURCE_DIR}/build" STUDIO_REQUIRED_BUILD_DIRECTORY)
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
    add_compile_options(/utf-8)
endif()

set(VTK_DIR "C:/CLib/vtkQt/lib/cmake/vtk-9.2" CACHE PATH
    "Directory containing the Qt-enabled VTK package configuration")
set(TBB_DIR "C:/CLib/TBB/lib/cmake/tbb" CACHE PATH
    "Directory containing the oneTBB package configuration")
set(libuv_DIR "C:/CLib/libuv/lib/cmake/libuv" CACHE PATH
    "Directory containing the libuv package configuration")

find_package(Qt5 5.15 REQUIRED COMPONENTS Widgets Concurrent Svg)
find_package(TBB CONFIG REQUIRED COMPONENTS tbb)
find_package(libuv 1.52.1 CONFIG REQUIRED)
find_package(VTK 9.2 REQUIRED COMPONENTS
    CommonCore
    CommonDataModel
    InteractionStyle
    RenderingCore
    RenderingOpenGL2
    RenderingQt
    GUISupportQt)
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

if(WIN32 AND NOT EXISTS "${VTK_RUNTIME_DIR}/vtkCommonCore-9.2.dll")
    message(FATAL_ERROR
        "VTK_RUNTIME_DIR does not contain the expected VTK 9.2 DLLs: ${VTK_RUNTIME_DIR}")
endif()
if(WIN32 AND NOT EXISTS "${TBB_RUNTIME_DIR}/tbb12.dll")
    message(FATAL_ERROR "TBB_RUNTIME_DIR does not contain tbb12.dll: ${TBB_RUNTIME_DIR}")
endif()
if(WIN32 AND NOT EXISTS "${LIBUV_RUNTIME_DIR}/uv.dll")
    message(FATAL_ERROR "LIBUV_RUNTIME_DIR does not contain uv.dll: ${LIBUV_RUNTIME_DIR}")
endif()
if(WIN32 AND NOT EXISTS "${LIBIOMP5_RUNTIME_DLL}")
    message(FATAL_ERROR "LibTorch Intel OpenMP runtime does not exist: ${LIBIOMP5_RUNTIME_DLL}")
endif()

file(GLOB STUDIO_INTERNAL_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/*.h")
list(REMOVE_ITEM STUDIO_INTERNAL_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/src/main.cpp")

add_library(DeepLearningStudioLib STATIC ${STUDIO_INTERNAL_SOURCES})
target_include_directories(DeepLearningStudioLib PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(DeepLearningStudioLib PUBLIC
    DeepLearning::Core
    Qt5::Widgets
    Qt5::Concurrent
    Qt5::Svg
    TBB::tbb
    libuv::uv
    ${VTK_LIBRARIES})
target_compile_definitions(DeepLearningStudioLib PUBLIC QT_NO_KEYWORDS)

add_executable(DeepLearningStudio WIN32
    resources/studio.qrc
    src/main.cpp)
target_link_libraries(DeepLearningStudio PRIVATE DeepLearningStudioLib)

if(MSVC)
    set_property(TARGET DeepLearningStudio PROPERTY VS_DEBUGGER_ENVIRONMENT
        "PATH=${VTK_RUNTIME_DIR};${TBB_RUNTIME_DIR};${LIBUV_RUNTIME_DIR};${LIBTORCH_RUNTIME_DIR};%PATH%")
endif()

function(studio_copy_runtime target)
    if(NOT WIN32)
        return()
    endif()
    add_custom_command(TARGET ${target} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            $<TARGET_RUNTIME_DLLS:${target}>
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
            "${Qt5_DIR}/../../../plugins/imageformats/qjpeg.dll"
            "${Qt5_DIR}/../../../plugins/imageformats/qtiff.dll"
            "${Qt5_DIR}/../../../plugins/imageformats/qwebp.dll"
            $<TARGET_FILE_DIR:${target}>/imageformats
        COMMAND_EXPAND_LISTS)
endfunction()

studio_copy_runtime(DeepLearningStudio)

include(CTest)
if(BUILD_TESTING)
    find_package(Qt5 5.15 REQUIRED COMPONENTS Test)

    function(studio_add_test target test_name source)
        add_executable(${target} ${source})
        target_link_libraries(${target} PRIVATE DeepLearningStudioLib)
        set_target_properties(${target} PROPERTIES
            RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/.test-artifacts/$<CONFIG>")
        studio_copy_runtime(${target})
        add_test(NAME ${test_name} COMMAND ${target})
    endfunction()

    studio_add_test(
        image_viewport_transform_test image_viewport_transform
        test/image_viewport_transform_test.cpp)

    add_executable(dataset_visual_test
        resources/studio.qrc
        test/dataset_visual_test.cpp)
    target_link_libraries(dataset_visual_test PRIVATE DeepLearningStudioLib)
    set_target_properties(dataset_visual_test PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/.test-artifacts/$<CONFIG>")
    studio_copy_runtime(dataset_visual_test)
    add_test(NAME dataset_visual_contract COMMAND dataset_visual_test)

    studio_add_test(
        workflow_engine_test workflow_engine
        test/workflow_test.cpp)
    studio_add_test(
        workflow_document_test workflow_document_roundtrip
        test/workflow_document_test.cpp)
    studio_add_test(
        project_manager_test project_manager_roundtrip
        test/project_manager_test.cpp)
    studio_add_test(
        task_workspace_service_test task_workspace_domain
        test/task_workspace_service_test.cpp)
    studio_add_test(
        annotation_exchange_test annotation_exchange_contract
        test/annotation_exchange_test.cpp)
    studio_add_test(
        augmentation_dialog_test augmentation_dialog_contract
        test/augmentation_dialog_test.cpp)

    add_executable(workflow_scene_test test/workflow_scene_test.cpp)
    target_link_libraries(workflow_scene_test PRIVATE DeepLearningStudioLib Qt5::Test)
    set_target_properties(workflow_scene_test PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/.test-artifacts/$<CONFIG>")
    studio_copy_runtime(workflow_scene_test)
    add_test(NAME workflow_scene_interactions COMMAND workflow_scene_test)

    add_executable(main_window_test
        resources/studio.qrc
        test/main_window_test.cpp)
    target_link_libraries(main_window_test PRIVATE DeepLearningStudioLib)
    set_target_properties(main_window_test PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/.test-artifacts/$<CONFIG>")
    studio_copy_runtime(main_window_test)
    add_test(NAME main_window_integration COMMAND main_window_test)
endif()
