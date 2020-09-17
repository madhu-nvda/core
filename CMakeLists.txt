# Copyright (c) 2020, NVIDIA CORPORATION. All rights reserved.
#
# Redistribution and use in source and binary forms, with or without
# modification, are permitted provided that the following conditions
# are met:
#  * Redistributions of source code must retain the above copyright
#    notice, this list of conditions and the following disclaimer.
#  * Redistributions in binary form must reproduce the above copyright
#    notice, this list of conditions and the following disclaimer in the
#    documentation and/or other materials provided with the distribution.
#  * Neither the name of NVIDIA CORPORATION nor the names of its
#    contributors may be used to endorse or promote products derived
#    from this software without specific prior written permission.
#
# THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS ``AS IS'' AND ANY
# EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
# IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
# PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE COPYRIGHT OWNER OR
# CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
# EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
# PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
# PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY
# OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
# (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
# OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

cmake_minimum_required(VERSION 3.17)

project(tritoncore LANGUAGES C CXX)

#
# Options
#
set(TRITON_VERSION "0.0.0" CACHE STRING "Triton version as x.y.z")
option(TRITON_ENABLE_STATS "Enable statistics collection" ON)
option(TRITON_ENABLE_LOGGING "Enable console logging" ON)
option(TRITON_ENABLE_METRICS "Enable metrics collection" OFF)
option(TRITON_ENABLE_METRICS_GPU "Enable GPU metrics collection" OFF)
option(TRITON_ENABLE_TRACING "Enable tracing" OFF)
option(TRITON_ENABLE_NVTX "Enable collection of NVTX when used with profiler" OFF)
option(TRITON_ENABLE_ENSEMBLE "Enable support for model ensembles" OFF)
option(TRITON_ENABLE_GCS "Enable GCS Filesystem support" OFF)
option(TRITON_ENABLE_S3 "Enable S3 Filesystem support" OFF)

option(TRITON_ENABLE_GPU "Enable GPU support" ON)
set(TRITON_MIN_COMPUTE_CAPABILITY "6.0" CACHE STRING "Set the minimum supported CUDA compute capability" )

set(TRITON_COMMON_REPO_TAG "main" CACHE STRING "Tag for triton-inference-server/common repo")

if(TRITON_ENABLE_METRICS AND NOT TRITON_ENABLE_STATS)
  message(FATAL_ERROR "TRITON_ENABLE_METRICS=ON requires TRITON_ENABLE_STATS=ON")
endif()

if(TRITON_ENABLE_TRACING AND NOT TRITON_ENABLE_STATS)
  message(FATAL_ERROR "TRITON_ENABLE_TRACING=ON requires TRITON_ENABLE_STATS=ON")
endif()

if (TRITON_ENABLE_METRICS_GPU AND NOT TRITON_ENABLE_METRICS)
  message(FATAL_ERROR "TRITON_ENABLE_METRICS_GPU=ON requires TRITON_ENABLE_METRICS=ON")
endif()

if (TRITON_ENABLE_METRICS_GPU AND NOT TRITON_ENABLE_GPU)
  message(FATAL_ERROR "TRITON_ENABLE_METRICS_GPU=ON requires TRITON_ENABLE_GPU=ON")
endif()

if (TRITON_ENABLE_NVTX AND NOT TRITON_ENABLE_GPU)
  message(FATAL_ERROR "TRITON_ENABLE_NVTX=ON requires TRITON_ENABLE_GPU=ON")
endif()


if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

include(ExternalProject)

#
# Triton Server API
#
add_library(
  triton-core-serverapi INTERFACE
)

add_library(
  TritonCore::triton-core-serverapi ALIAS triton-core-serverapi
)

target_include_directories(
  triton-core-serverapi
  INTERFACE
    $<INSTALL_INTERFACE:include>
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
)

#
# Triton Backend API
#
add_library(
  triton-core-backendapi INTERFACE
)

