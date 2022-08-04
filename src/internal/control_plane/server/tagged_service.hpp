/**
 * SPDX-FileCopyrightText: Copyright (c) 2021-2022, NVIDIA CORPORATION & AFFILIATES. All rights reserved.
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

#include "internal/control_plane/server/client_instance.hpp"

#include "srf/utils/macros.hpp"
#include "srf/utils/string_utils.hpp"

#include <cstdint>
#include <stdexcept>

namespace srf::internal::control_plane::server {

/**
 * @brief Creates masked tags.
 *
 * Limit UINT32_MAX tagged object can be created in a given process before reaching the overflow limit.
 * Each tagged object can issues UINT16_MAX unique tags.
 */
class Tagged
{
  public:
    using tag_t = std::uint64_t;

    Tagged()          = default;
    virtual ~Tagged() = 0;

    DELETE_COPYABILITY(Tagged);
    DELETE_MOVEABILITY(Tagged);

    // a valid tag masks out both the upper and lower 16-bits
    // and compares the value against the instances tag() value
    bool valid_tag(const tag_t& tag) const;

    tag_t upper_bound() const;
    tag_t lower_bound() const;

  protected:
    tag_t next_tag();

  private:
    static tag_t next();

    const tag_t m_tag{next()};
    std::uint16_t m_uid{1};
};

/**
 * @brief Server-side Service class that ensures each registered instance has a unique tag and that all tags are
 * assocated with an instance_id
 *
 * This is the primary base class for a control plane server-side stateful service which can be updated by the client
 * and state updates driven independently via the issue_update() method.
 *
 * TaggedService is not thread-safe or protected in anyway. The global state mutex should protect all TaggedServices.
 *
 * In most scenarios, the service side will have a batched updated which will periodically visit each TaggedService and
 * call issue_update(); however, depending on the service request/update message, the call may also require an immediate
 * update.
 */
class TaggedService : public Tagged
{
    virtual void do_drop_tag(const tag_t& tag) = 0;
    virtual void do_issue_update()             = 0;

  public:
    ~TaggedService() override;

    void drop_instance(std::shared_ptr<ClientInstance> instance);
    void drop_instance(ClientInstance::instance_id_t instance_id);
    void drop_tag(tag_t tag);
    void drop_all();

    void issue_update();

    std::size_t tag_count() const;
    std::size_t tag_count_for_instance_id(ClientInstance::instance_id_t instance_id) const;

  protected:
    tag_t register_instance_id(ClientInstance::instance_id_t instance_id);

  private:
    std::multimap<ClientInstance::instance_id_t, tag_t> m_instance_tags;

    decltype(m_instance_tags)::iterator drop_tag(decltype(m_instance_tags)::iterator it);
};

}  // namespace srf::internal::control_plane::server
