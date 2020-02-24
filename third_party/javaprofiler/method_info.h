/*
 * Copyright 2019 Google LLC
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

#ifndef GOOGLE_JAVAPROFILER_METHOD_INFO_H_
#define GOOGLE_JAVAPROFILER_METHOD_INFO_H_

#include <unordered_map>

#include "perftools/profiles/proto/builder.h"
#include "third_party/javaprofiler/globals.h"
#include "third_party/javaprofiler/stacktrace_decls.h"

namespace google {
namespace javaprofiler {

/**
 * Method information containing method/class/file names and the
 * BCI-to-line-number lookups.
 */
class MethodInfo {
 public:
  // Constructor providing all the information regarding a method.
  MethodInfo(const std::string &method_name, const std::string &class_name,
             const std::string &file_name)
      : method_name_(method_name),
        class_name_(class_name),
        file_name_(file_name) {}

  // An invalid location Id.
  static const int64 kInvalidLocationId = 0;

  // Return the location representing info, returns kInvalidLocationId
  // if not found.
  int64 Location(int line_number);

  // Put a location ID assocated to a ByteCode Index (BCI).
  void AddLocation(int bci, int64 location_id) {
    locations_[bci] = location_id;
  }

  const std::string &MethodName() const { return method_name_; }

  const std::string &ClassName() const { return class_name_; }

  const std::string &FileName() const { return file_name_; }

 private:
  std::string method_name_;
  std::string class_name_;
  std::string file_name_;

  // Cache of jlocation results.
  std::unordered_map<int, int64> locations_;
};

}  // namespace javaprofiler
}  // namespace google

#endif  // THIRD_PARTY_JAVAPROFILER_METHOD_CACHE_H_
