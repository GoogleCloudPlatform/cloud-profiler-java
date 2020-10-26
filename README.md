# Google Cloud Profiler Profiler Java Agent

This repository contains source code for the
[Google Cloud Profiler Profiler](https://cloud.google.com/profiler/) Java agent.

## Installation

Most users should install the pre-built agent:

```shell
wget -q -O- https://storage.googleapis.com/cloud-profiler/java/latest/profiler_java_agent.tar.gz \
| sudo tar xzv -C /opt/cprof
```

See the
[Google Cloud Profiler Java profiling doc](https://cloud.google.com/profiler/docs/profiling-java)
for detailed and most up-to-date guide on installing and using the agent.

## Build from Source

In rare cases that you need to build from source, a script is provided to build
the agent using Docker. Make sure Docker is installed before running the
commands below.

```shell
 $ git clone https://github.com/GoogleCloudPlatform/cloud-profiler-java.git
 $ cd cloud-profiler-java
 $ ./build.sh
```

## To build for Alpine

**Only Alpine versions 3.11 and later are currently supported.**

**Per thread timers are not available on Alpine.** Since SIGEV_THREAD_ID is not
supported by `timer_create` on Alpine, per thread timers are not implemented and
the flag `-cprof_cpu_use_per_thread_timers` is ignored on this platform.

```shell
$ git clone https://github.com/GoogleCloudPlatform/cloud-profiler-java.git
$ cd cloud-profiler-java
$ ./build.sh -m alpine
```

## To build for ARM64

**Support for ARM64 is provided for testing purposes only.** The following
commands must be run on an ARM64 machine. Cross compilation is not supported.

```shell
$ git clone https://github.com/GoogleCloudPlatform/cloud-profiler-java.git
$ cd cloud-profiler-java
$ ./build.sh -m arm64
```
