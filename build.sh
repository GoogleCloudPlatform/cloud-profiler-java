#!/bin/bash
#
# Copyright 2018 Google LLC
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#      http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

# The script builds the agent using local Docker.

set -o errexit
set -o nounset
#
# Command line arguments: [-d]
#   -d: specify the temporary directory for the build.
#   -m: use 'amd64' to build for amd64, 'alpine' for Alpine Linux,
#       'arm64' for ARM64. Omitting -m builds for amd64.
#   -v: the version for this build of the agent.

ARM64_BUILD="0"
ALPINE_BUILD="0"
DOCKERFILE="Dockerfile"
FILE_SUFFIX=""
MACHINE_TYPE=$(uname -m)
while getopts ":d:m:v:" opt; do
  case $opt in
  d)
      BUILD_TEMP_DIR=$OPTARG
      ;;
  m)
      TARGET=$OPTARG
      if [[ "${TARGET}" == "arm64" ]]; then
        if [[ "${MACHINE_TYPE}" != "aarch64" ]] ||  [[ "${MACHINE_TYPE}" != "arm64" ]]; then
          echo "-m arm64 is supported only when running on an ARM64 system."
          exit;
        fi
        ARM64_BUILD="1"
        FILE_SUFFIX="_arm64"
        ALPINE_BUILD="0"
        echo "Building for ARM64 is provided only for testing."
      elif [[ "${TARGET}" == "alpine" ]]; then
        if [[ "${MACHINE_TYPE}" != "x86_64" ]]; then
          echo "-m alpine is supported only when running on an x86_64 system."
          exit;
        fi
        ALPINE_BUILD="1"
        FILE_SUFFIX="_alpine"
        ARM64_BUILD="0"
      elif [[ "${TARGET}" == "amd64" ]]; then
        if [[ "${MACHINE_TYPE}" != "x86_64" ]]; then
          echo "-m amd64 is supported only when running on an x86_64 system."
          exit;
        fi
        ARM64_BUILD="0"
        ALPINE_BUILD="0"
        FILE_SUFFIX=""
      else
        echo "-m must be either amd64, arm64 or alpine."
        exit;
      fi
      ;;
  v)
      VERSION=$OPTARG
      ;;
  :)
      echo "Missing option argument for -$OPTARG" >&2;
      exit 1
      ;;
  *)
      echo "Unknown option: -$OPTARG" >&2;
      exit 1
      ;;
  esac
done

if [[ -z "${VERSION-}" ]]; then
  VERSION="github${FILE_SUFFIX}:$(git rev-parse HEAD || echo "unknown")"
fi

if [[ -z "${BUILD_TEMP_DIR-}" ]]; then
  RUN_ID="$(cat /dev/urandom | tr -dc 'a-zA-Z0-9' | fold -w 32 | head -n 1)"
  BUILD_TEMP_DIR="/tmp/${RUN_ID}"
fi

function PrintMessage() {
  local CURRENT_TIME="$( date +%H:%M:%S )"
  echo "[${CURRENT_TIME}] $1"
  echo "[${CURRENT_TIME}] $1" >> "${LOG_FILE}" 2>&1
}

cd "$(dirname "$0")"
LOG_FILE="${BUILD_TEMP_DIR}/build.log"

echo "Log file: ${LOG_FILE}"
trap "{ echo 'FAILED: see ${LOG_FILE} for details' ; exit 1; }" ERR

mkdir -p "${BUILD_TEMP_DIR}"

if [[ "${ALPINE_BUILD}" = "1" ]]; then
    PrintMessage "Building the builder Alpine Docker container..."
    DOCKERFILE="Dockerfile.alpine"
elif [[ "${ARM64_BUILD}" = "1" ]]; then
    PrintMessage "Building the builder ARM64 Docker container..."
else
    PrintMessage "Building the builder Docker container..."
fi
docker build -f "${DOCKERFILE}" -t cprof-agent-builder . >> "${LOG_FILE}" 2>&1

PrintMessage "Packaging the agent code..."
mkdir -p "${BUILD_TEMP_DIR}"/build
tar cf "${BUILD_TEMP_DIR}"/build/src.tar . >> "${LOG_FILE}" 2>&1

PrintMessage "Building the agent..."
docker run -ti -v "${BUILD_TEMP_DIR}/build":/root/build \
    cprof-agent-builder bash \
    -c \
    "cd ~/build && tar xvf src.tar && make -f Makefile all \
       AGENT_VERSION=${VERSION}" \
    >> "${LOG_FILE}" 2>&1

PrintMessage "Packaging the agent binaries..."
tar zcf "${BUILD_TEMP_DIR}"/profiler_java_agent${FILE_SUFFIX}.tar.gz \
    -C "${BUILD_TEMP_DIR}"/build/.out \
    NOTICES profiler_java_agent.so \
    >> "${LOG_FILE}" 2>&1

PrintMessage "Agent built and stored locally in: ${BUILD_TEMP_DIR}/profiler_java_agent${FILE_SUFFIX}.tar.gz"

trap - EXIT
PrintMessage "SUCCESS"
