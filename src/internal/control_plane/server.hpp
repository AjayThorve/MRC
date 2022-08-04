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
#include "internal/control_plane/server/subscription_service.hpp"
#include "internal/expected.hpp"
#include "internal/grpc/server.hpp"
#include "internal/grpc/server_streaming.hpp"
#include "internal/runnable/resources.hpp"
#include "internal/service.hpp"

#include "srf/channel/status.hpp"
#include "srf/node/queue.hpp"
#include "srf/protos/architect.grpc.pb.h"
#include "srf/protos/architect.pb.h"
#include "srf/runnable/runner.hpp"

#include <boost/fiber/condition_variable.hpp>
#include <boost/fiber/recursive_mutex.hpp>
#include <google/protobuf/repeated_ptr_field.h>

#include <chrono>
#include <map>
#include <memory>
#include <string>

namespace srf::internal::control_plane {

/**
 * @brief Control Plane Server
 *
 * The Control Plane Server is an asynchronous bi-directional grpc server with persistent stream connections to the
 * Control Plane Clients. The primary function of the server is to provide global state for all clients and a method
 * to exchange connection information like UCX worker addresses.
 *
 * The server must be resilient to termination, meaning we can not use glog's CHECK statement to validate assumptions.
 * We will use C++ exceptions that throw a srf::internal::Error to replace the std::abort of a failed CHECK/ASSERT.
 * To indicate "softer" errors, perhaps configuration errors by the client or mismatched state between client and server
 * as failed Expected. All top-level event handlers should return an Expected<Message> where message is the type of
 * message which will be returned to the client. The write methods will check the state of the Expected<Message> and
 * send back either the Message or an Error with the proper error code and error message.
 */
class Server : public Service
{
  public:
    using stream_t      = std::shared_ptr<rpc::ServerStream<srf::protos::Event, srf::protos::Event>>;
    using writer_t      = std::shared_ptr<rpc::StreamWriter<srf::protos::Event>>;
    using event_t       = stream_t::element_type::IncomingData;
    using instance_t    = std::shared_ptr<server::ClientInstance>;
    using stream_id_t   = std::size_t;
    using instance_id_t = std::size_t;

    Server(runnable::Resources& runnable);

  private:
    void do_service_start() final;
    void do_service_stop() final;
    void do_service_kill() final;
    void do_service_await_live() final;
    void do_service_await_join() final;

    void do_accept_stream(rxcpp::subscriber<stream_t>& s);
    void do_handle_event(event_t&& event);
    void do_issue_update(rxcpp::subscriber<void*>& s);

    // srf resources
    runnable::Resources& m_runnable;

    // grpc
    rpc::Server m_server;
    std::shared_ptr<srf::protos::Architect::AsyncService> m_service;

    // connection info
    std::map<stream_id_t, stream_t> m_streams;
    std::map<instance_id_t, instance_t> m_instances;
    std::multimap<stream_id_t, instance_id_t> m_instances_by_stream;
    std::set<std::string> m_ucx_worker_addresses;

    // subscription services
    std::map<std::string, std::unique_ptr<server::SubscriptionService>> m_subscription_services;

    // operators / queues
    std::unique_ptr<srf::node::Queue<event_t>> m_queue;

    // runners
    std::unique_ptr<srf::runnable::Runner> m_stream_acceptor;
    std::unique_ptr<srf::runnable::Runner> m_event_handler;
    std::unique_ptr<srf::runnable::Runner> m_update_handler;

    // state mutex/cv/timeout
    mutable boost::fibers::mutex m_mutex;
    boost::fibers::condition_variable m_update_cv;
    std::chrono::milliseconds m_update_period{30000};

    // top-level event handlers - these methods lock internal state
    Expected<protos::RegisterWorkersResponse> unary_register_workers(event_t& event);
    Expected<protos::Ack> unary_create_subscription_service(event_t& event);
    Expected<protos::RegisterSubscriptionServiceResponse> unary_register_subscription_service(event_t& event);
    Expected<protos::Ack> unary_drop_from_subscription_service(event_t& event);
    void drop_stream(writer_t writer);
    void on_fatal_exception();

    // methods used to create and issue responses
    template <typename MessageT>
    static Expected<> unary_response(event_t& event, Expected<MessageT>&& message);

    // convenience methods - these method do not lock internal state
    Expected<instance_t> get_instance(const instance_id_t& instance_id) const;
    Expected<instance_t> validate_instance_id(const instance_id_t& instance_id, const event_t& event) const;
    Expected<decltype(m_subscription_services)::const_iterator> get_subscription_service(const std::string& name) const;

    // protobuf convenience methods
    template <typename T>
    static Expected<std::set<T>> check_unique_repeated_field(const google::protobuf::RepeatedPtrField<T>& items)
    {
        std::set<T> unique(items.begin(), items.end());
        if (unique.size() != items.size())
        {
            return Error::create("non-unique repeated field; duplicated detected");
        }
        return unique;
    }

    template <typename T>
    static Expected<T> unpack_request(event_t& event)
    {
        T msg;
        if (event.msg.has_message() && event.msg.message().UnpackTo(&msg))
        {
            return msg;
        }
        if (event.msg.has_error())
        {
            return Error::create(event.msg.error().message());
        }
        return Error::create("unable to unpack request; client an unexpected message type");
    }
};

template <typename MessageT>
Expected<> Server::unary_response(event_t& event, Expected<MessageT>&& message)
{
    if (!message)
    {
        protos::Error error;
        error.set_code(protos::ErrorCode::InstanceError);
        error.set_message(message.error().message());
        return unary_response<protos::Error>(event, std::move(error));
    }
    srf::protos::Event out;
    out.set_tag(event.msg.tag());
    out.set_event(protos::EventType::Response);
    out.mutable_message()->PackFrom(*message);
    if (event.stream->await_write(std::move(out)) != channel::Status::success)
    {
        return Error::create("failed to write to channel");
    }
    return {};
}

}  // namespace srf::internal::control_plane
