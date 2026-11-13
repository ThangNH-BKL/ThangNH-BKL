# Distributed under the OSI-approved BSD 3-Clause License.  See accompanying
# file Copyright.txt or https://cmake.org/licensing for details.

cmake_minimum_required(VERSION ${CMAKE_VERSION}) # this file comes with cmake

if("D:/Project_Python/.venv/Copydata/CameraFtpSync/third_party/curl-8.23.0.tar.xz" STREQUAL "")
  message(FATAL_ERROR "LOCAL can't be empty")
endif()

if(NOT EXISTS "D:/Project_Python/.venv/Copydata/CameraFtpSync/third_party/curl-8.23.0.tar.xz")
  message(FATAL_ERROR "File not found: D:/Project_Python/.venv/Copydata/CameraFtpSync/third_party/curl-8.23.0.tar.xz")
endif()

if("SHA256" STREQUAL "")
  message(WARNING "File cannot be verified since no URL_HASH specified")
  return()
endif()

if("8dadd7d2536a314f982ad05a651b713f426bf7d02cc2c76cffb9796f2218e6c5" STREQUAL "")
  message(FATAL_ERROR "EXPECT_VALUE can't be empty")
endif()

message(VERBOSE "verifying file...
     file='D:/Project_Python/.venv/Copydata/CameraFtpSync/third_party/curl-8.23.0.tar.xz'")

file("SHA256" "D:/Project_Python/.venv/Copydata/CameraFtpSync/third_party/curl-8.23.0.tar.xz" actual_value)

if(NOT "${actual_value}" STREQUAL "8dadd7d2536a314f982ad05a651b713f426bf7d02cc2c76cffb9796f2218e6c5")
  message(FATAL_ERROR "error: SHA256 hash of
  D:/Project_Python/.venv/Copydata/CameraFtpSync/third_party/curl-8.23.0.tar.xz
does not match expected value
  expected: '8dadd7d2536a314f982ad05a651b713f426bf7d02cc2c76cffb9796f2218e6c5'
    actual: '${actual_value}'
")
endif()

message(VERBOSE "verifying file... done")
