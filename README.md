# Stackdriver Profiler Java Agent

This repository contains source code for the [Stackdriver
Profiler](https://cloud.google.com/profiler/) Java agent.

## Installation

Most users should install the pre-built agent:

```shell
wget -q -O- https://storage.googleapis.com/cloud-profiler/java/latest/profiler_java_agent.tar.gz \
| sudo tar xzv -C /opt/cprof
```

See the [Stackdriver Profiler Java profiling
doc](https://cloud.google.com/profiler/docs/profiling-java) for detailed and
most up-to-date guide on installing and using the agent.

## Build from Source

### Start on OSX

```bash
# Warning - you'll need to edit both glog & gflags formulae to enable static builds. Remove the BUILD_SHARED_LIBS define.
$ brew edit glog && brew install glog
$ brew edit gflags && brew install gflags
$ brew install curl protobuf cmake grpc
$ ./build_osx.sh
```

### Finish via Docker 

In rare cases that you need to build from source, a script is provided to build
the agent using Docker. Make sure Docker is installed before running the
commands below.

```shell
 $ git clone https://github.com/GoogleCloudPlatform/cloud-profiler-java.git
 $ cd cloud-profiler-java
 $ ./build.sh
```
