/* Copyright 2023 The TensorFlow Authors. All Rights Reserved.

Licensed under the Apache License, Version 2.0 (the "License");
you may not use this file except in compliance with the License.
You may obtain a copy of the License at

    http://www.apache.org/licenses/LICENSE-2.0

Unless required by applicable law or agreed to in writing, software
distributed under the License is distributed on an "AS IS" BASIS,
WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
See the License for the specific language governing permissions and
limitations under the License.
==============================================================================*/

#ifndef XLA_STREAM_EXECUTOR_COMMAND_BUFFER_H_
#define XLA_STREAM_EXECUTOR_COMMAND_BUFFER_H_

#include <cstdint>
#include <memory>
#include <tuple>

#include "absl/functional/any_invocable.h"
#include "xla/stream_executor/kernel.h"
#include "xla/stream_executor/launch_dim.h"
#include "xla/stream_executor/platform/port.h"
#include "tsl/platform/errors.h"
#include "tsl/platform/status.h"
#include "tsl/platform/statusor.h"

namespace stream_executor {

class Stream;
class StreamExecutor;

namespace internal {
class CommandBufferInterface;
}

//===----------------------------------------------------------------------===//
// CommandBuffer
//===----------------------------------------------------------------------===//

// Command buffer represent a "bundle of work items" for StreamExecutor device
// that can be submitted with one API call, e.g. command buffer might have
// multiple device kernels and synchronization barriers between them. Command
// buffers allow to amortize the cost of launching "work" on device by building
// it on the host ahead of time without expensive interaction with underlying
// device.
//
// TODO(ezhulenev): Add a concept of a "nested" command buffer which can't be
// submitted on its own, but has to be recorded into the parent command buffer.
// For CUDA backend nested command buffers will never be instantiated into
// executable graphs, but instead will only have a regular graph instance. We
// should almost always trace into nested command buffers.
class CommandBuffer {
 public:
  //===--------------------------------------------------------------------===//
  // Command buffer constructors
  //===--------------------------------------------------------------------===//

  // Creates a new empty command buffer on the given executor.
  static tsl::StatusOr<CommandBuffer> Create(StreamExecutor* executor);

  // Creates a new command buffer on the given executor by tracing `function`
  // invocation. All StreamExecutor operations on a Stream argument will be
  // recorded into the command buffer. Returned command buffer is finalized, and
  // can't be updated.
  //
  // Command buffer tracing should be used only when it is impossible to use
  // explicit construction APIs, e.g. when calling external libraries.
  static tsl::StatusOr<CommandBuffer> Trace(
      StreamExecutor* executor,
      absl::AnyInvocable<tsl::Status(Stream*)> function);

  //===--------------------------------------------------------------------===//
  // Command buffer API
  //===--------------------------------------------------------------------===//

  // Adds a kernel launch command to the command buffer.
  tsl::Status Launch(const ThreadDim& threads, const BlockDim& blocks,
                     const KernelBase& kernel, const KernelArgsArrayBase& args);

  // Adds a device-to-device memory copy to the command buffer.
  tsl::Status MemcpyDeviceToDevice(DeviceMemoryBase* dst,
                                   const DeviceMemoryBase& src, uint64_t size);

  // Finalizes command buffer and makes it executable. Once command buffer is
  // finalized no commands can be added to it.
  tsl::Status Finalize();

  // Type-safe wrapper for launching typed kernels. Notice that the order of
  // arguments is different do disambiguate from the regular launch API.
  template <typename... Params, typename... Args>
  tsl::Status Launch(const TypedKernel<Params...>& kernel,
                     const ThreadDim& threads, const BlockDim& blocks,
                     Args... args);

  internal::CommandBufferInterface* implementation() {
    return implementation_.get();
  }

  const internal::CommandBufferInterface* implementation() const {
    return implementation_.get();
  }

  explicit CommandBuffer(
      std::unique_ptr<internal::CommandBufferInterface> implementation);

  CommandBuffer(CommandBuffer&&) = default;
  CommandBuffer& operator=(CommandBuffer&&) = default;

 private:
  std::unique_ptr<internal::CommandBufferInterface> implementation_;

  SE_DISALLOW_COPY_AND_ASSIGN(CommandBuffer);
};

//===----------------------------------------------------------------------===//
// CommandBuffer templates implementation below
//===----------------------------------------------------------------------===//

template <typename... Params, typename... Args>
inline tsl::Status CommandBuffer::Launch(const TypedKernel<Params...>& kernel,
                                         const ThreadDim& threads,
                                         const BlockDim& blocks, Args... args) {
  KernelInvocationChecker<std::tuple<Params...>,
                          std::tuple<Args...>>::CheckAllStaticAssert();

  KernelArgsArray<sizeof...(args)> kernel_args;
  kernel.PackParams(&kernel_args, args...);
  TF_RETURN_IF_ERROR(Launch(threads, blocks, kernel, kernel_args));
  return tsl::OkStatus();
}

}  // namespace stream_executor

#endif  // XLA_STREAM_EXECUTOR_COMMAND_BUFFER_H_
