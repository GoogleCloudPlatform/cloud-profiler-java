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

#include "src/uploader_gcs.h"

#include <sstream>
#include <string>

#include "src/http.h"
#include "third_party/absl/flags/flag.h"

DEFINE_int32(cprof_gcs_upload_timeout_sec, 10,
             "Google Cloud Storage profile upload timeout in seconds");

namespace cloud {
namespace profiler {

const char kGcsHost[] = "https://storage.googleapis.com";

bool GcsUploader::Upload(const std::string &profile_type,
                         const std::string &profile) {
  LOG(INFO) << "Uploading " << profile.size() << " byte " << profile_type
            << " profile to GCS";

  std::string access_token = env_->Oauth2AccessToken();
  if (access_token.empty()) {
    LOG(ERROR) << "Failed to gather an OAuth2 access token for GCS upload";
    return false;
  }

  HTTPRequest uploadReq;
  uploadReq.AddAuthBearerHeader(access_token);
  uploadReq.AddContentTypeHeader("application/octet-stream");
  uploadReq.AddHeader("Content-Length", std::to_string(profile.size()));
  uploadReq.SetTimeout(absl::GetFlag(FLAGS_cprof_gcs_upload_timeout_sec));

  std::string url =
      std::string(kGcsHost) + "/" + ProfilePath(prefix_, profile_type);
  if (!uploadReq.DoPut(url, profile)) {
    LOG(ERROR) << "Error making profile upload HTTP request to GCS";
    return false;
  }

  int resp_code = uploadReq.GetResponseCode();
  if (resp_code != kHTTPStatusOK) {
    LOG(ERROR) << "Profile upload to GCS failed, status code: " << resp_code;
    return false;
  }

  return true;
}

}  // namespace profiler
}  // namespace cloud
