#![allow(non_snake_case, non_camel_case_types, non_upper_case_globals)]
include!(concat!(env!("OUT_DIR"), "/bindings.rs"));

#[cfg(test)]
mod tests;

use std::{
    cell::RefCell,
    ffi::{c_uchar, CStr},
    fmt::Display,
    mem::{size_of, MaybeUninit},
    os::raw::{c_char, c_void},
    ptr::null_mut,
};

use clap::Parser;

#[derive(clap::Parser, Debug)]
struct Args {
    #[arg(long)]
    service: Option<String>,

    #[arg(long)]
    service_version: Option<String>,

    #[arg(long)]
    project_id: Option<String>,

    #[arg(long)]
    zone_name: Option<String>,

    #[arg(long, default_value_t = 3)]
    gce_metadata_server_retry_count: u32,

    #[arg(long, default_value_t = 1)]
    gce_metadata_server_retry_sleep_sec: u32,

    #[arg(long, default_value_t = false)]
    cpu_use_per_thread_timers: bool,

    #[arg(long, default_value_t = true)]
    force_debug_non_safepoints: bool,

    #[arg(long, default_value_t = 4096)]
    wall_num_threads_cutoff: u32,

    #[arg(long, default_value_t = false)]
    enable_heap_sampling: bool,

    #[arg(long, default_value_t = 512 * 1024)]
    heap_sampling_interval: u32,
}

impl From<*mut c_char> for Args {
    fn from(options: *mut c_char) -> Self {
        let mut args = vec![""];
        if options != null_mut() {
            let options = unsafe { CStr::from_ptr(options) };
            let options = options.to_str().unwrap();
            args.extend(options.split(','));
        }
        Args::parse_from(args)
    }
}

struct JavaVMWrapper<'a> {
    jvm: &'a mut JavaVM,
    functions: &'a JNIInvokeInterface_,
}

impl<'a> JavaVMWrapper<'a> {
    unsafe fn from(jvm: *mut JavaVM) -> Self {
        let jvm = jvm.as_mut().unwrap();
        let functions = jvm.functions.as_ref().unwrap();
        Self { jvm, functions }
    }

    fn get_jvmti(&mut self) -> JVMTIWrapper {
        let func = self.functions.GetEnv.unwrap();
        let mut jvmti = MaybeUninit::uninit();
        let jvmti_version: jint = JVMTI_VERSION.try_into().unwrap();
        let err = unsafe { func(self.jvm, jvmti.as_mut_ptr(), jvmti_version) };

        let jni_ok: jint = JNI_OK.try_into().unwrap();
        assert_eq!(err, jni_ok);

        unsafe { JVMTIWrapper::from(jvmti.assume_init() as *mut jvmtiEnv) }
    }
}

fn check_error(err: jvmtiError) {
    assert_eq!(err, jvmtiError_JVMTI_ERROR_NONE)
}

struct JVMTIString<'a> {
    string: *mut c_char,
    jvmti: &'a RefCell<&'a mut JVMTIWrapper<'a>>,
}

impl<'a> Drop for JVMTIString<'a> {
    fn drop(&mut self) {
        self.jvmti
            .borrow_mut()
            .deallocate(self.string as *mut c_uchar)
    }
}

impl<'a> JVMTIString<'a> {
    fn to_string(&self) -> String {
        unsafe { CStr::from_ptr(self.string) }
            .to_string_lossy()
            .to_string()
    }
}

struct JVMTIWrapper<'a> {
    env: &'a mut jvmtiEnv,
    functions: &'a jvmtiInterface_1_,
}

impl<'a> JVMTIWrapper<'a> {
    unsafe fn from(env: *mut jvmtiEnv) -> Self {
        let env = env.as_mut().unwrap();
        let functions = env.functions.as_ref().unwrap();
        Self { env, functions }
    }

    fn deallocate(&mut self, ptr: *mut c_uchar) {
        let func = self.functions.Deallocate.unwrap();
        unsafe { check_error(func(self.env, ptr)) }
    }

