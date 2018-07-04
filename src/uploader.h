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

#ifndef CLOUD_PROFILER_AGENT_JAVA_UPLOADER_H_
#define CLOUD_PROFILER_AGENT_JAVA_UPLOADER_H_

#include "src/globals.h"

namespace cloud {
namespace profiler {

class ProfileUploader {
 public:
  virtual ~ProfileUploader() {}
  virtual bool Upload(const string &profile_type, const string &profile) = 0;
};

// Returns the path to use for a profile.  The path will contain the current
// timestamp which makes it fairly (but not necessarily globally) unique.
string ProfilePath(const string& prefix, const string& profile_type);

}  // namespace profiler
}  // namespace cloud

#endif  // CLOUD_PROFILER_AGENT_JAVA_UPLOADER_H_
