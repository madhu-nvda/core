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

project(tritoncorelibrary LANGUAGES C CXX)

#
# Dependencies
#
include(FetchContent)

FetchContent_Declare(
  repo-common
  GIT_REPOSITORY https://github.com/triton-inference-server/common.git
  GIT_TAG ${TRITON_COMMON_REPO_TAG}
  GIT_SHALLOW ON
)
FetchContent_MakeAvailable(repo-common)

#
# CUDA
#
if(${TRITON_ENABLE_GPU})
  find_package(CUDAToolkit REQUIRED)
  message(STATUS "Using CUDA Toolkit ${CUDAToolkit_VERSION}")

  if(CUDAToolkit_VERSION VERSION_GREATER "10.1" OR CUDAToolkit_VERSION VERSION_EQUAL "10.1")
    message(STATUS "CUDA graphs are supported.")
  else()
    message(WARNING "CUDA ${CUDAToolkit_VERSION} does not support CUDA graphs.")
  endif()
endif() # TRITON_ENABLE_GPU

#
# Boost
#
find_package(Boost REQUIRED)
message(STATUS "Using Boost ${Boost_VERSION}")

#
# Protobuf
#
set(protobuf_MODULE_COMPATIBLE TRUE CACHE BOOL "protobuf_MODULE_COMPATIBLE" FORCE)
find_package(Protobuf CONFIG REQUIRED)
message(STATUS "Using protobuf ${Protobuf_VERSION}")

#
# Prometheus
#
if(${TRITON_ENABLE_METRICS})
  find_package(prometheus-cpp CONFIG REQUIRED)
  message(STATUS "Using prometheus-cpp ${prometheus-cpp_VERSION}")
endif() # TRITON_ENABLE_METRICS

#
# Cloud storage SDKs
#
if(${TRITON_ENABLE_GCS})
  find_package(storage_client REQUIRED)
  message(STATUS "Using google-cloud-cpp ${storage_client_VERSION}")
  set_source_files_properties(
    filesystem.cc
    PROPERTIES
    COMPILE_FLAGS -Wno-missing-field-initializers
  )
endif() # TRITON_ENABLE_GCS

if(${TRITON_ENABLE_S3})
  find_package(AWSSDK REQUIRED COMPONENTS s3)
  message(STATUS "Using aws-sdk-cpp ${AWSSDK_VERSION}")
endif()

#
# Model configuration protobuf
#
protobuf_generate_cpp(PROTO_SRCS PROTO_HDRS model_config.proto)
add_library(
  triton-core-modelconfig
  ${PROTO_SRCS} ${PROTO_HDRS}
)

add_library(
  TritonCore::triton-core-modelconfig ALIAS triton-core-modelconfig
)

target_include_directories(
  triton-core-modelconfig
  PUBLIC
    $<INSTALL_INTERFACE:include>
    $<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}>
    ${Protobuf_INCLUDE_DIRS}
)

target_compile_features(triton-core-modelconfig PRIVATE cxx_std_11)
target_compile_options(
  triton-core-modelconfig
  PRIVATE
    -Wall -Wextra -Wno-unused-parameter -Werror
)

set_target_properties(
  triton-core-modelconfig
  PROPERTIES
    POSITION_INDEPENDENT_CODE ON
)

target_link_libraries(
  triton-core-modelconfig
  ${PROTOBUF_LIBRARY}
)

#
# Core library
#
configure_file(libtritonserver.ldscript libtritonserver.ldscript COPYONLY)

add_library(
  triton-core SHARED
  autofill.h
  autofill.cc
  backend.h
  backend.cc
  backend_context.h
  backend_context.cc
  constants.h
  cuda_memory_manager.h
  cuda_memory_manager.cc
  cuda_utils.h
  cuda_utils.cc
  dynamic_batch_scheduler.h
  dynamic_batch_scheduler.cc
  ensemble_scheduler.h
  ensemble_scheduler.cc
  ensemble_utils.h
  ensemble_utils.cc
  filesystem.h
  filesystem.cc
  infer_parameter.h
  infer_parameter.cc
  infer_request.h
  infer_request.cc
  infer_response.h
  infer_response.cc
  infer_stats.h
  infer_stats.cc
  infer_trace.h
  infer_trace.cc
  label_provider.h
  label_provider.cc
  logging.h
  logging.cc
  memory.h
  memory.cc
  metric_model_reporter.h
  metric_model_reporter.cc
  metrics.h
  metrics.cc
  model_config.h
  model_config.cc
  model_config_utils.h
  model_config_utils.cc
  model_repository_manager.h
  model_repository_manager.cc
  nvtx.h
  pinned_memory_manager.h
  pinned_memory_manager.cc
  response_allocator.h
  scheduler.h
  scheduler_utils.h
  scheduler_utils.cc
  sequence_batch_scheduler.h
  sequence_batch_scheduler.cc
  server.h
  server.cc
  server_message.h
  status.h
  status.cc
  sync_queue.h
  tritonserver.cc
)

add_library(
  TritonCore::triton-core ALIAS triton-core
)

target_include_directories(
  triton-core
  PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../include
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${Boost_INCLUDE_DIRS}
    ${Protobuf_INCLUDE_DIRS}
)
if(${TRITON_ENABLE_GPU})
  target_include_directories(
    triton-core
    PRIVATE
      ${CNMEM_INCLUDE_DIRS}
  )
endif() # TRITON_ENABLE_GPU

target_compile_features(triton-core PRIVATE cxx_std_11)
target_compile_options(
  triton-core
  PRIVATE
    -Wall -Wextra -Wno-unused-parameter -Werror
)

