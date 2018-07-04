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

#ifndef CLOUD_PROFILER_AGENT_JAVA_CLOUD_ENV_H_
#define CLOUD_PROFILER_AGENT_JAVA_CLOUD_ENV_H_

#include "src/globals.h"

namespace cloud {
namespace profiler {

class HTTPRequest;

// Agent cloud environment (creds, project ID etc.), mockable for testing.
// The implementation is thread-unsafe, the caller must synchronize the access.
class CloudEnv {
 public:
  CloudEnv();
  virtual ~CloudEnv() {}

  // Returns the current cloud project ID.
  virtual string ProjectID();

  // Returns the current cloud zone (e.g. 'us-central1-a').
  virtual string ZoneName();

  // Returns a valid OAuth2 access token representing the service account
  // identity of the instance or an empty string if the instance does not have a
  // service account assigned or an error occurred while trying to fetch the
  // access token. The token may carry limited set of OAuth2 scopes, so the
  // later use of the token for a specific operation may fail with an
  // authorization error.
  virtual string Oauth2AccessToken();

  // Returns the profiled service name for the current environment.
  virtual string Service();

  // Returns the profiled service version for the current environment.
  virtual string ServiceVersion();

  // Implements the method using the given HTTP request for communication.
  // Visible for testing.
  string ProjectID(HTTPRequest* req);

  // Implements the method using the given HTTP request for communication.
  // Visible for testing.
  string ZoneName(HTTPRequest* req);

  // Implements the method using the given HTTP request for communication.
  // Visible for testing.
  string Oauth2AccessToken(HTTPRequest* req);

 private:
  string project_id_;
  string zone_name_;
  string service_;
  string service_version_;
  DISALLOW_COPY_AND_ASSIGN(CloudEnv);
};

// Returns the default instance of a cloud env object. The returned
// implementation is thread-unsafe, the caller must synchronize the access.
CloudEnv* DefaultCloudEnv();

}  // namespace profiler
}  // namespace cloud

#endif  // CLOUD_PROFILER_AGENT_JAVA_CLOUD_ENV_H_
