VERS_1.0 {
  global:
    Agent_OnLoad;
    Agent_OnUnload;
    Java_com_google_cloud_profiler_Profiler_isEnabled;
    Java_com_google_cloud_profiler_Profiler_enable;
    Java_com_google_cloud_profiler_Profiler_disable;
    Java_com_google_cloud_profiler_Profiler_collect;
    Java_com_google_cloud_dataflow_worker_profiler_Profiler_disable;
    Java_com_google_cloud_dataflow_worker_profiler_Profiler_enable;
    Java_com_google_cloud_dataflow_worker_profiler_Profiler_getAttribute;
    Java_com_google_cloud_dataflow_worker_profiler_Profiler_registerAttribute;
    Java_com_google_cloud_dataflow_worker_profiler_Profiler_setAttribute;
  local:
    *;
};
