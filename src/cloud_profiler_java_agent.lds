VERS_1.0 {
  global:
    Agent_OnLoad;
    Agent_OnUnload;
    Java_org_apache_beam_runners_dataflow_worker_profiler_Profiler_disable;
    Java_org_apache_beam_runners_dataflow_worker_profiler_Profiler_enable;
    Java_org_apache_beam_runners_dataflow_worker_profiler_Profiler_getAttribute;
    Java_org_apache_beam_runners_dataflow_worker_profiler_Profiler_registerAttribute;
    Java_org_apache_beam_runners_dataflow_worker_profiler_Profiler_setAttribute;
  local:
    *;
};
