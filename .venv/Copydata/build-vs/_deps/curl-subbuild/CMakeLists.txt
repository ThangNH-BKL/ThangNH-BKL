# Distributed under the OSI-approved BSD 3-Clause License.  See accompanying
# file Copyright.txt or https://cmake.org/licensing for details.

cmake_minimum_required(VERSION 3.31.6-msvc6)

# Reject any attempt to use a toolchain file. We must not use one because
# we could be downloading it here. If the CMAKE_TOOLCHAIN_FILE environment
# variable is set, the cache variable will have been initialized from it.
unset(CMAKE_TOOLCHAIN_FILE CACHE)
unset(ENV{CMAKE_TOOLCHAIN_FILE})

# We name the project and the target for the ExternalProject_Add() call
# to something that will highlight to the user what we are working on if
# something goes wrong and an error message is produced.

project(curl-populate NONE)



include(ExternalProject)
ExternalProject_Add(curl-populate
                     "UPDATE_DISCONNECTED" "False" "DOWNLOAD_EXTRACT_TIMESTAMP" "FALSE" "EXTERNALPROJECT_INTERNAL_ARGUMENT_SEPARATOR" "URL" "D:/Tool/Copydata/CameraFtpSync/CameraFtpSync-source-1.0.0/CameraFtpSync/third_party/curl-8.12.1.tar.xz" "URL_HASH" "SHA256=0341f1ed97a26c811abaebd37d62b833956792b7607ea3f15d001613c76de202"
                    SOURCE_DIR          "D:/Tool/Copydata/CameraFtpSync/CameraFtpSync-source-1.0.0/CameraFtpSync/build-vs/_deps/curl-src"
                    BINARY_DIR          "D:/Tool/Copydata/CameraFtpSync/CameraFtpSync-source-1.0.0/CameraFtpSync/build-vs/_deps/curl-build"
                    CONFIGURE_COMMAND   ""
                    BUILD_COMMAND       ""
                    INSTALL_COMMAND     ""
                    TEST_COMMAND        ""
                    USES_TERMINAL_DOWNLOAD  YES
                    USES_TERMINAL_UPDATE    YES
                    USES_TERMINAL_PATCH     YES
)


