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

CC = g++

# -fpermissive used to bypass <:: errors from gcc 4.7
CFLAGS = \
	-m64 \
	-std=c++11 \
	-fpermissive \
	-fPIC \
	-Wall \
	-Wno-unused-parameter \
	-Wno-deprecated \
	-Wno-ignored-qualifiers \
	-Wno-sign-compare \
	-Wno-array-bounds \
	-g0 \
	-DSTANDALONE_BUILD \
	-D_GNU_SOURCE \
	-DENABLE_HEAP_SAMPLING

ifneq ($(AGENT_VERSION),)
  CFLAGS += -DCLOUD_PROFILER_AGENT_VERSION=\"$(AGENT_VERSION)\"
endif

SRC_ROOT_PATH=.

JAVA_PATH ?= /usr/lib/jvm/java-11-openjdk-amd64
PROTOC ?= /usr/local/bin/protoc
PROTOC_GEN_GRPC ?= /usr/local/bin/grpc_cpp_plugin

PROFILE_PROTO_PATH ?= third_party/perftools/profiles/proto
JAVA_AGENT_PATH ?= src
JAVAPROFILER_LIB_PATH = third_party/javaprofiler
GENFILES_PATH ?= .genfiles
OUT_PATH ?= .out
INCLUDE_PATH ?= /usr/local/include
PROTOBUF_INCLUDE_PATH ?= /usr/local/include

INCLUDES = \
	-I$(JAVA_PATH)/include \
	-I$(JAVA_PATH)/include/linux \
	-I$(SRC_ROOT_PATH) \
	-I$(SRC_ROOT_PATH)/third_party \
	-I$(GENFILES_PATH) \
	-I$(GENFILES_PATH)/third_party \
	-I$(INCLUDE_PATH) \
	-I$(PROTOBUF_INCLUDE_PATH) \

TARGET_AGENT = $(OUT_PATH)/profiler_java_agent.so
TARGET_NOTICES = $(OUT_PATH)/NOTICES

PROFILE_PROTO_SOURCES = \
	$(GENFILES_PATH)/$(PROFILE_PROTO_PATH)/profile.pb.cc \
	$(PROFILE_PROTO_PATH)/builder.cc \

PROFILER_API_SOURCES = \
	$(GENFILES_PATH)/google/api/annotations.pb.cc \
	$(GENFILES_PATH)/google/api/http.pb.cc \
	$(GENFILES_PATH)/google/devtools/cloudprofiler/v2/profiler.grpc.pb.cc \
	$(GENFILES_PATH)/google/devtools/cloudprofiler/v2/profiler.pb.cc \
	$(GENFILES_PATH)/google/protobuf/duration.pb.cc \
	$(GENFILES_PATH)/google/rpc/error_details.pb.cc \

JAVAPROFILER_LIB_SOURCES = \
	$(JAVAPROFILER_LIB_PATH)/accessors.cc \
	$(JAVAPROFILER_LIB_PATH)/async_ref_counted_string.cc \
	$(JAVAPROFILER_LIB_PATH)/clock.cc \
	$(JAVAPROFILER_LIB_PATH)/display.cc \
	$(JAVAPROFILER_LIB_PATH)/heap_sampler.cc \
	$(JAVAPROFILER_LIB_PATH)/method_info.cc \
	$(JAVAPROFILER_LIB_PATH)/native.cc \
	$(JAVAPROFILER_LIB_PATH)/profile_proto_builder.cc \
	$(JAVAPROFILER_LIB_PATH)/stacktrace_fixer.cc \
	$(JAVAPROFILER_LIB_PATH)/stacktraces.cc \
	$(JAVAPROFILER_LIB_PATH)/tags.cc \

