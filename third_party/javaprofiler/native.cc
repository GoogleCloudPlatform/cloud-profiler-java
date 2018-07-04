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

// Force definition of SNCx64 macro used below for all variations of
// inttypes.h.
#ifndef __STDC_FORMAT_MACROS
#define __STDC_FORMAT_MACROS
#endif

#include "third_party/javaprofiler/native.h"

#include <libgen.h>

#include <cinttypes>
#include <cstdio>
#include <cstring>
#include <memory>

namespace google {
namespace javaprofiler {

NativeProcessInfo::NativeProcessInfo(const string &procmaps_filename)
    : procmaps_filename_(procmaps_filename) {
  Refresh();
}

void NativeProcessInfo::Refresh() {
  FILE *f = fopen(procmaps_filename_.c_str(), "r");

  if (f == nullptr) {
    LOG(ERROR) << "Could not open maps file: " << procmaps_filename_;
    return;
  }

  mappings_.clear();

  const size_t kBufferLineSize = 2048;
  std::unique_ptr<char[]> line(new char[kBufferLineSize]);
  while (fgets(&line[0], kBufferLineSize, f)) {
    uint64_t start = 0, limit = 0, offset = 0;
    char permissions[5];
    int filename_index = 0;

    if (sscanf(&line[0], "%" SCNx64 "-%" SCNx64 " %4s %" SCNx64 " %*s %*s %n",
               &start, &limit, &permissions[0], &offset,
               &filename_index) != 4) {
      // Partial match, ignore.
      continue;
    }
    if (strlen(permissions) != 4 || permissions[2] != 'x') {
      // Only examine executable mappings.
      continue;
    }

    if (filename_index == 0 || filename_index > strlen(&line[0])) {
      // No valid filename found.
      continue;
    }

    if (!line[filename_index]) {
      // Skip mappings with an empty name. Likely generated code that
      // cannot be symbolized anyway.
      continue;
    }

    const char *filename = &line[filename_index];
    size_t filename_len = strcspn(filename, " \t\n");
    mappings_.emplace_back(
        Mapping{start, limit, string(filename, filename_len)});
  }
  fclose(f);
}

}  // namespace javaprofiler
}  // namespace google
