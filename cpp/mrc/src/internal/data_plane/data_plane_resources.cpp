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

#include "internal/data_plane/data_plane_resources.hpp"

#include "internal/data_plane/callbacks.hpp"
#include "internal/data_plane/client.hpp"
#include "internal/data_plane/server.hpp"
#include "internal/memory/host_resources.hpp"
#include "internal/ucx/endpoint.hpp"
#include "internal/ucx/ucx_resources.hpp"
#include "internal/ucx/worker.hpp"

#include "mrc/memory/literals.hpp"

#include <ucp/api/ucp.h>

#include <memory>

namespace mrc::data_plane {

using namespace mrc::memory::literals;

DataPlaneResources::DataPlaneResources(resources::PartitionResourceBase& base,
                                       ucx::UcxResources& ucx,
                                       memory::HostResources& host,
                                       const InstanceID& instance_id,
                                       control_plane::Client& control_plane_client) :
  resources::PartitionResourceBase(base),
  Service("DataPlaneResources"),
  m_ucx(ucx),
  m_host(host),
  m_control_plane_client(control_plane_client),
  m_instance_id(instance_id),
  m_transient_pool(32_MiB, 4, m_host.registered_memory_resource()),
  m_server(std::make_unique<Server>(base, ucx, host, m_transient_pool, m_instance_id))
//   m_client(std::make_unique<Client>(base, ucx, m_control_plane_client.connections(), m_transient_pool))
{
    // ensure the data plane progress engine is up and running
    service_start();
    service_await_live();
}

DataPlaneResources::~DataPlaneResources()
{
    Service::call_in_destructor();
}

Client& DataPlaneResources::client()
{
    return *m_client;
}

std::string DataPlaneResources::ucx_address() const
{
    return m_ucx.worker().address();
}

const ucx::RegistrationCache& DataPlaneResources::registration_cache() const
{
    return m_ucx.registration_cache();
}

void DataPlaneResources::do_service_start()
{
    m_server->service_start();
    m_client->service_start();
}

void DataPlaneResources::do_service_await_live()
{
    m_server->service_await_live();
    m_client->service_await_live();
}

void DataPlaneResources::do_service_stop()
{
    // we only issue
    m_client->service_stop();
}

void DataPlaneResources::do_service_kill()
{
    m_server->service_kill();
    m_client->service_kill();
}

void DataPlaneResources::do_service_await_join()
{
    m_client->service_await_join();
    m_server->service_stop();
    m_server->service_await_join();
}

Server& DataPlaneResources::server()
{
    return *m_server;
}

mrc::runnable::LaunchOptions DataPlaneResources::launch_options(std::size_t concurrency)
{
    return ucx::UcxResources::launch_options(concurrency);
}

const InstanceID& DataPlaneResources::instance_id() const
{
    return m_instance_id;
}

DataPlaneResources2::DataPlaneResources2()
{
    DVLOG(10) << "initializing ucx context";
    m_context = std::make_shared<ucx::Context>();

    DVLOG(10) << "initialize a ucx data_plane worker";
    m_worker = std::make_shared<ucx::Worker>(m_context);

    DVLOG(10) << "initialize the registration cache for this context";
    m_registration_cache = std::make_shared<ucx::RegistrationCache>(m_context);

    // flush any work that needs to be done by the workers
    this->flush();
}

DataPlaneResources2::~DataPlaneResources2() {}

ucx::Context& DataPlaneResources2::context() const
{
    return *m_context;
}

ucx::Worker& DataPlaneResources2::worker() const
{
    return *m_worker;
}

std::shared_ptr<ucx::Endpoint> DataPlaneResources2::create_endpoint(const ucx::WorkerAddress& address)
{
    return std::make_shared<ucx::Endpoint>(m_worker, address);
}

uint32_t DataPlaneResources2::progress()
{
    // Forward the worker once
    return m_worker->progress();
}

void DataPlaneResources2::flush()
{
    while (m_worker->progress() != 0)
    {
        // Loop again
    }
}

std::shared_ptr<Request> DataPlaneResources2::send_async(const ucx::Endpoint& endpoint,
                                                         void* addr,
                                                         std::size_t bytes,
                                                         std::uint64_t tag)
{
    auto* request_ptr = new std::shared_ptr<Request>();

    auto request = std::make_shared<Request>();

    // Increment the request's reference count to ensure that it is not deleted until the callback is run
    *request_ptr = request;

    CHECK_EQ(request->m_request, nullptr);
    CHECK(request->m_state == Request::State::Init);
    request->m_state = Request::State::Running;

    ucp_request_param_t send_params;
    send_params.op_attr_mask = UCP_OP_ATTR_FIELD_CALLBACK | UCP_OP_ATTR_FIELD_USER_DATA | UCP_OP_ATTR_FLAG_NO_IMM_CMPL;
    send_params.cb.send      = Callbacks::send2;

    // Set the user data to the request pointer so that the callback can decrement the reference count
    send_params.user_data = request_ptr;

    request->m_request = ucp_tag_send_nbx(endpoint.handle(), addr, bytes, tag, &send_params);
    CHECK(request->m_request);
    CHECK(!UCS_PTR_IS_ERR(request->m_request));

    return request;
}
std::shared_ptr<Request> DataPlaneResources2::receive_async(void* addr,
                                                            std::size_t bytes,
                                                            std::uint64_t tag,
                                                            std::uint64_t mask)
{
    auto* request_ptr = new std::shared_ptr<Request>();

    auto request = std::make_shared<Request>();

    // Increment the request's reference count to ensure that it is not deleted until the callback is run
    *request_ptr = request;

    CHECK_EQ(request->m_request, nullptr);
    CHECK(request->m_state == Request::State::Init);
    request->m_state = Request::State::Running;

    ucp_request_param_t params;
    params.op_attr_mask = UCP_OP_ATTR_FIELD_CALLBACK | UCP_OP_ATTR_FIELD_USER_DATA | UCP_OP_ATTR_FLAG_NO_IMM_CMPL;
    params.cb.recv      = Callbacks::recv2;
    params.user_data    = &request;

    request->m_request = ucp_tag_recv_nbx(m_worker->handle(), addr, bytes, tag, mask, &params);
    CHECK(request->m_request);
    CHECK(!UCS_PTR_IS_ERR(request->m_request));

    return request;
}

}  // namespace mrc::data_plane
