# CMake generated Testfile for 
# Source directory: /home/vector/Downloads/synth
# Build directory: /home/vector/Downloads/synth/build
# 
# This file includes the relevant testing commands required for 
# testing this directory and lists subdirectories to be tested as well.
add_test(core_unit_tests "/home/vector/Downloads/synth/build/tests")
set_tests_properties(core_unit_tests PROPERTIES  _CMAKE_TEST_BUILD_DEPENDS "/home/vector/Downloads/synth/build/tests" _BACKTRACE_TRIPLES "/home/vector/Downloads/synth/CMakeLists.txt;156;add_test;/home/vector/Downloads/synth/CMakeLists.txt;0;")
add_test(system_integration_tests "/home/vector/Downloads/synth/build/integration_tests")
set_tests_properties(system_integration_tests PROPERTIES  _CMAKE_TEST_BUILD_DEPENDS "/home/vector/Downloads/synth/build/integration_tests" _BACKTRACE_TRIPLES "/home/vector/Downloads/synth/CMakeLists.txt;157;add_test;/home/vector/Downloads/synth/CMakeLists.txt;0;")