add_library(
  TritonCore::triton-core-backendapi ALIAS triton-core-backendapi
)

target_include_directories(
  triton-core-backendapi
  INTERFACE
    $<INSTALL_INTERFACE:include>
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
)

#
# GRPC repo
#
# We build both GRPC and protobuf from the same repo to ensure
# compatibility (though for Triton core probably not necessary)
#
ExternalProject_Add(grpc-repo
  PREFIX grpc-repo
  GIT_REPOSITORY "https://github.com/grpc/grpc.git"
  GIT_TAG "v1.25.0"
  SOURCE_DIR "${CMAKE_CURRENT_BINARY_DIR}/grpc-repo/src/grpc"
  CONFIGURE_COMMAND ""
  BUILD_COMMAND ""
  INSTALL_COMMAND ""
  TEST_COMMAND ""
)

#
# Protobuf
#
# Use -DCMAKE_BUILD_TYPE:STRING=Release to workaround the protobuf
# build issue described in
# https://stackoverflow.com/a/60218066/2091555
ExternalProject_Add(protobuf
  PREFIX protobuf
  SOURCE_DIR "${CMAKE_CURRENT_BINARY_DIR}/grpc-repo/src/grpc/third_party/protobuf/cmake"
  DOWNLOAD_COMMAND ""
  CMAKE_CACHE_ARGS
    -Dprotobuf_BUILD_TESTS:BOOL=OFF
    -Dprotobuf_BUILD_EXAMPLES:BOOL=OFF
    -Dprotobuf_WITH_ZLIB:BOOL=OFF
    -Dprotobuf_MSVC_STATIC_RUNTIME:BOOL=OFF
    -DCMAKE_BUILD_TYPE:STRING=Release
    -DCMAKE_POSITION_INDEPENDENT_CODE:BOOL=ON
    -DCMAKE_INSTALL_PREFIX:PATH=${CMAKE_CURRENT_BINARY_DIR}/protobuf/install
  DEPENDS grpc-repo
)

if (WIN32)
  set(TRITON_CORE_PROTOBUF_CONFIG_DIR "${CMAKE_CURRENT_BINARY_DIR}/protobuf/install/cmake")
else()
  set(TRITON_CORE_PROTOBUF_CONFIG_DIR "${CMAKE_CURRENT_BINARY_DIR}/protobuf/install/lib/cmake/protobuf")
endif()

#
# GRPC
#
# Need for GCS cloud storage. If OPENSSL_ROOT_DIR is set, propagate
# that hint path to GRPC build.
set(TRITON_CORE_OPENSSL_ROOT_DIR "")
if (OPENSSL_ROOT_DIR)
  set(TRITON_CORE_OPENSSL_ROOT_DIR "-DOPENSSL_ROOT_DIR:PATH=${OPENSSL_ROOT_DIR}")
endif()

ExternalProject_Add(c-ares
  PREFIX c-ares
  SOURCE_DIR "${CMAKE_CURRENT_BINARY_DIR}/grpc-repo/src/grpc/third_party/cares/cares"
  DOWNLOAD_COMMAND ""
  CMAKE_CACHE_ARGS
    -DCARES_SHARED:BOOL=OFF
    -DCARES_STATIC:BOOL=ON
    -DCARES_STATIC_PIC:BOOL=ON
    -DCMAKE_BUILD_TYPE:STRING=${CMAKE_BUILD_TYPE}
    -DCMAKE_INSTALL_PREFIX:PATH=${CMAKE_CURRENT_BINARY_DIR}/c-ares/install
  DEPENDS grpc-repo
)

