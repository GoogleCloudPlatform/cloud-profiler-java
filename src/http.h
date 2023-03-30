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

#ifndef CLOUD_PROFILER_AGENT_JAVA_HTTP_H_
#define CLOUD_PROFILER_AGENT_JAVA_HTTP_H_

#include <string>

#include "src/globals.h"

typedef void CURL;
struct curl_slist;

namespace cloud {
namespace profiler {

const int kHTTPStatusOK = 200;

// A simple libcurl-based HTTP transport.
class HTTPRequest {
 public:
  HTTPRequest();
  virtual ~HTTPRequest();

  virtual void AddAuthBearerHeader(const std::string& token);
  virtual void AddContentTypeHeader(const std::string& content_type);
  virtual void AddHeader(const std::string& name, const std::string& value);
  virtual void SetTimeout(int timeout_sec);

  virtual bool DoGet(const std::string& url, std::string* data);
  virtual bool DoPut(const std::string& url, const std::string& data);

  virtual int GetResponseCode();

 private:
  bool DoRequest(const std::string& url);

  static size_t ResponseCallback(void* contents, size_t size, size_t nmemb,
                                 std::string* resp);

  CURL* curl_;
  struct curl_slist* headers_;
  DISALLOW_COPY_AND_ASSIGN(HTTPRequest);
};

}  // namespace profiler
}  // namespace cloud

#endif  // CLOUD_PROFILER_AGENT_JAVA_HTTP_H_
