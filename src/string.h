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

#ifndef CLOUD_PROFILER_AGENT_JAVA_STRING_H_
#define CLOUD_PROFILER_AGENT_JAVA_STRING_H_

#include <map>
#include <string>
#include <vector>

#include "src/globals.h"

namespace cloud {
namespace profiler {

// Splits a string by the specified character, e.g. ("a,b", ',') -> ["a", "b"].
std::vector<string> Split(const string& s, char sp);

// Parses a comma-separated key/value string (e.g. "foo=1,bar=2") into a map.
// Of duplicate keys the rightmost value wins. Returns false if the string is
// not in the expected format.
bool ParseKeyValueList(const string& s, std::map<string, string>* out);

}  // namespace profiler
}  // namespace cloud

#endif  // CLOUD_PROFILER_AGENT_JAVA_STRING_H_