ExternalProject_Add(grpc
  PREFIX grpc
  SOURCE_DIR "${CMAKE_CURRENT_BINARY_DIR}/grpc-repo/src/grpc"
  DOWNLOAD_COMMAND ""
  CMAKE_CACHE_ARGS
    -DgRPC_INSTALL:BOOL=ON
    -DgRPC_BUILD_TESTS:BOOL=OFF
    -DgRPC_PROTOBUF_PROVIDER:STRING=package
    -DgRPC_PROTOBUF_PACKAGE_TYPE:STRING=CONFIG
    -DProtobuf_DIR:PATH=${TRITON_CORE_PROTOBUF_CONFIG_DIR}
    -DgRPC_ZLIB_PROVIDER:STRING=package
    -DgRPC_CARES_PROVIDER:STRING=package
    -Dc-ares_DIR:PATH=${CMAKE_CURRENT_BINARY_DIR}/c-ares/install/lib/cmake/c-ares
    -DgRPC_SSL_PROVIDER:STRING=package
    ${TRITON_CORE_OPENSSL_ROOT_DIR}
    -DCMAKE_BUILD_TYPE:STRING=${CMAKE_BUILD_TYPE}
    -DCMAKE_INSTALL_PREFIX:PATH=${CMAKE_CURRENT_BINARY_DIR}/grpc/install
  DEPENDS grpc-repo c-ares protobuf
)

#
# GCS: google-cloud-cpp
#
# GCS_CMAKE_PREFIX_PATH is necessary because google-cloud-cpp does not
# pass protobuf_DIR and gRPC_DIR etc to its external projects but
# expects them in CMAKE_PREFIX_PATH
ExternalProject_Add(crc32c
  PREFIX crc32c
  GIT_REPOSITORY "https://github.com/google/crc32c.git"
  GIT_TAG "1.1.0"
  CMAKE_CACHE_ARGS
    -DCRC32C_BUILD_TESTS:BOOL=OFF
    -DBUILD_SHARED_LIBS:STRING=no
    -DCRC32C_BUILD_BENCHMARKS:BOOL=OFF
    -DCMAKE_POSITION_INDEPENDENT_CODE:BOOL=ON
    -DCMAKE_INSTALL_PREFIX:PATH=${CMAKE_CURRENT_BINARY_DIR}/crc32c/install
)

set(GCS_CMAKE_PREFIX_PATH ${CMAKE_PREFIX_PATH}
   ${CMAKE_CURRENT_BINARY_DIR}/c-ares/install/lib/cmake/c-ares
   ${CMAKE_CURRENT_BINARY_DIR}/grpc/install/lib/cmake/grpc
   ${CMAKE_CURRENT_BINARY_DIR}/crc32c/install/lib/cmake/Crc32c
   ${TRITON_CORE_PROTOBUF_CONFIG_DIR})

ExternalProject_Add(google-cloud-cpp
  PREFIX google-cloud-cpp
  GIT_REPOSITORY "https://github.com/googleapis/google-cloud-cpp.git"
  GIT_TAG "v0.10.0"
  CMAKE_CACHE_ARGS
    -DGOOGLE_CLOUD_CPP_DEPENDENCY_PROVIDER:STRING=package
    -DBUILD_TESTING:BOOL=OFF
    -DCMAKE_PREFIX_PATH:PATH=${GCS_CMAKE_PREFIX_PATH}
    -DCMAKE_POSITION_INDEPENDENT_CODE:BOOL=ON
    -DCMAKE_INSTALL_PREFIX:PATH=${CMAKE_CURRENT_BINARY_DIR}/google-cloud-cpp/install
  DEPENDS grpc protobuf crc32c
)

#
# S3: AWS sdk
#
ExternalProject_Add(aws-sdk-cpp
  PREFIX aws-sdk-cpp
  GIT_REPOSITORY "https://github.com/aws/aws-sdk-cpp.git"
  GIT_TAG "1.7.129"
  SOURCE_DIR "${CMAKE_CURRENT_BINARY_DIR}/aws-sdk-cpp/src/aws-sdk-cpp"
  CMAKE_CACHE_ARGS
    -DBUILD_ONLY:STRING=s3
    -DBUILD_SHARED_LIBS:BOOL=OFF
    -DMINIMIZE_SIZE:BOOL=ON
    -DENABLE_TESTING:BOOL=OFF
    -DCMAKE_POSITION_INDEPENDENT_CODE:BOOL=ON
    -DCMAKE_INSTALL_PREFIX:PATH=${CMAKE_CURRENT_BINARY_DIR}/aws-sdk-cpp/install
)

