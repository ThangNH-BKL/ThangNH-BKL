# CMake generated Testfile for 
# Source directory: D:/Project_Python/.venv/Copydata/CameraFtpSync
# Build directory: D:/Project_Python/.venv/Copydata/build-ninja
# 
# This file includes the relevant testing commands required for 
# testing this directory and lists subdirectories to be tested as well.
add_test([=[core_fault_injection]=] "D:/Project_Python/.venv/Copydata/build-ninja/tls-fix/CameraCoreTests.exe")
set_tests_properties([=[core_fault_injection]=] PROPERTIES  _BACKTRACE_TRIPLES "D:/Project_Python/.venv/Copydata/CameraFtpSync/CMakeLists.txt;71;add_test;D:/Project_Python/.venv/Copydata/CameraFtpSync/CMakeLists.txt;0;")
add_test([=[streaming_resources]=] "D:/Project_Python/.venv/Copydata/build-ninja/tls-fix/CameraResourceTests.exe")
set_tests_properties([=[streaming_resources]=] PROPERTIES  _BACKTRACE_TRIPLES "D:/Project_Python/.venv/Copydata/CameraFtpSync/CMakeLists.txt;74;add_test;D:/Project_Python/.venv/Copydata/CameraFtpSync/CMakeLists.txt;0;")
subdirs("_deps/curl-build")
