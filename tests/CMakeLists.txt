add_executable(cadforge_tests test_main.cpp test_model.cpp test_sketch.cpp test_naming.cpp test_fea.cpp)
target_link_libraries(cadforge_tests PRIVATE cadforge::model cadforge::fea)
cadforge_set_warnings(cadforge_tests)
add_test(NAME cadforge_tests COMMAND cadforge_tests)
