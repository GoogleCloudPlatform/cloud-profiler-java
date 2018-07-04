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

#ifndef CLOUD_PROFILER_AGENT_JAVA_UPLOADER_GCS_H_
#define CLOUD_PROFILER_AGENT_JAVA_UPLOADER_GCS_H_

#include <cstdio>

#include "src/cloud_env.h"
#include "src/uploader.h"

namespace cloud {
namespace profiler {

class CloudEnv;
class HTTPRequest;

// Profile uploader for Google Cloud Storage. Uses the credentials of the Google
// Compute Engine instance the agent runs on for the authentication. If the GCE
// environment can't be detected or if instance credentials are not authorized
// for the write access to the configured GCS path, the uploads will fail.
class GcsUploader : public cloud::profiler::ProfileUploader {
 public:
  // Constructs an uploader to GCS which uses the specified caller-owned
  // environment object and the specified profile path prefix.
  GcsUploader(CloudEnv* env, const string& prefix)
      : env_(env), prefix_(prefix) {}

  // Implements ProfileUploader interface.
  bool Upload(const string& profile_type, const string& profile) override;

 private:
  CloudEnv* env_;
  string prefix_;
  DISALLOW_COPY_AND_ASSIGN(GcsUploader);
};

}  // namespace profiler
}  // namespace cloud

#endif  // CLOUD_PROFILER_AGENT_JAVA_UPLOADER_GCS_H_
