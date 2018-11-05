// Copyright (c) 2018, NVIDIA CORPORATION. All rights reserved.
//
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions
// are met:
//  * Redistributions of source code must retain the above copyright
//    notice, this list of conditions and the following disclaimer.
//  * Redistributions in binary form must reproduce the above copyright
//    notice, this list of conditions and the following disclaimer in the
//    documentation and/or other materials provided with the distribution.
//  * Neither the name of NVIDIA CORPORATION nor the names of its
//    contributors may be used to endorse or promote products derived
//    from this software without specific prior written permission.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS ``AS IS'' AND ANY
// EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
// IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
// PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE COPYRIGHT OWNER OR
// CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
// EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
// PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
// PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY
// OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
// (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
// OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
//

#include "src/core/metrics.h"

#include <cuda_runtime_api.h>
#include <nvml.h>
#include <thread>
#include "src/core/constants.h"
#include "src/core/logging.h"

namespace nvidia { namespace inferenceserver {

Metrics::Metrics()
    : registry_(std::make_shared<prometheus::Registry>()),
      inf_success_family_(
        prometheus::BuildCounter()
          .Name("nv_inference_request_success")
          .Help("Number of successful inference requests, all batch sizes")
          .Register(*registry_)),
      inf_failure_family_(
        prometheus::BuildCounter()
          .Name("nv_inference_request_failure")
          .Help("Number of failed inference requests, all batch sizes")
          .Register(*registry_)),
      inf_count_family_(prometheus::BuildCounter()
                          .Name("nv_inference_count")
                          .Help("Number of inferences performed")
                          .Register(*registry_)),
      inf_count_exec_family_(prometheus::BuildCounter()
                               .Name("nv_inference_exec_count")
                               .Help("Number of model executions performed")
                               .Register(*registry_)),
      inf_request_duration_us_family_(
        prometheus::BuildCounter()
          .Name("nv_inference_request_duration_us")
          .Help("Cummulative inference request duration in microseconds")
          .Register(*registry_)),
      inf_compute_duration_us_family_(
        prometheus::BuildCounter()
          .Name("nv_inference_compute_duration_us")
          .Help("Cummulative inference compute duration in microseconds")
          .Register(*registry_)),
      inf_queue_duration_us_family_(
        prometheus::BuildCounter()
          .Name("nv_inference_queue_duration_us")
          .Help("Cummulative inference queuing duration in microseconds")
          .Register(*registry_)),
      inf_load_ratio_family_(prometheus::BuildHistogram()
                               .Name("nv_inference_load_ratio")
                               .Register(*registry_)),
      gpu_utilization_family_(prometheus::BuildGauge()
                                .Name("nv_gpu_utilization")
                                .Help("GPU utilization rate [0.0 - 1.0)")
                                .Register(*registry_)),
      gpu_power_usage_family_(prometheus::BuildGauge()
                                .Name("nv_gpu_power_usage")
                                .Help("GPU power usage in watts")
                                .Register(*registry_)),
      gpu_power_limit_family_(prometheus::BuildGauge()
                                .Name("nv_gpu_power_limit")
                                .Help("GPU power management limit in watts")
                                .Register(*registry_)),
      gpu_energy_consumption_family_(
        prometheus::BuildCounter()
          .Name("nv_energy_consumption")
          .Help("GPU energy consumption in joules since the trtserver started")
          .Register(*registry_))
{
}

Metrics::~Metrics()
{
  // Signal the nvml thread to exit and then wait for it...
  if (nvml_thread_ != nullptr) {
    nvml_thread_exit_.store(true);
    nvml_thread_->join();
  }
}

void
Metrics::Initialize(uint32_t port)
{
  auto singleton = GetSingleton();
  if (singleton->exposer_) {
    LOG_WARNING << "Metrics already initialized.";
    return;
  }

  singleton->InitializeNvmlMetrics();

  std::ostringstream stream;
  stream << "0.0.0.0:" << port;
  singleton->exposer_.reset(new prometheus::Exposer(stream.str()));
  singleton->exposer_->RegisterCollectable(singleton->registry_);
}

bool
Metrics::InitializeNvmlMetrics()
{
  nvmlReturn_t nvmlerr = nvmlInit();
  if (nvmlerr != NVML_SUCCESS) {
    LOG_ERROR << "failed to initialize NVML: NVML_ERROR " << nvmlerr;
    return false;
  }

  unsigned int dcnt;
  nvmlerr = nvmlDeviceGetCount(&dcnt);
  if (nvmlerr != NVML_SUCCESS) {
    LOG_ERROR << "failed to get device count for nvml metrics: NVML_ERROR "
              << nvmlerr;
    return false;
  }

  // Create NVML metrics for each GPU
  LOG_INFO << "found " << dcnt << " GPUs supporting NVML metrics";
  for (unsigned int didx = 0; didx < dcnt; ++didx) {
    // Get handle for the GPU
    nvmlDevice_t gpu;
    nvmlReturn_t nvmlerr = nvmlDeviceGetHandleByIndex(didx, &gpu);
    if (nvmlerr == NVML_SUCCESS) {
      char name[NVML_DEVICE_NAME_BUFFER_SIZE + 1];
      if (
        nvmlDeviceGetName(gpu, name, NVML_DEVICE_NAME_BUFFER_SIZE) ==
        NVML_SUCCESS) {
        LOG_INFO << "  GPU " << didx << ": " << name;
      }
    } else {
      LOG_ERROR << "failed to get device handle for GPU " << didx
                << ": NVML_ERROR " << nvmlerr;
      continue;
    }

    std::string uuid;
    char uuid_str[NVML_DEVICE_UUID_BUFFER_SIZE + 1];
    nvmlerr = nvmlDeviceGetUUID(gpu, uuid_str, NVML_DEVICE_UUID_BUFFER_SIZE);
    if (nvmlerr == NVML_SUCCESS) {
      uuid = uuid_str;
    } else {
      uuid = "unknown";
    }

    std::map<std::string, std::string> gpu_labels;
    gpu_labels.insert(std::map<std::string, std::string>::value_type(
      kMetricsLabelGpuUuid, uuid));

    gpu_utilization_.push_back(&gpu_utilization_family_.Add(gpu_labels));
    gpu_power_usage_.push_back(&gpu_power_usage_family_.Add(gpu_labels));
    gpu_power_limit_.push_back(&gpu_power_limit_family_.Add(gpu_labels));
    gpu_energy_consumption_.push_back(
      &gpu_energy_consumption_family_.Add(gpu_labels));
  }

  // Periodically send the NVML metrics...
  if (dcnt > 0) {
    nvml_thread_exit_.store(false);
    nvml_thread_.reset(new std::thread([this, dcnt] {
      unsigned long long last_energy[dcnt];
      for (unsigned int didx = 0; didx < dcnt; ++didx) {
        last_energy[didx] = 0;
      }

      while (!nvml_thread_exit_.load()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(2000));

        for (unsigned int didx = 0; didx < dcnt; ++didx) {
          nvmlDevice_t gpu;
          nvmlReturn_t nvmlerr = nvmlDeviceGetHandleByIndex(didx, &gpu);
          if (nvmlerr != NVML_SUCCESS) {
            LOG_ERROR << "failed to get NVML handle for GPU " << didx
                      << ", NVML_ERROR " << nvmlerr;
          } else {
            // Power limit
            {
              unsigned int power_limit;
              nvmlReturn_t nvmlerr =
                nvmlDeviceGetPowerManagementLimit(gpu, &power_limit);
              if (nvmlerr != NVML_SUCCESS) {
                LOG_ERROR << "failed to get power limit for GPU " << didx
                          << ", NVML_ERROR " << nvmlerr;
                power_limit = 0;
              }
              gpu_power_limit_[didx]->Set((double)power_limit * 0.001);
            }

            // Power usage
            {
              unsigned int power_usage;
              nvmlReturn_t nvmlerr = nvmlDeviceGetPowerUsage(gpu, &power_usage);
              if (nvmlerr != NVML_SUCCESS) {
                LOG_ERROR << "failed to get power usage for GPU " << didx
                          << ", NVML_ERROR " << nvmlerr;
                power_usage = 0;
              }
              gpu_power_usage_[didx]->Set((double)power_usage * 0.001);
            }

            // Energy Consumption
            {
              unsigned long long energy;
              nvmlReturn_t nvmlerr =
                nvmlDeviceGetTotalEnergyConsumption(gpu, &energy);
              if (nvmlerr != NVML_SUCCESS) {
                LOG_ERROR << "failed to get energy consumption for GPU " << didx
                          << ", NVML_ERROR " << nvmlerr;
              } else {
                if (last_energy[didx] == 0) {
                  last_energy[didx] = energy;
                }
                gpu_energy_consumption_[didx]->Increment(
                  (double)(energy - last_energy[didx]) * 0.001);
                last_energy[didx] = energy;
              }
            }

            // Utilization
            {
              nvmlUtilization_t util;
              nvmlReturn_t nvmlerr = nvmlDeviceGetUtilizationRates(gpu, &util);
              if (nvmlerr != NVML_SUCCESS) {
                LOG_ERROR << "failed to get utilization for GPU " << didx
                          << ", NVML_ERROR " << nvmlerr;
                util.gpu = 0;
              }
              gpu_utilization_[didx]->Set((double)util.gpu * 0.01);
            }
          }
        }
      }
    }));
  }

