add_executable(cadforge_tests test_model.cpp)
target_link_libraries(cadforge_tests PRIVATE cadforge::model)
cadforge_set_warnings(cadforge_tests)
add_test(NAME cadforge_tests COMMAND cadforge_tests)