#
# Prometheus
#
# Prometheus library is used to collect and report metrics.
#
ExternalProject_Add(prometheus-cpp
  PREFIX prometheus-cpp
  URL "https://github.com/jupp0r/prometheus-cpp/archive/v0.7.0.tar.gz"
  URL_HASH SHA256=93907d937fa7eab9605cba786123d3eba4e87e3dca8ecec93ff9eae4eef8de5a
  CMAKE_CACHE_ARGS
    -DENABLE_PUSH:BOOL=OFF
    -DENABLE_PULL:BOOL=OFF
    -DENABLE_TESTING:BOOL=OFF
    -DUSE_THIRDPARTY_LIBRARIES:BOOL=OFF
    -DCMAKE_POSITION_INDEPENDENT_CODE:BOOL=ON
    -DCMAKE_BUILD_TYPE:STRING=${CMAKE_BUILD_TYPE}
    -DCMAKE_INSTALL_PREFIX:PATH=${CMAKE_CURRENT_BINARY_DIR}/prometheus-cpp/install
)

#
# CNMeM (CUDA memory management library)
#
# Used to sub-allocate from a shared pool of CUDA memory
#
ExternalProject_Add(cnmem
  PREFIX cnmem
  GIT_REPOSITORY "https://github.com/NVIDIA/cnmem.git"
  GIT_TAG "37896cc9bfc6536a8c878a1e675835c22d827821"
  PATCH_COMMAND sed -i "s/SHARED/STATIC/g" ${CMAKE_CURRENT_BINARY_DIR}/cnmem/src/cnmem/CMakeLists.txt
  CMAKE_CACHE_ARGS
    -DCMAKE_POSITION_INDEPENDENT_CODE:BOOL=ON
    -DCMAKE_BUILD_TYPE:STRING=${CMAKE_BUILD_TYPE}
    -DCMAKE_INSTALL_PREFIX:PATH=${CMAKE_CURRENT_BINARY_DIR}/cnmem/install
)

#
# libtritonserver.so
#
# Triton core library. We build this as an external project since it
# depends on the protobuf external project already being built so that
# we can use find_package to discover the protobuf components in that
# build. The actual CMakeLists.txt for the build is
# cmake/CMakeLists.txt.
if (CMAKE_INSTALL_PREFIX_INITIALIZED_TO_DEFAULT)
  set(TRITON_CORE_INSTALL_PREFIX ${CMAKE_CURRENT_BINARY_DIR}/triton-core/install)
else()
  set(TRITON_CORE_INSTALL_PREFIX ${CMAKE_INSTALL_PREFIX})
endif()

set(TRITON_CORE_DEPENDS protobuf)
if(${TRITON_ENABLE_GCS})
  set(TRITON_CORE_DEPENDS ${TRITON_CORE_DEPENDS} google-cloud-cpp)
endif() # TRITON_ENABLE_GCS
if(${TRITON_ENABLE_S3})
  set(TRITON_CORE_DEPENDS ${TRITON_CORE_DEPENDS} aws-sdk-cpp)
endif() # TRITON_ENABLE_S3
if(${TRITON_ENABLE_METRICS})
  set(TRITON_CORE_DEPENDS ${TRITON_CORE_DEPENDS} prometheus-cpp)
endif() # TRITON_ENABLE_METRICS
if(${TRITON_ENABLE_GPU})
  set(TRITON_CORE_DEPENDS ${TRITON_CORE_DEPENDS} cnmem)
endif() # TRITON_ENABLE_GPU