  return true;
}

bool
Metrics::UUIDForCudaDevice(int cuda_device, std::string* uuid)
{
  char pcibusid_str[64];
  cudaError_t cuerr =
    cudaDeviceGetPCIBusId(pcibusid_str, sizeof(pcibusid_str) - 1, cuda_device);
  if (cuerr != cudaSuccess) {
    LOG_ERROR << "failed to get PCI Bus ID for CUDA device " << cuda_device
              << ": " << cudaGetErrorString(cuerr);
    return false;
  }

  nvmlDevice_t device;
  nvmlReturn_t nvmlerr = nvmlDeviceGetHandleByPciBusId(pcibusid_str, &device);
  if (nvmlerr != NVML_SUCCESS) {
    LOG_ERROR << "failed to get device from PCI Bus ID: NVML_ERROR " << nvmlerr;
    return false;
  }

  char uuid_str[NVML_DEVICE_UUID_BUFFER_SIZE + 1];
  nvmlerr = nvmlDeviceGetUUID(device, uuid_str, NVML_DEVICE_UUID_BUFFER_SIZE);
  if (nvmlerr != NVML_SUCCESS) {
    LOG_ERROR << "failed to get device UUID: NVML_ERROR " << nvmlerr;
    return false;
  }

  *uuid = uuid_str;
  return true;
}

std::shared_ptr<prometheus::Registry>
Metrics::GetRegistry()
{
  auto singleton = Metrics::GetSingleton();
  return singleton->registry_;
}

Metrics*
Metrics::GetSingleton()
{
  static Metrics singleton;
  return &singleton;
}

}}  // namespace nvidia::inferenceserver
