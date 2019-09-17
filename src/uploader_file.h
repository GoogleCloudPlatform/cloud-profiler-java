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

#ifndef CLOUD_PROFILER_AGENT_JAVA_UPLOADER_FILE_H_
#define CLOUD_PROFILER_AGENT_JAVA_UPLOADER_FILE_H_

#include <cstdio>

#include "src/uploader.h"

namespace cloud {
namespace profiler {

class FileUploader : public cloud::profiler::ProfileUploader {
 public:
  explicit FileUploader(const std::string &prefix) : prefix_(prefix) {}

  bool Upload(const std::string &profile_type,
              const std::string &profile) override {
    std::string filename = ProfilePath(prefix_, profile_type);

    FILE *f = fopen(filename.c_str(), "w");
    if (f == nullptr) {
      LOG(INFO) << "Failed to create file " << filename;
      return false;
    }

    LOG(INFO) << "Saving profile to " << filename;

    int count = profile.size();
    int wrote = fwrite(profile.c_str(), 1, count, f);
    fclose(f);

    if (wrote != count) {
      LOG(INFO) << "Failure! Wrote " << wrote << " bytes, wanted " << count;
      return false;
    }
    return true;
  }

 private:
  std::string prefix_;
  DISALLOW_COPY_AND_ASSIGN(FileUploader);
};

}  // namespace profiler
}  // namespace cloud

#endif  // CLOUD_PROFILER_AGENT_JAVA_UPLOADER_FILE_H_
