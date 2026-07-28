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

#
# Base image
#
FROM ubuntu:focal

#
# Dependencies
#
# Install JDK 11 as sampling heap profiler depends on the new JVMTI APIs
# Install Kitware apt repository. This repository contains newer version of
# CMake. CMake > 3.13 is required to use "module" mode for gRPC dependencies.
# See https://github.com/grpc/grpc/blob/master/BUILDING.md#install-after-build.
# Kitware installs CMake v3.20.5

RUN apt-get update && apt-get install -y software-properties-common apt-transport-https ca-certificates gnupg wget && \
    test -f /usr/share/doc/kitware-archive-keyring/copyright || \
    wget -O - https://apt.kitware.com/keys/kitware-archive-latest.asc 2>/dev/null \
    | gpg --dearmor - \
    | tee /usr/share/keyrings/kitware-archive-keyring.gpg >/dev/null && \
    echo 'deb [signed-by=/usr/share/keyrings/kitware-archive-keyring.gpg] https://apt.kitware.com/ubuntu/ focal main' \
    | tee /etc/apt/sources.list.d/kitware.list >/dev/null && \
    apt-get update && \
    apt-get install kitware-archive-keyring

# Installing openjdk-11-jdk-headless can be very slow due to repo download
# speed.

# Everything we can get through apt-get
RUN apt-get update && apt-get -y -q install \
  apt-utils \
  autoconf \
  automake \
  cmake \
  curl \
  g++ \
  git \
  libtool \
  make \
  openjdk-11-jdk-headless \
  pkg-config \
  unzip \
  zlib1g-dev

# openssl
# This openssl (compiled with -fPIC) is used to statically link into the agent
# shared library.
ENV PKG_CONFIG_PATH=/usr/local/ssl/lib/pkgconfig
RUN mkdir /tmp/openssl && cd /tmp/openssl && \
    curl -sL https://github.com/openssl/openssl/archive/OpenSSL_1_1_1t.tar.gz | \
        tar xzv --strip=1 && \
    ./config no-shared -fPIC --openssldir=/usr/local/ssl --prefix=/usr/local/ssl && \
    make && make install_sw && \
    cd ~ && rm -rf /tmp/openssl

# curl
RUN git clone --depth=1 -b curl-8_10_1 https://github.com/curl/curl.git /tmp/curl && \
    cd /tmp/curl && \
    ./buildconf && \
    ./configure --disable-ldap --disable-shared --without-libssh2 \
                --without-librtmp --without-libidn --enable-static \
                --without-libidn2 --without-libpsl \
                --with-pic --with-ssl=/usr/local/ssl/ && \
    make -j && make install && \
    cd ~ && rm -rf /tmp/curl

# gflags
RUN git clone --depth=1 -b v2.1.2 https://github.com/gflags/gflags.git /tmp/gflags && \
    cd /tmp/gflags && \
    mkdir build && cd build && \
    cmake -DCMAKE_POLICY_VERSION_MINIMUM=3.5 -DCMAKE_CXX_FLAGS=-fpic -DGFLAGS_NAMESPACE=google .. && \
    make -j && make install && \
    cd ~ && rm -rf /tmp/gflags

# google-glog
RUN mkdir /tmp/glog && cd /tmp/glog && \
    curl -sL https://github.com/google/glog/archive/v0.4.0.tar.gz | \
        tar xzv --strip=1 && ./autogen.sh && ./configure --with-pic && \
    make -j && make install && \
    cd ~ && rm -rf /tmp/glog

