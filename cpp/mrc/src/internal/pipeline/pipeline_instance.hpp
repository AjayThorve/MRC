/*
 * SPDX-FileCopyrightText: Copyright (c) 2021-2023, NVIDIA CORPORATION & AFFILIATES. All rights reserved.
 * SPDX-License-Identifier: Apache-2.0
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#pragma once

#include "internal/control_plane/state/root_state.hpp"
#include "internal/service.hpp"

#include "mrc/core/async_service.hpp"
#include "mrc/types.hpp"

#include <cstdint>
#include <map>
#include <memory>

namespace mrc::runtime {
class Runtime;
}
namespace mrc::resources {
class SystemResources;
}  // namespace mrc::resources
namespace mrc::segment {
class SegmentInstance;
}  // namespace mrc::segment
namespace mrc::manifold {
struct Interface;
}  // namespace mrc::manifold

namespace mrc::pipeline {
class PipelineDefinition;
class ManifoldInstance;

class PipelineInstance final : public AsyncService, public runnable::RunnableResourcesProvider
{
  public:
    PipelineInstance(runtime::Runtime& runtime,
                     std::shared_ptr<const PipelineDefinition> definition,
                     uint64_t instance_id);
    ~PipelineInstance() override;

    std::shared_ptr<manifold::Interface> get_manifold(const PortName& port_name);

    // currently we are passing the instance back to the executor
    // we should own the instance here in the pipeline instance
    // we need to stage those object that are created into some struct/container so we can mass start them after all
    // object have been created
    void create_segment(const SegmentAddress& address, std::uint32_t partition_id);
    void stop_segment(const SegmentAddress& address);
    void join_segment(const SegmentAddress& address);
    void remove_segment(const SegmentAddress& address);

    /**
     * @brief Start all Segments and Manifolds
     *
     * This call is idempotent. You can call it multiple times and it will simply ensure that all segments owned by this
     * pipeline instance have been started. Any Segment that natually shutdowns down is still owned by the Pipeline
     * Instance until the configuration manager explicitly tells the Pipeline Instace to remove it.
     *
     */
    void update();

  private:
    void do_service_start(std::stop_token stop_token) final;
    void process_state_update(control_plane::state::PipelineInstance& instance);
    // void do_service_start() final;
    // void do_service_await_live() final;
    // void do_service_stop() final;
    // void do_service_kill() final;
    // void do_service_await_join() final;

    void mark_joinable();

    manifold::Interface& manifold(const PortName& port_name);

    runtime::Runtime& m_runtime;

    std::shared_ptr<const PipelineDefinition> m_definition;  // convert to pipeline::Pipeline

    uint64_t m_instance_id;

    std::map<SegmentAddress, std::unique_ptr<segment::SegmentInstance>> m_segments;
    std::map<PortName, std::shared_ptr<ManifoldInstance>> m_manifold_instances;

    bool m_joinable{false};
    Promise<void> m_joinable_promise;
    SharedFuture<void> m_joinable_future;
};

}  // namespace mrc::pipeline
