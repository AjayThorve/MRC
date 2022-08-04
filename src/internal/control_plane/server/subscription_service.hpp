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
#include "internal/control_plane/server/tagged_service.hpp"

#include "srf/protos/architect.pb.h"
#include "srf/types.hpp"

#include <string>

namespace srf::internal::control_plane::server {

class Role;

/**
 * @brief A specialize TaggedService to synchronize tag and instance_id information across between a collection of
 * client-side objects with common linkages, e.g. the Publisher/Subscriber services which form the building blocks for
 * Ingress/EgressPorts use instances of SubscriptionService for Publishers to get control plane updates to the list of
 * Subscribers.
 *
 * The PubSub example is specialzied example of the more generic SubscriptionService. This example has two roles:
 * {"publisher", "subscriber"}, where the publisher gets updates on the subscriber role, but the subscribers only
 * register as members and do not receive publisher updates.
 */
class SubscriptionService final : public TaggedService
{
  public:
    SubscriptionService(std::string name, std::set<std::string> roles);
    ~SubscriptionService() final = default;

    tag_t register_instance(std::shared_ptr<server::ClientInstance> instance,
                            const std::string& role,
                            const std::set<std::string>& subscribe_to_roles);

    bool has_role(const std::string& role) const;
    bool compare_roles(const std::set<std::string>& roles) const;

  private:
    void add_role(const std::string& name);
    Role& get_role(const std::string& name);

    void do_drop_tag(const tag_t& tag) final;
    void do_issue_update() final;

    std::string m_name;

    // roles are defines at construction time in the body of the constructor
    // no new keys should be added
    std::map<std::string, std::unique_ptr<Role>> m_roles;
};

/**
 * @brief Component of SubscriptionService that holds state for each Role
 *
 * A Role has a set of members and subscribers. When either list is updated, the Role's nonce is incremented. When the
 * nonce is greater than the value of the nonce on last update, an update can be issued by calling issue_update.
 *
 * An issue_update will send a protos::SubscriptionServiceUpdate to all subscribers containing the (tag, instance_id)
 * tuple for each item in the members list.
 */
class Role
{
  public:
    Role(std::string service_name, std::string role_name) :
      m_service_name(std::move(service_name)),
      m_role_name(std::move(role_name))
    {}

    // subscribers are notified when new members are added
    void add_member(std::uint64_t tag, std::shared_ptr<server::ClientInstance> instance);
    void add_subscriber(std::uint64_t tag, std::shared_ptr<server::ClientInstance> instance);

    // drop a client instance - this will remove the instaces from both the
    // members and subscribers list
    void drop_tag(std::uint64_t tag);

    // if dirty, issue update
    void issue_update();

  private:
    protos::SubscriptionServiceUpdate make_update() const;

    static void await_update(const std::shared_ptr<server::ClientInstance>& instance,
                             const protos::SubscriptionServiceUpdate& update);

    std::string m_service_name;
    std::string m_role_name;
    std::map<std::uint64_t, std::shared_ptr<server::ClientInstance>> m_members;
    std::map<std::uint64_t, std::shared_ptr<server::ClientInstance>> m_subscribers;
    std::size_t m_nonce{1};
    std::size_t m_last_update{1};
};

}  // namespace srf::internal::control_plane::server
