file(GLOB_RECURSE STUDIO_INTERNAL_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/*.h")
list(REMOVE_ITEM STUDIO_INTERNAL_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/app/main.cpp")

add_library(DeepLearningStudioLib STATIC ${STUDIO_INTERNAL_SOURCES})
target_include_directories(DeepLearningStudioLib PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(DeepLearningStudioLib PUBLIC
    DeepLearning::Core
    Qt5::Widgets
    Qt5::Concurrent
    Qt5::Svg)
target_compile_definitions(DeepLearningStudioLib PUBLIC QT_NO_KEYWORDS NOMINMAX)

add_executable(DeepLearningStudio WIN32
    "${CMAKE_SOURCE_DIR}/resources/studio.qrc"
    app/main.cpp)
if(WIN32)
    target_sources(DeepLearningStudio PRIVATE "${CMAKE_SOURCE_DIR}/resources/studio.rc")
endif()
target_link_libraries(DeepLearningStudio PRIVATE DeepLearningStudioLib)
set_target_properties(DeepLearningStudio PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/$<CONFIG>")

qt5_add_translation(STUDIO_TRANSLATIONS
    "${CMAKE_SOURCE_DIR}/resources/translations/studio_zh_CN.ts")
add_custom_target(studio_translations DEPENDS ${STUDIO_TRANSLATIONS})
get_filename_component(STUDIO_QT_TRANSLATIONS_DIR "${Qt5_DIR}/../../../translations" ABSOLUTE)
set(STUDIO_QT_TRANSLATION_ENTRY "")
if(EXISTS "${STUDIO_QT_TRANSLATIONS_DIR}/qt_zh_CN.qm")
    set(STUDIO_QT_TRANSLATION_ENTRY
        "<file alias=\"qt_zh_CN.qm\">${STUDIO_QT_TRANSLATIONS_DIR}/qt_zh_CN.qm</file>")
endif()
set(STUDIO_TRANSLATIONS_QRC "${CMAKE_CURRENT_BINARY_DIR}/studio_translations.qrc")
configure_file("${CMAKE_SOURCE_DIR}/resources/translations/translations.qrc.in"
    "${STUDIO_TRANSLATIONS_QRC}" @ONLY)
target_sources(DeepLearningStudio PRIVATE "${STUDIO_TRANSLATIONS_QRC}")
add_dependencies(DeepLearningStudio studio_translations)
# The shell UI test embeds the same generated translations resource.
set(STUDIO_TRANSLATIONS_QRC "${STUDIO_TRANSLATIONS_QRC}" PARENT_SCOPE)

# The solution belongs to the root directory's project() call.
set_property(DIRECTORY "${CMAKE_SOURCE_DIR}" PROPERTY VS_STARTUP_PROJECT DeepLearningStudio)
if(MSVC)
    set_property(TARGET DeepLearningStudio PROPERTY VS_DEBUGGER_ENVIRONMENT
        "PATH=${VTK_RUNTIME_DIR};${TBB_RUNTIME_DIR};${LIBUV_RUNTIME_DIR};${LIBTORCH_RUNTIME_DIR};%PATH%")
endif()
studio_copy_runtime(DeepLearningStudio)
