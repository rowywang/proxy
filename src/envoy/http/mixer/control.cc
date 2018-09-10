/* Copyright 2017 Istio Authors. All Rights Reserved.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *    http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "src/envoy/http/mixer/control.h"
#include "include/istio/utils/local_attributes.h"

using ::istio::mixer::v1::Attributes;
using ::istio::utils::LocalNode;

namespace Envoy {
namespace Http {
namespace Mixer {

Control::Control(const Config& config, Upstream::ClusterManager& cm,
                 Event::Dispatcher& dispatcher,
                 Runtime::RandomGenerator& random, Stats::Scope& scope,
                 Utils::MixerFilterStats& stats,
                 const LocalInfo::LocalInfo& local_info)
    : config_(config),
      check_client_factory_(Utils::GrpcClientFactoryForCluster(
          config_.check_cluster(), cm, scope, dispatcher.timeSource())),
      report_client_factory_(Utils::GrpcClientFactoryForCluster(
          config_.report_cluster(), cm, scope, dispatcher.timeSource())),
      stats_obj_(dispatcher, stats,
                 config_.config_pb().transport().stats_update_interval(),
                 [this](::istio::mixerclient::Statistics* stat) -> bool {
                   return GetStats(stat);
                 }) {
  auto& logger = Logger::Registry::getLog(Logger::Id::config);
  LocalNode local_node;
  if (!Utils::ExtractNodeInfo(local_info.node(), &local_node)) {
    ENVOY_LOG_TO_LOGGER(logger, warn, "Unable to get node metadata");
  }
  ::istio::utils::SerializeForwardedAttributes(local_node,
                                               &serialized_forward_attributes_);

  ::istio::control::http::Controller::Options options(config_.config_pb(),
                                                      local_node);

  Utils::CreateEnvironment(dispatcher, random, *check_client_factory_,
                           *report_client_factory_,
                           serialized_forward_attributes_, &options.env);

  controller_ = ::istio::control::http::Controller::Create(options);
}

Utils::CheckTransport::Func Control::GetCheckTransport(
    Tracing::Span& parent_span) {
  return Utils::CheckTransport::GetFunc(*check_client_factory_, parent_span,
                                        serialized_forward_attributes_);
}

// Call controller to get statistics.
bool Control::GetStats(::istio::mixerclient::Statistics* stat) {
  if (!controller_) {
    return false;
  }
  controller_->GetStatistics(stat);
  return true;
}

}  // namespace Mixer
}  // namespace Http
}  // namespace Envoy
