#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>

#include "framework.h"
#include "webgpu-headers/webgpu.h"

#define LOG_PREFIX "[push_constants]"

static void handle_request_adapter(WGPURequestAdapterStatus status,
                                   WGPUAdapter adapter, WGPUStringView message,
                                   void *userdata1, void *userdata2) {
  UNUSED(status)
  UNUSED(message)
  UNUSED(userdata2)
  *(WGPUAdapter *)userdata1 = adapter;
}
static void handle_request_device(WGPURequestDeviceStatus status,
                                  WGPUDevice device, WGPUStringView message,
                                  void *userdata1, void *userdata2) {
  UNUSED(status)
  UNUSED(message)
  UNUSED(userdata2)
  *(WGPUDevice *)userdata1 = device;
}
static void handle_buffer_map(WGPUMapAsyncStatus status, 
                              WGPUStringView message,
                              void *userdata1, void *userdata2) {
  UNUSED(userdata1)
  UNUSED(userdata2)
  printf(LOG_PREFIX " buffer_map status=%#.8x\n", status);
}

int main(int argc, char *argv[]) {
  UNUSED(argc)
  UNUSED(argv)
  frmwrk_setup_logging(WGPULogLevel_Warn);

  uint32_t numbers[] = {0, 0, 0, 0};
  uint32_t numbers_size = sizeof(numbers);
  uint32_t numbers_length = numbers_size / sizeof(uint32_t);

  WGPUInstance instance = wgpuCreateInstance(NULL);
  assert(instance);

  WGPUAdapter adapter = NULL;
  wgpuInstanceRequestAdapter(instance, NULL,
                             (const WGPURequestAdapterCallbackInfo){
                                 .callback = handle_request_adapter,
                                 .userdata1 = &adapter
                             });
  assert(adapter);

  WGPUNativeLimits supported_limits_extras = {
      .chain =
          {
              .sType = WGPUSType_NativeLimits,
          },
      .maxPushConstantSize = 0,
  };
  WGPULimits supported_limits = {
      .nextInChain = &supported_limits_extras.chain,
  };
  wgpuAdapterGetLimits(adapter, &supported_limits);

  WGPUFeatureName requiredFeatures[] = {
      WGPUNativeFeature_PushConstants,
  };
  WGPUDeviceDescriptor device_desc = {
      .label = {"compute_device", WGPU_STRLEN},
      .requiredFeatures = requiredFeatures,
      .requiredFeatureCount = 1,
      .requiredLimits = &supported_limits,
  };

  WGPUDevice device = NULL;
  wgpuAdapterRequestDevice(adapter, &device_desc, 
                           (const WGPURequestDeviceCallbackInfo){ 
                               .callback = handle_request_device,
                               .userdata1 = &device
                           });
  assert(device);

  WGPUQueue queue = wgpuDeviceGetQueue(device);
  assert(queue);

  WGPUShaderModule shader_module =
      frmwrk_load_shader_module(device, "shader.wgsl");
  assert(shader_module);

  WGPUBuffer storage_buffer = wgpuDeviceCreateBuffer(
      device, &(const WGPUBufferDescriptor){
                  .label = {"storage_buffer", WGPU_STRLEN},
                  .usage = WGPUBufferUsage_Storage | WGPUBufferUsage_CopyDst |
                           WGPUBufferUsage_CopySrc,
                  .size = numbers_size,
                  .mappedAtCreation = false,
              });
  assert(storage_buffer);

  WGPUBuffer staging_buffer = wgpuDeviceCreateBuffer(
      device, &(const WGPUBufferDescriptor){
                  .label = {"staging_buffer", WGPU_STRLEN},
                  .usage = WGPUBufferUsage_MapRead | WGPUBufferUsage_CopyDst,
                  .size = numbers_size,
                  .mappedAtCreation = false,
              });
  assert(staging_buffer);

  WGPUPushConstantRange push_constant_range = {
      .stages = WGPUShaderStage_Compute,
      .start = 0,
      .end = sizeof(uint32_t),
  };

  WGPUPipelineLayoutExtras pipeline_layout_extras = {
      .chain =
          {
              .sType = WGPUSType_PipelineLayoutExtras,
          },
      .pushConstantRangeCount = 1,
      .pushConstantRanges = &push_constant_range,
  };

  WGPUBindGroupLayoutEntry bind_group_layout_entries[] = {
      {
          .binding = 0,
          .visibility = WGPUShaderStage_Compute,
          .buffer =
              {
                  .type = WGPUBufferBindingType_Storage,
              },
      },
  };
  WGPUBindGroupLayoutDescriptor bind_group_layout_desc = {
      .label = {"bind_group_layout", WGPU_STRLEN},
      .nextInChain = NULL,
      .entryCount = 1,
      .entries = bind_group_layout_entries,
  };
  WGPUBindGroupLayout bind_group_layout =
      wgpuDeviceCreateBindGroupLayout(device, &bind_group_layout_desc);
  assert(bind_group_layout);

  WGPUPipelineLayoutDescriptor pipeline_layout_desc = {
      .label = {"pipeline_layout", WGPU_STRLEN},
      .nextInChain = &pipeline_layout_extras.chain,
      .bindGroupLayouts = &bind_group_layout,
      .bindGroupLayoutCount = 1,
  };
  WGPUPipelineLayout pipeline_layout =
      wgpuDeviceCreatePipelineLayout(device, &pipeline_layout_desc);
  assert(pipeline_layout);

  WGPUComputePipeline compute_pipeline = wgpuDeviceCreateComputePipeline(
      device, &(const WGPUComputePipelineDescriptor){
                  .label = {"compute_pipeline", WGPU_STRLEN},
                  .compute =
                      (const WGPUProgrammableStageDescriptor){
                          .module = shader_module,
                          .entryPoint = {"main", WGPU_STRLEN},
                      },
                  .layout = pipeline_layout,
              });
  assert(compute_pipeline);

  WGPUBindGroup bind_group = wgpuDeviceCreateBindGroup(
      device, &(const WGPUBindGroupDescriptor){
                  .label = {"bind_group", WGPU_STRLEN},
                  .layout = bind_group_layout,
                  .entryCount = 1,
                  .entries =
                      (const WGPUBindGroupEntry[]){
                          (const WGPUBindGroupEntry){
                              .binding = 0,
                              .buffer = storage_buffer,
                              .offset = 0,
                              .size = numbers_size,
                          },
                      },
              });
  assert(bind_group);

  WGPUCommandEncoder command_encoder = wgpuDeviceCreateCommandEncoder(
      device, &(const WGPUCommandEncoderDescriptor){
                  .label = {"command_encoder", WGPU_STRLEN},
              });
  assert(command_encoder);

  WGPUComputePassEncoder compute_pass_encoder =
      wgpuCommandEncoderBeginComputePass(command_encoder,
                                         &(const WGPUComputePassDescriptor){
                                             .label = {"compute_pass", WGPU_STRLEN},
                                         });
  assert(compute_pass_encoder);

  wgpuComputePassEncoderSetPipeline(compute_pass_encoder, compute_pipeline);
  wgpuComputePassEncoderSetBindGroup(compute_pass_encoder, 0, bind_group, 0,
                                     NULL);

  for (uint32_t i = 0; i < numbers_length; i++) {
    uint32_t pushConst = i;
    wgpuComputePassEncoderSetPushConstants(compute_pass_encoder, 0,
                                           sizeof(uint32_t), &pushConst);

    wgpuComputePassEncoderDispatchWorkgroups(compute_pass_encoder,
                                             numbers_length, 1, 1);
  }

  wgpuComputePassEncoderEnd(compute_pass_encoder);
  wgpuComputePassEncoderRelease(compute_pass_encoder);

  wgpuCommandEncoderCopyBufferToBuffer(command_encoder, storage_buffer, 0,
                                       staging_buffer, 0, numbers_size);

  WGPUCommandBuffer command_buffer = wgpuCommandEncoderFinish(
      command_encoder, &(const WGPUCommandBufferDescriptor){
                           .label = {"command_buffer", WGPU_STRLEN},
                       });
  assert(command_buffer);

  wgpuQueueWriteBuffer(queue, storage_buffer, 0, &numbers, numbers_size);
  wgpuQueueSubmit(queue, 1, &command_buffer);

  wgpuBufferMapAsync(staging_buffer, WGPUMapMode_Read, 0, numbers_size,
                     (const WGPUBufferMapCallbackInfo){
                         .callback = handle_buffer_map
                     });
  wgpuDevicePoll(device, true, NULL);

  uint32_t *buf =
      (uint32_t *)wgpuBufferGetMappedRange(staging_buffer, 0, numbers_size);
  assert(buf);

  printf("times: [%d, %d, %d, %d]\n", buf[0], buf[1], buf[2], buf[3]);

  wgpuBufferUnmap(staging_buffer);
  wgpuCommandBufferRelease(command_buffer);
  wgpuCommandEncoderRelease(command_encoder);
  wgpuBindGroupRelease(bind_group);
  wgpuBindGroupLayoutRelease(bind_group_layout);
  wgpuComputePipelineRelease(compute_pipeline);
  wgpuBufferRelease(storage_buffer);
  wgpuBufferRelease(staging_buffer);
  wgpuShaderModuleRelease(shader_module);
  wgpuQueueRelease(queue);
  wgpuDeviceRelease(device);
  wgpuAdapterRelease(adapter);
  wgpuInstanceRelease(instance);
  return EXIT_SUCCESS;
}
