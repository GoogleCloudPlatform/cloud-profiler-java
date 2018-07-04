/*
 * Copyright 2018 Google LLC
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#ifndef CLOUD_PROFILER_AGENT_JAVA_THROTTLER_API_H_
#define CLOUD_PROFILER_AGENT_JAVA_THROTTLER_API_H_

#include <memory>
#include <random>
#include <vector>

#include "src/clock.h"
#include "src/cloud_env.h"
#include "src/throttler.h"
#include "google/devtools/cloudprofiler/v2/profiler.grpc.pb.h"

#include "grpcpp/client_context.h"
#include "grpcpp/support/status.h"

namespace cloud {
namespace profiler {

// Throttler implementation using the Cloud Profiler API.
class APIThrottler : public Throttler {
 public:
  APIThrottler();
  // Testing-only constructor.
  APIThrottler(CloudEnv* env, Clock* clock,
               std::unique_ptr<google::devtools::cloudprofiler::v2::grpc::
                                   ProfilerService::StubInterface>
                   stub);

  // Set the list of supported profile types. The list is used in the profile
  // creation call to the server to specify the supported types.
  void SetProfileTypes(
      const std::vector<google::devtools::cloudprofiler::v2::ProfileType>&
          types);

  bool WaitNext() override;
  string ProfileType() override;
  int64_t DurationNanos() override;
  bool Upload(string profile) override;

 private:
  // Takes a backoff on profile creation error. The backoff duration
  // may be specified by the server. Otherwise it will be a randomized
  // exponentially increasing value, bounded by kMaxBackoffNanos.
  void OnCreationError(const grpc::ClientContext& ctx, const grpc::Status& st);

 private:
  CloudEnv* env_;
  Clock* clock_;
  std::unique_ptr<
      google::devtools::cloudprofiler::v2::grpc::ProfilerService::StubInterface>
      stub_;
  google::devtools::cloudprofiler::v2::Profile profile_;
  std::vector<google::devtools::cloudprofiler::v2::ProfileType> types_;

  // Profile creation error handling.
  int64_t creation_backoff_envelope_ns_;
  std::default_random_engine gen_;
  std::uniform_int_distribution<int64_t> dist_;
};

}  // namespace profiler
}  // namespace cloud

#endif  // CLOUD_PROFILER_AGENT_JAVA_THROTTLER_API_H_
