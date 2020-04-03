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
FROM ubuntu:xenial

#
# Dependencies
#

# Install JDK 11 as sampling heap profiler depends on the new JVMTI APIs.
RUN apt-get update && apt-get install -y software-properties-common
RUN add-apt-repository -y ppa:openjdk-r/ppa
RUN apt-get update

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
  libcurl4-openssl-dev \
  libssl-dev \
  libtool \
  make \
  openjdk-11-jdk-headless \
  python \
  unzip \
  zlib1g-dev

# curl
RUN git clone --depth=1 -b curl-7_55_1 https://github.com/curl/curl.git /tmp/curl && \
    cd /tmp/curl && \
    ./buildconf && \
    ./configure --disable-ldap --disable-shared --without-libssh2 \
                --without-librtmp --without-libidn --enable-static \
                --with-pic --with-ssl && \
    make -j && make install && \
    cd ~ && rm -rf /tmp/curl

# gflags
RUN git clone --depth=1 -b v2.1.2 https://github.com/gflags/gflags.git /tmp/gflags && \
    cd /tmp/gflags && \
    mkdir build && cd build && \
    cmake -DCMAKE_CXX_FLAGS=-fpic -DGFLAGS_NAMESPACE=google .. && \
    make -j && make install && \
    cd ~ && rm -rf /tmp/gflags

# google-glog
RUN mkdir /tmp/glog && cd /tmp/glog && \
    curl -sL https://github.com/google/glog/archive/v0.3.5.tar.gz | \
        tar xzv --strip=1 && ./configure --with-pic && \
    make -j && make install && \
    cd ~ && rm -rf /tmp/glog

# openssl
# This openssl (compiled with -fPIC) is used to statically link into the agent
# shared library.  We still install libssl-dev above since getting libcurl to
# link with the custom version is tricky, see
# http://askubuntu.com/questions/475670/how-to-build-curl-with-the-latest-openssl.
# Also, intentionally not using -j as OpenSSL < 1.1.0 not quite supporting the
# parallel builds, see
# https://github.com/openssl/openssl/issues/5762#issuecomment-376622684.
RUN mkdir /tmp/openssl && cd /tmp/openssl && \
    curl -sL https://github.com/openssl/openssl/archive/OpenSSL_1_0_2t.tar.gz | \
        tar xzv --strip=1 && \
    ./config no-shared -fPIC --openssldir=/usr/local/ssl && \
    make && make install_sw && \
    cd ~ && rm -rf /tmp/openssl

# gRPC & protobuf
# Use the protobuf version from gRPC for everything to avoid conflicting
# versions to be linked in. Disable OpenSSL embedding: when it's on, the build
# process of gRPC puts the OpenSSL static object files into the gRPC archive
# which causes link errors later when the agent is linked with the static
# OpenSSL library itself.
RUN git clone --depth=1 --recursive -b v1.25.0 https://github.com/grpc/grpc.git /tmp/grpc && \
    cd /tmp/grpc && \
    cd third_party/protobuf && \
    ./autogen.sh && \
    ./configure --with-pic CXXFLAGS="$(pkg-config --cflags protobuf)" LIBS="$(pkg-config --libs protobuf)" LDFLAGS="-Wl,--no-as-needed" && \
    make -j && make install && ldconfig && \
    cd ../.. && \
    CPPFLAGS="-I /usr/local/ssl/include" LDFLAGS="-Wl,--no-as-needed" make -j CONFIG=opt EMBED_OPENSSL=false V=1 HAS_SYSTEM_OPENSSL_NPN=0 && \
    CPPFLAGS="-I /usr/local/ssl/include" LDFLAGS="-Wl,--no-as-needed" make CONFIG=opt EMBED_OPENSSL=false V=1 HAS_SYSTEM_OPENSSL_NPN=0 install && \
    rm -rf /tmp/grpc