    fn get_potential_capabilities(&mut self) -> jvmtiCapabilities {
        let mut caps = MaybeUninit::uninit();
        let func = self.functions.GetPotentialCapabilities.unwrap();
        unsafe {
            check_error(func(self.env, caps.as_mut_ptr()));
            caps.assume_init()
        }
    }

    fn add_capabilities(&mut self, caps: &jvmtiCapabilities) {
        let func = self.functions.AddCapabilities.unwrap();
        unsafe { check_error(func(self.env, caps)) }
    }

    fn get_thread_name(&mut self, thread: jthread) -> String {
        let func = self.functions.GetThreadInfo.unwrap();
        let mut value = MaybeUninit::uninit();
        unsafe {
            check_error(func(self.env, thread, value.as_mut_ptr()));
            let thread_info = value.assume_init();
            let rc = RefCell::new(self);
            let result = JVMTIString {
                string: thread_info.name,
                jvmti: &rc,
            };
            result.to_string()
        }
    }

    fn set_event_callbacks(&mut self, callbacks: jvmtiEventCallbacks) {
        let func = self.functions.SetEventCallbacks.unwrap();
        const struct_size: i32 = size_of::<jvmtiEventCallbacks>() as i32;
        unsafe { check_error(func(self.env, &callbacks, struct_size)) }
    }

    fn set_event_notification_mode(
        &mut self,
        event_mode: jvmtiEventMode,
        event: jvmtiEvent,
        thread: jthread,
    ) {
        let func = self.functions.SetEventNotificationMode.unwrap();
        unsafe { check_error(func(self.env, event_mode, event, thread)) }
    }
}

fn desired_caps(args: &Args) -> jvmtiCapabilities {
    let mut caps: jvmtiCapabilities = Default::default();
    caps.set_can_generate_all_class_hook_events(1);
    caps.set_can_get_source_file_name(1);
    caps.set_can_get_line_numbers(1);
    caps.set_can_get_bytecodes(1);
    caps.set_can_get_constant_pool(1);
    if args.force_debug_non_safepoints {
        caps.set_can_generate_compiled_method_load_events(1);
    }
    caps
}

fn validate(_caps: &jvmtiCapabilities, _all_caps: &jvmtiCapabilities) -> bool {
    // TODO: implement
    true
}

fn prepare_jvmti(jvmti: &mut JVMTIWrapper, args: &Args) {
    let all_caps = jvmti.get_potential_capabilities();
    let caps = desired_caps(&args);
    assert!(validate(&caps, &all_caps));
    jvmti.add_capabilities(&caps);
}

extern "C" fn OnThreadStart(jvmti: *mut jvmtiEnv, _jni: *mut JNIEnv, thread: jthread) {
    let mut jvmti = unsafe { JVMTIWrapper::from(jvmti) };
    eprintln!("thread {:?} started", jvmti.get_thread_name(thread));
}

extern "C" fn OnThreadEnd(jvmti: *mut jvmtiEnv, _jni: *mut JNIEnv, thread: jthread) {
    let mut jvmti = unsafe { JVMTIWrapper::from(jvmti) };
    eprintln!("thread {:?} ended", jvmti.get_thread_name(thread));
}

#[no_mangle]
pub extern "C" fn Agent_OnLoad(
    vm: *mut JavaVM,
    options: *mut c_char,
    _reserved: *mut c_void,
) -> jint {
    let args = Args::from(options);
    let mut vm = unsafe { JavaVMWrapper::from(vm) };
    let mut jvmti = vm.get_jvmti();
    prepare_jvmti(&mut jvmti, &args);

    jvmti.set_event_callbacks(jvmtiEventCallbacks {
        ThreadStart: Some(OnThreadStart),
        ThreadEnd: Some(OnThreadEnd),
        ..Default::default()
    });

    for event in vec![
        jvmtiEvent_JVMTI_EVENT_THREAD_START,
        jvmtiEvent_JVMTI_EVENT_THREAD_END,
    ] {
        jvmti.set_event_notification_mode(jvmtiEventMode_JVMTI_ENABLE, event, null_mut());
    }

    0
}
