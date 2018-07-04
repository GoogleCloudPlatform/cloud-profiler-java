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

#include "src/string.h"

#include <sstream>

namespace cloud {
namespace profiler {

std::vector<string> Split(const string& s, char sp) {
  std::stringstream ss(s);
  string cur;
  std::vector<string> ret;
  while (std::getline(ss, cur, sp)) {
    ret.push_back(std::move(cur));
  }
  return ret;
}

bool ParseKeyValueList(const string& s, std::map<string, string>* out) {
  if (out == nullptr) {
    return false;
  }
  for (const string& kv : Split(s, ',')) {
    size_t pos = kv.find_first_of('=');
    if (pos == 0 || pos == string::npos) {
      return false;
    }
    string k = kv.substr(0, pos);
    if (k.empty()) {
      return false;
    }
    (*out)[k] = kv.substr(pos + 1);
  }
  return true;
}

}  // namespace profiler
}  // namespace cloud
