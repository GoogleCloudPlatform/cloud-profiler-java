package com.google.cloud.profiler;

public class Profiler {
  public static native boolean isEnabled();
  public static native void enable();
  public static native void disable();
  public static native byte[] collect(String type, long duration_nanos, long sampling_period_nanos);
}
