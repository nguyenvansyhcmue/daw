# CMake generated Testfile for 
# Source directory: D:/Stu
# Build directory: D:/Stu/build-perf
# 
# This file includes the relevant testing commands required for 
# testing this directory and lists subdirectories to be tested as well.
if(CTEST_CONFIGURATION_TYPE MATCHES "^([Dd][Ee][Bb][Uu][Gg])$")
  add_test([=[StudioForgeCoreTests]=] "D:/Stu/build-perf/Debug/StudioForgeCoreTests.exe")
  set_tests_properties([=[StudioForgeCoreTests]=] PROPERTIES  _BACKTRACE_TRIPLES "D:/Stu/CMakeLists.txt;178;add_test;D:/Stu/CMakeLists.txt;0;")
elseif(CTEST_CONFIGURATION_TYPE MATCHES "^([Rr][Ee][Ll][Ee][Aa][Ss][Ee])$")
  add_test([=[StudioForgeCoreTests]=] "D:/Stu/build-perf/Release/StudioForgeCoreTests.exe")
  set_tests_properties([=[StudioForgeCoreTests]=] PROPERTIES  _BACKTRACE_TRIPLES "D:/Stu/CMakeLists.txt;178;add_test;D:/Stu/CMakeLists.txt;0;")
elseif(CTEST_CONFIGURATION_TYPE MATCHES "^([Mm][Ii][Nn][Ss][Ii][Zz][Ee][Rr][Ee][Ll])$")
  add_test([=[StudioForgeCoreTests]=] "D:/Stu/build-perf/MinSizeRel/StudioForgeCoreTests.exe")
  set_tests_properties([=[StudioForgeCoreTests]=] PROPERTIES  _BACKTRACE_TRIPLES "D:/Stu/CMakeLists.txt;178;add_test;D:/Stu/CMakeLists.txt;0;")
elseif(CTEST_CONFIGURATION_TYPE MATCHES "^([Rr][Ee][Ll][Ww][Ii][Tt][Hh][Dd][Ee][Bb][Ii][Nn][Ff][Oo])$")
  add_test([=[StudioForgeCoreTests]=] "D:/Stu/build-perf/RelWithDebInfo/StudioForgeCoreTests.exe")
  set_tests_properties([=[StudioForgeCoreTests]=] PROPERTIES  _BACKTRACE_TRIPLES "D:/Stu/CMakeLists.txt;178;add_test;D:/Stu/CMakeLists.txt;0;")
else()
  add_test([=[StudioForgeCoreTests]=] NOT_AVAILABLE)
endif()
subdirs("_deps/juce-build")