ExternalProject_Add(triton-core
  PREFIX triton-core
  SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/src"
  BINARY_DIR "${CMAKE_CURRENT_BINARY_DIR}/triton-core"
  CMAKE_CACHE_ARGS
    -DProtobuf_DIR:PATH=${TRITON_CORE_PROTOBUF_CONFIG_DIR}
    -Dprometheus-cpp_DIR:PATH=${CMAKE_CURRENT_BINARY_DIR}/prometheus-cpp/install/lib/cmake/prometheus-cpp
    -Dstorage_client_DIR:PATH=${CMAKE_CURRENT_BINARY_DIR}/google-cloud-cpp/install/lib/cmake/storage_client
    -Dgoogle_cloud_cpp_common_DIR:PATH=${CMAKE_CURRENT_BINARY_DIR}/google-cloud-cpp/install/lib/cmake/google_cloud_cpp_common
    -DCrc32c_DIR:PATH=${CMAKE_CURRENT_BINARY_DIR}/crc32c/install/lib/cmake/Crc32c
    -DAWSSDK_DIR:PATH=${CMAKE_CURRENT_BINARY_DIR}/aws-sdk-cpp/install/lib/cmake/AWSSDK
    -Daws-cpp-sdk-core_DIR:PATH=${CMAKE_CURRENT_BINARY_DIR}/aws-sdk-cpp/install/lib/cmake/aws-cpp-sdk-core
    -Daws-cpp-sdk-s3_DIR:PATH=${CMAKE_CURRENT_BINARY_DIR}/aws-sdk-cpp/install/lib/cmake/aws-cpp-sdk-s3
    -Daws-c-event-stream_DIR:PATH=${CMAKE_CURRENT_BINARY_DIR}/aws-sdk-cpp/install/lib/aws-c-event-stream/cmake
    -Daws-c-common_DIR:PATH=${CMAKE_CURRENT_BINARY_DIR}/aws-sdk-cpp/install/lib/aws-c-common/cmake
    -Daws-checksums_DIR:PATH=${CMAKE_CURRENT_BINARY_DIR}/aws-sdk-cpp/install/lib/aws-checksums/cmake
    -DCNMEM_INCLUDE_DIRS:PATH=${CMAKE_CURRENT_BINARY_DIR}/cnmem/install/include
    -DCMAKE_BUILD_TYPE:STRING=${CMAKE_BUILD_TYPE}
    -DCMAKE_INSTALL_PREFIX:PATH=${TRITON_CORE_INSTALL_PREFIX}
    -DTRITON_COMMON_REPO_TAG:STRING=${TRITON_COMMON_REPO_TAG}
    -DTRITON_VERSION:STRING=${TRITON_VERSION}
    -DTRITON_ENABLE_GPU:BOOL=${TRITON_ENABLE_GPU}
    -DTRITON_MIN_COMPUTE_CAPABILITY:STRING=${TRITON_MIN_COMPUTE_CAPABILITY}
    -DTRITON_ENABLE_STATS:BOOL=${TRITON_ENABLE_STATS}
    -DTRITON_ENABLE_LOGGING:BOOL=${TRITON_ENABLE_LOGGING}
    -DTRITON_ENABLE_METRICS:BOOL=${TRITON_ENABLE_METRICS}
    -DTRITON_ENABLE_METRICS_GPU:BOOL=${TRITON_ENABLE_METRICS_GPU}
    -DTRITON_ENABLE_TRACING:BOOL=${TRITON_ENABLE_TRACING}
    -DTRITON_ENABLE_NVTX:BOOL=${TRITON_ENABLE_NVTX}
    -DTRITON_ENABLE_ENSEMBLE:BOOL=${TRITON_ENABLE_ENSEMBLE}
    -DTRITON_ENABLE_GCS:BOOL=${TRITON_ENABLE_GCS}
    -DTRITON_ENABLE_S3:BOOL=${TRITON_ENABLE_S3}
  DEPENDS ${TRITON_CORE_DEPENDS}
)
