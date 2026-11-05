function(studio_add_test target test_name source)
    add_executable(${target} ${source} ${ARGN})
    target_link_libraries(${target} PRIVATE DeepLearningStudioLib)
    set_target_properties(${target} PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/.test-artifacts/$<CONFIG>")
    studio_copy_runtime(${target})
    add_test(NAME ${test_name} COMMAND ${target})
endfunction()

studio_add_test(image_viewport_transform_test image_viewport_transform
    image_viewport_transform_test.cpp)
studio_add_test(dataset_visual_test dataset_visual_contract
    dataset_visual_test.cpp "${CMAKE_SOURCE_DIR}/resources/studio.qrc")
studio_add_test(workflow_engine_test workflow_engine
    workflow_test.cpp)
studio_add_test(workflow_document_test workflow_document_roundtrip
    workflow_document_test.cpp)
studio_add_test(project_manager_test project_manager_roundtrip
    project_manager_test.cpp)
studio_add_test(task_workspace_service_test task_workspace_domain
    task_workspace_service_test.cpp)
studio_add_test(annotation_exchange_test annotation_exchange_contract
    annotation_exchange_test.cpp)
studio_add_test(augmentation_dialog_test augmentation_dialog_contract
    augmentation_dialog_test.cpp)
studio_add_test(workflow_scene_test workflow_scene_interactions
    workflow_scene_test.cpp)
target_link_libraries(workflow_scene_test PRIVATE Qt5::Test)
studio_add_test(main_window_test main_window_integration
    main_window_test.cpp "${CMAKE_SOURCE_DIR}/resources/studio.qrc")
studio_add_test(shell_ui_test shell_ui_contract
    shell_ui_test.cpp "${CMAKE_SOURCE_DIR}/resources/studio.qrc" "${STUDIO_TRANSLATIONS_QRC}")
add_dependencies(shell_ui_test studio_translations)
target_link_libraries(shell_ui_test PRIVATE Qt5::Test)
studio_add_test(feature_module_test feature_module_contract
    feature_module_test.cpp)
studio_add_test(weight_conversion_test weight_conversion_contract
    weight_conversion_test.cpp "${CMAKE_SOURCE_DIR}/resources/studio.qrc")