# Add any header not already as a .cc in JAVAPROFILER_LIB_SOURCES.
JAVAPROFILER_LIB_HEADERS = \
	$(JAVAPROFILER_LIB_PATH)/accessors.h \
	$(JAVAPROFILER_LIB_PATH)/async_ref_counted_string.h \
	$(JAVAPROFILER_LIB_PATH)/heap_sampler.h \
	$(JAVAPROFILER_LIB_PATH)/jvmti_error.h \
	$(JAVAPROFILER_LIB_PATH)/method_info.h \
	$(JAVAPROFILER_LIB_PATH)/profile_proto_builder.h \
	$(JAVAPROFILER_LIB_PATH)/stacktrace_decls.h \
	$(JAVAPROFILER_LIB_PATH)/stacktraces.h \
	$(JAVAPROFILER_LIB_PATH)/tags.h \

SOURCES = \
	$(JAVA_AGENT_PATH)/cloud_env.cc \
	$(JAVA_AGENT_PATH)/entry.cc \
	$(JAVA_AGENT_PATH)/http.cc \
	$(JAVA_AGENT_PATH)/jni.cc \
	$(JAVA_AGENT_PATH)/pem_roots.cc \
	$(JAVA_AGENT_PATH)/profiler.cc \
	$(JAVA_AGENT_PATH)/proto.cc \
	$(JAVA_AGENT_PATH)/string.cc \
	$(JAVA_AGENT_PATH)/threads.cc \
	$(JAVA_AGENT_PATH)/throttler_api.cc \
	$(JAVA_AGENT_PATH)/throttler_timed.cc \
	$(JAVA_AGENT_PATH)/uploader.cc \
	$(JAVA_AGENT_PATH)/uploader_gcs.cc \
	$(JAVA_AGENT_PATH)/worker.cc \
	$(PROFILE_PROTO_SOURCES) \
	$(PROFILER_API_SOURCES) \
	$(JAVAPROFILER_LIB_SOURCES) \

PROFILE_PROTO_HEADERS = \
	$(GENFILES_PATH)/$(PROFILE_PROTO_PATH)/profile.pb.h \

PROFILER_API_HEADERS = $(PROFILER_API_SOURCES:.pb.cc=.pb.h)
JAVAPROFILER_LIB_HEADERS += $(JAVAPROFILER_LIB_SOURCES:.cc=.h)

HEADERS = \
	$(JAVA_AGENT_PATH)/clock.h \
	$(JAVA_AGENT_PATH)/cloud_env.h \
	$(JAVA_AGENT_PATH)/globals.h \
	$(JAVA_AGENT_PATH)/http.h \
	$(JAVA_AGENT_PATH)/pem_roots.h \
	$(JAVA_AGENT_PATH)/profiler.h \
	$(JAVA_AGENT_PATH)/proto.h \
	$(JAVA_AGENT_PATH)/string.h \
	$(JAVA_AGENT_PATH)/threads.h \
	$(JAVA_AGENT_PATH)/throttler.h \
	$(JAVA_AGENT_PATH)/throttler_api.h \
	$(JAVA_AGENT_PATH)/throttler_timed.h \
	$(JAVA_AGENT_PATH)/uploader.h \
	$(JAVA_AGENT_PATH)/uploader_file.h \
	$(JAVA_AGENT_PATH)/uploader_gcs.h \
	$(JAVA_AGENT_PATH)/worker.h \
	$(PROFILE_PROTO_HEADERS) \
	$(PROFILER_API_HEADERS) \
	$(JAVAPROFILER_LIB_HEADERS) \

VERSION_SCRIPT = $(JAVA_AGENT_PATH)/cloud_profiler_java_agent.lds
OPT_FLAGS = -O3
LDFLAGS = -static-libstdc++ -shared
LDS_FLAGS = -Wl,-z,defs -Wl,--version-script=$(VERSION_SCRIPT)

LIB_ROOT_PATH ?= /usr/local

# Linking OpenSSL statically as different distributions have different version
# of the libraries, including different soname. And we'd prefer to have a single
# agent binary which runs everywhere.
LIBS1= \
	-ldl \
	-lrt \
	-pthread \
  $(LIB_ROOT_PATH)/lib/libcurl.a \
  $(LIB_ROOT_PATH)/lib/libglog.a \
	$(LIB_ROOT_PATH)/lib/libgflags.a \

