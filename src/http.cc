// Copyright 2018 Google LLC
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//      http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "src/http.h"

#include "curl/curl.h"

namespace cloud {
namespace profiler {

typedef long curl_long_t;  // NOLINT 'long'

HTTPRequest::HTTPRequest() : headers_(nullptr) {
  curl_ = curl_easy_init();
  if (!curl_) {
    LOG(ERROR) << "Failed to initialize curl";
  }
}

HTTPRequest::~HTTPRequest() {
  if (headers_) {
    curl_slist_free_all(headers_);
    headers_ = nullptr;
  }
  if (curl_) {
    curl_easy_cleanup(curl_);
    curl_ = nullptr;
  }
}

void HTTPRequest::AddAuthBearerHeader(const std::string& token) {
  AddHeader("Authorization", "Bearer " + token);
}

void HTTPRequest::AddHeader(const std::string& name, const std::string& value) {
  std::string h = name + ":" + value;
  headers_ = curl_slist_append(headers_, h.c_str());  // copies the string
}

void HTTPRequest::AddContentTypeHeader(const std::string& content_type) {
  AddHeader("Content-Type", content_type);
}

void HTTPRequest::SetTimeout(int timeout_sec) {
  curl_easy_setopt(curl_, CURLOPT_TIMEOUT, (curl_long_t)timeout_sec);
}

bool HTTPRequest::DoGet(const std::string& url, std::string* data) {
  data->clear();
  curl_easy_setopt(curl_, CURLOPT_HTTP_VERSION, CURL_HTTP_VERSION_1_1);
  curl_easy_setopt(curl_, CURLOPT_WRITEDATA, data);
  curl_easy_setopt(curl_, CURLOPT_WRITEFUNCTION, ResponseCallback);
  return DoRequest(url);
}

bool HTTPRequest::DoPut(const std::string& url, const std::string& data) {
  curl_easy_setopt(curl_, CURLOPT_CUSTOMREQUEST, "PUT");
  curl_easy_setopt(curl_, CURLOPT_POSTFIELDS, data.c_str());  // does not copy
  curl_easy_setopt(curl_, CURLOPT_POSTFIELDSIZE, (curl_long_t)data.size());
  return DoRequest(url);
}

int HTTPRequest::GetResponseCode() {
  curl_long_t response_code = 0;
  curl_easy_getinfo(curl_, CURLINFO_RESPONSE_CODE, &response_code);
  return response_code;
}

bool HTTPRequest::DoRequest(const std::string& url) {
  curl_easy_setopt(curl_, CURLOPT_URL, url.c_str());
  curl_easy_setopt(curl_, CURLOPT_HTTPHEADER, headers_);
  CURLcode res = curl_easy_perform(curl_);
  if (res != CURLE_OK) {
    LOG(ERROR) << "Failed to make HTTP request: " << curl_easy_strerror(res);
    return false;
  }
  return true;
}

size_t HTTPRequest::ResponseCallback(void* contents, size_t size, size_t nmemb,
                                     std::string* resp) {
  size_t real_size = size * nmemb;
  resp->append(static_cast<char *>(contents), real_size);
  return real_size;
}

}  // namespace profiler
}  // namespace cloud