# gRPC & protobuf - build using CMake
# Use the protobuf version from gRPC for everything to avoid conflicting
# versions to be linked in. Disable OpenSSL embedding: when it's on, the build
# process of gRPC puts the OpenSSL static object files into the gRPC archive
# which causes link errors later when the agent is linked with the static
# OpenSSL library itself.
# Limit the number of threads used by make, as unlimited threads causes
# memory exhausted error on the Kokoro VM.
# -DCMAKE_CXX_STANDARD=17 is necessary since it is the minimum version supported
# by gRPC dependencies.
#
# See https://github.com/grpc/grpc/blob/v1.66.0/test/distrib/cpp/run_distrib_test_cmake_pkgconfig.sh
RUN git clone --depth=1 --recursive -b v1.66.0 https://github.com/grpc/grpc.git /tmp/grpc && \
    cd /tmp/grpc/ && \
    # Modern Protobuf (v22+) depends on Abseil. To prevent symbol conflicts and undefined references
    # when gRPC links against Protobuf, Abseil must be built and installed as a shared system package
    # first from gRPC's submodule. Then both Protobuf and gRPC consume it via package (-D..._ABSL_PROVIDER=package).
    # Protobuf CMakeLists.txt moved to repo root in newer releases, so we build in third_party/protobuf/build.
    # Step 1: Install Abseil as a shared package first
    mkdir -p third_party/abseil-cpp/build && \
    (cd third_party/abseil-cpp/build && \
    cmake -DCMAKE_POLICY_VERSION_MINIMUM=3.5 -DABSL_BUILD_TESTING=OFF -DABSL_ENABLE_INSTALL=ON -DCMAKE_CXX_STANDARD=17 -DCMAKE_POSITION_INDEPENDENT_CODE=TRUE -DCMAKE_BUILD_TYPE=Release .. && \
    make -j8 install) && \
    # Step 2: Install Protobuf (using shared Abseil package)
    mkdir -p third_party/protobuf/build && \
    (cd third_party/protobuf/build && \
    cmake -DCMAKE_POLICY_VERSION_MINIMUM=3.5 -Dprotobuf_BUILD_TESTS=OFF -Dprotobuf_ABSL_PROVIDER=package -DCMAKE_CXX_STANDARD=17 -DCMAKE_POSITION_INDEPENDENT_CODE=TRUE -DCMAKE_BUILD_TYPE=Release .. && \
    make -j8 install) && \
    # Step 3: Install gRPC (using shared Abseil and Protobuf packages)
    mkdir -p cmake/build && \
    (cd cmake/build && \
    cmake \
        -DCMAKE_POLICY_VERSION_MINIMUM=3.5           \
        -DOPENSSL_ROOT_DIR=/usr/local/ssl            \
        -DOPENSSL_INCLUDE_DIR=/usr/local/ssl/include \
        -DOPENSSL_CRYPTO_LIB=/usr/local/ssl/lib      \
        -DCMAKE_BUILD_TYPE=Release                   \
        -DCMAKE_CXX_STANDARD=17                      \
        -DCMAKE_INSTALL_PREFIX=/usr/local/grpc       \
        -DgRPC_INSTALL=ON                            \
        -DgRPC_BUILD_TESTS=OFF                       \
        -DgRPC_ABSL_PROVIDER=package                 \
        -DgRPC_CARES_PROVIDER=module                 \
        -DgRPC_RE2_PROVIDER=module                   \
        -DgRPC_ZLIB_PROVIDER=module                  \
        -DgRPC_PROTOBUF_PROVIDER=package             \
        -DgRPC_SSL_PROVIDER=package                  \
        ../.. && \
    make -j8 install) && \
    # gRPC's bundled RE2 submodule (-DgRPC_RE2_PROVIDER=module) does not install a pkg-config file by default.
    # We generate re2.pc manually so pkg-config --libs --static protobuf grpc++ succeeds when compiling the agent.
    mkdir -p /usr/local/grpc/lib/pkgconfig && \
    printf "Name: re2\nDescription: RE2\nVersion: 0.0.0\nLibs: -L/usr/local/grpc/lib -lre2\nCflags: -I/usr/local/grpc/include\n" > /usr/local/grpc/lib/pkgconfig/re2.pc && \
    cd ~ && rm -rf /tmp/grpc

# Add /usr/local/grpc/lib64/pkgconfig and /usr/local/lib/pkgconfig as 64-bit builds on Focal may install .pc files there.
ENV PKG_CONFIG_PATH="${PKG_CONFIG_PATH}:/usr/local/ssl/lib/pkgconfig:/usr/local/grpc/lib/pkgconfig:/usr/local/lib/pkgconfig:/usr/local/lib64/pkgconfig"
ENV PATH="${PATH}:/usr/local/grpc/bin"