target_compile_definitions(
  triton-core
  PRIVATE TRITON_VERSION="${TRITON_VERSION}"
)

if(${TRITON_ENABLE_STATS})
target_compile_definitions(
  triton-core
  PRIVATE TRITON_ENABLE_STATS=1
)
endif() # TRITON_ENABLE_STATS
if(${TRITON_ENABLE_LOGGING})
target_compile_definitions(
  triton-core
  PRIVATE TRITON_ENABLE_LOGGING=1
)
endif() # TRITON_ENABLE_LOGGING
if(${TRITON_ENABLE_METRICS})
target_compile_definitions(
  triton-core
  PRIVATE TRITON_ENABLE_METRICS=1
)
  if(${TRITON_ENABLE_METRICS_GPU})
    target_compile_definitions(
      triton-core
      PRIVATE TRITON_ENABLE_METRICS_GPU=1
    )
  endif() # TRITON_ENABLE_METRICS_GPU
endif() # TRITON_ENABLE_METRICS
if(${TRITON_ENABLE_TRACING})
target_compile_definitions(
  triton-core
  PRIVATE TRITON_ENABLE_TRACING=1
)
endif() # TRITON_ENABLE_TRACING
if(${TRITON_ENABLE_NVTX})
target_compile_definitions(
  triton-core
  PRIVATE TRITON_ENABLE_NVTX=1
)
endif() # TRITON_ENABLE_NVTX
if(${TRITON_ENABLE_ENSEMBLE})
target_compile_definitions(
  triton-core
  PRIVATE TRITON_ENABLE_ENSEMBLE=1
)
endif() # TRITON_ENABLE_ENSEMBLE
if(${TRITON_ENABLE_GCS})
target_compile_definitions(
  triton-core
  PRIVATE TRITON_ENABLE_GCS=1
)
endif() # TRITON_ENABLE_GCS
if(${TRITON_ENABLE_S3})
target_compile_definitions(
  triton-core
  PRIVATE TRITON_ENABLE_S3=1
)
endif() # TRITON_ENABLE_S3
if(${TRITON_ENABLE_GPU})
target_compile_definitions(
  triton-core
  PRIVATE TRITON_ENABLE_GPU=1
  PRIVATE TRITON_MIN_COMPUTE_CAPABILITY=${TRITON_MIN_COMPUTE_CAPABILITY}
)
if(CUDAToolkit_VERSION VERSION_GREATER "10.1" OR CUDAToolkit_VERSION VERSION_EQUAL "10.1")
  target_compile_definitions(
    triton-core
    PRIVATE TRITON_ENABLE_CUDA_GRAPH=1
  )
endif()
endif() # TRITON_ENABLE_GPU

set_target_properties(
  triton-core
  PROPERTIES
    POSITION_INDEPENDENT_CODE ON
    OUTPUT_NAME tritonserver
    SKIP_BUILD_RPATH TRUE
    BUILD_WITH_INSTALL_RPATH TRUE
    INSTALL_RPATH_USE_LINK_PATH FALSE
    INSTALL_RPATH "$\{ORIGIN\}:$\{ORIGIN\}/pytorch"
    LINK_DEPENDS ${CMAKE_CURRENT_BINARY_DIR}/libtritonserver.ldscript
    LINK_FLAGS "-Wl,--version-script libtritonserver.ldscript"
)

target_link_libraries(
  triton-core
  PRIVATE
    triton-common-json        # from repo-common
    triton-core-modelconfig
)

if(${TRITON_ENABLE_GPU})
  target_link_libraries(
    triton-core
    PRIVATE
      CUDA::cudart
  )
endif() # TRITON_ENABLE_GPU
if(${TRITON_ENABLE_METRICS})
  target_link_libraries(
    triton-core
    PRIVATE
      prometheus-cpp::core
  )
endif() # TRITON_ENABLE_METRIC
if(${TRITON_ENABLE_GCS})
  target_link_libraries(
    triton-core
    PRIVATE
      storage_client
  )
endif() # TRITON_ENABLE_GCS
if(${TRITON_ENABLE_S3})
  target_link_libraries(
    triton-core
    PRIVATE
      aws-cpp-sdk-s3
  )
endif() # TRITON_ENABLE_S3

#
# Install
#
include(GNUInstallDirs)
set(INSTALL_CONFIGDIR ${CMAKE_INSTALL_LIBDIR}/TritonCore)

install(
  TARGETS
    triton-core
  EXPORT
    triton-core-targets
  LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
  ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
)

install(
  EXPORT
    triton-core-targets
  FILE
    TritonCoreTargets.cmake
  NAMESPACE
    TritonCore::
  DESTINATION
    ${INSTALL_CONFIGDIR}
)

include(CMakePackageConfigHelpers)
configure_package_config_file(
  ${CMAKE_CURRENT_LIST_DIR}/../cmake/TritonCoreConfig.cmake.in
  ${CMAKE_CURRENT_BINARY_DIR}/TritonCoreConfig.cmake
  INSTALL_DESTINATION ${INSTALL_CONFIGDIR}
)

install(
  FILES
    ${CMAKE_CURRENT_BINARY_DIR}/TritonCoreConfig.cmake
  DESTINATION
    ${INSTALL_CONFIGDIR}
)

#
# Export from build tree
#
export(
  EXPORT
    triton-core-targets
  FILE
    ${CMAKE_CURRENT_BINARY_DIR}/TritonCoreTargets.cmake
  NAMESPACE
    TritonCore::
)

export(PACKAGE TritonCore)
