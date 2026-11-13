# Distributed under the OSI-approved BSD 3-Clause License.  See accompanying
# file Copyright.txt or https://cmake.org/licensing for details.

cmake_minimum_required(VERSION ${CMAKE_VERSION}) # this file comes with cmake

if("D:/Tool/Copydata/CameraFtpSync/CameraFtpSync-source-1.0.0/CameraFtpSync/third_party/curl-8.12.1.tar.xz" STREQUAL "")
  message(FATAL_ERROR "LOCAL can't be empty")
endif()

if(NOT EXISTS "D:/Tool/Copydata/CameraFtpSync/CameraFtpSync-source-1.0.0/CameraFtpSync/third_party/curl-8.12.1.tar.xz")
  message(FATAL_ERROR "File not found: D:/Tool/Copydata/CameraFtpSync/CameraFtpSync-source-1.0.0/CameraFtpSync/third_party/curl-8.12.1.tar.xz")
endif()

if("SHA256" STREQUAL "")
  message(WARNING "File cannot be verified since no URL_HASH specified")
  return()
endif()

if("0341f1ed97a26c811abaebd37d62b833956792b7607ea3f15d001613c76de202" STREQUAL "")
  message(FATAL_ERROR "EXPECT_VALUE can't be empty")
endif()

message(VERBOSE "verifying file...
     file='D:/Tool/Copydata/CameraFtpSync/CameraFtpSync-source-1.0.0/CameraFtpSync/third_party/curl-8.12.1.tar.xz'")

file("SHA256" "D:/Tool/Copydata/CameraFtpSync/CameraFtpSync-source-1.0.0/CameraFtpSync/third_party/curl-8.12.1.tar.xz" actual_value)

if(NOT "${actual_value}" STREQUAL "0341f1ed97a26c811abaebd37d62b833956792b7607ea3f15d001613c76de202")
  message(FATAL_ERROR "error: SHA256 hash of
  D:/Tool/Copydata/CameraFtpSync/CameraFtpSync-source-1.0.0/CameraFtpSync/third_party/curl-8.12.1.tar.xz
does not match expected value
  expected: '0341f1ed97a26c811abaebd37d62b833956792b7607ea3f15d001613c76de202'
    actual: '${actual_value}'
")
endif()

message(VERBOSE "verifying file... done")