LIBS2= \
	$(LIB_ROOT_PATH)/lib/libprotobuf.a \
	$(LIB_ROOT_PATH)/ssl/lib/libssl.a \
	$(LIB_ROOT_PATH)/ssl/lib/libcrypto.a \
	-lz \

GRPC_LIBS= \
	$(LIB_ROOT_PATH)/lib/libgrpc++.a \
  $(LIB_ROOT_PATH)/lib/libgrpc.a \
  $(LIB_ROOT_PATH)/lib/libgpr.a \

all: \
	$(TARGET_AGENT) \
	$(TARGET_NOTICES) \

clean:
	rm -f $(TARGET_AGENT)
	rm -rf $(GENFILES_PATH)

$(TARGET_AGENT): $(SOURCES) $(HEADERS)
	mkdir -p $(dir $@)
	$(CC) $(INCLUDES) $(CFLAGS) $(OPT_FLAGS) $(LDFLAGS) $(SOURCES) $(LIBS1) $(GRPC_LIBS) $(LIBS2) -o $@ $(LDS_FLAGS)

$(TARGET_NOTICES): $(JAVA_AGENT_PATH)/NOTICES
	mkdir -p $(dir $@)
	cp -f $< $@

$(GENFILES_PATH)/%profiler.pb.h $(GENFILES_PATH)/%profiler.pb.cc : third_party/googleapis/%profiler.proto
	mkdir -p $(dir $@)
	$(PROTOC) -Ithird_party/googleapis -I$(PROTOBUF_INCLUDE_PATH) --cpp_out=$(GENFILES_PATH) $<

$(GENFILES_PATH)/%profiler.grpc.pb.h $(GENFILES_PATH)/%profiler.grpc.pb.cc : third_party/googleapis/%profiler.proto
	mkdir -p $(dir $@)
	$(PROTOC) -Ithird_party/googleapis -I$(PROTOBUF_INCLUDE_PATH) \
			--plugin=protoc-gen-grpc=$(PROTOC_GEN_GRPC) --grpc_out=services_namespace=grpc:$(GENFILES_PATH) $<

$(GENFILES_PATH)/%annotations.pb.h $(GENFILES_PATH)/%annotations.pb.cc : third_party/googleapis/%annotations.proto
	mkdir -p $(dir $@)
	$(PROTOC) -Ithird_party/googleapis -I$(PROTOBUF_INCLUDE_PATH) --cpp_out=$(GENFILES_PATH) $<

$(GENFILES_PATH)/%http.pb.h $(GENFILES_PATH)/%http.pb.cc : third_party/googleapis/%http.proto
	mkdir -p $(dir $@)
	$(PROTOC) -Ithird_party/googleapis --cpp_out=$(GENFILES_PATH) $<

$(GENFILES_PATH)/%error_details.pb.h $(GENFILES_PATH)/%error_details.pb.cc : third_party/googleapis/%error_details.proto
	mkdir -p $(dir $@)
	$(PROTOC) -Ithird_party/googleapis -I$(PROTOBUF_INCLUDE_PATH) --cpp_out=$(GENFILES_PATH) $<

$(GENFILES_PATH)/%duration.pb.h $(GENFILES_PATH)/%duration.pb.cc : $(PROTOBUF_INCLUDE_PATH)/%duration.proto
	mkdir -p $(dir $@)
	$(PROTOC) -I$(PROTOBUF_INCLUDE_PATH) --cpp_out=$(GENFILES_PATH) $<

$(GENFILES_PATH)/%.pb.h $(GENFILES_PATH)/%.pb.cc : %.proto
	mkdir -p $(dir $@)
	$(PROTOC) --cpp_out=$(GENFILES_PATH) $<

$(VERSION_SCRIPT):
	echo "VERS_1.1 { global: ;" $(AGENT_EXPORTS)  "; local: *; };" > $@

# vim: set ts=2 noet sw=2 sts=2 :
