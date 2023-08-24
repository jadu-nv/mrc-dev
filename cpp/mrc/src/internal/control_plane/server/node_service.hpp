/**
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

#include "mrc/core/async_service.hpp"
#include "mrc/runnable/context.hpp"
#include "mrc/runnable/runnable.hpp"
#include "mrc/runnable/runnable_resources.hpp"
#include "mrc/types.hpp"

#include <boost/fiber/mutex.hpp>
#include <node.h>

#include <memory>
#include <stop_token>
#include <string>
#include <thread>
#include <vector>

namespace mrc::control_plane {

class NodeContext : public ::mrc::runnable::Context
{
  public:
  protected:
    void do_init() override;

  private:
    void launch_node(std::vector<std::string> args);

    std::unique_ptr<::node::InitializationResult> m_init_result;
    std::unique_ptr<::node::MultiIsolatePlatform> m_platform;

    std::unique_ptr<::node::CommonEnvironmentSetup> m_setup;
};

class NodeRuntime : public ::mrc::runnable::RunnableWithContext<::mrc::runnable::Context>
{
  public:
    NodeRuntime(std::vector<std::string> args);
    ~NodeRuntime() override;

    void start();
    void stop();
    void kill();

  private:
    void run(::mrc::runnable::Context& ctx) override;
    void on_state_update(const Runnable::State& state) override;

    // void run_node();

    // std::unique_ptr<::node::CommonEnvironmentSetup> node_init_setup(std::vector<std::string> args);
    // void node_run_environment(std::unique_ptr<::node::CommonEnvironmentSetup>);
    void launch_node(std::vector<std::string> args);

    std::unique_ptr<::node::InitializationResult> m_init_result;
    std::unique_ptr<::node::MultiIsolatePlatform> m_platform;

    std::unique_ptr<::node::CommonEnvironmentSetup> m_setup;

    std::vector<std::string> m_args;
};

class NodeService : public AsyncService, public runnable::RunnableResourcesProvider
{
  public:
    NodeService(runnable::IRunnableResourcesProvider& resources, std::vector<std::string> args);
    ~NodeService() override;

  private:
    // void do_service_start() final;
    // void do_service_stop() final;
    // void do_service_kill() final;
    // void do_service_await_live() final;
    // void do_service_await_join() final;

    void do_service_start(std::stop_token stop_token) final;
    void do_service_kill() final;

    void launch_node(std::vector<std::string> args);

    // // mrc resources
    // runnable::RunnableResources& m_runnable;

    std::unique_ptr<::node::InitializationResult> m_init_result;
    std::unique_ptr<::node::MultiIsolatePlatform> m_platform;

    std::unique_ptr<::node::CommonEnvironmentSetup> m_setup;

    std::vector<std::string> m_args;

    mutable boost::fibers::mutex m_mutex;

    std::thread m_node_thread;
    Promise<void> m_started_promise{};
    SharedFuture<void> m_started_future;
    SharedFuture<void> m_completed_future;
};
}  // namespace mrc::control_plane