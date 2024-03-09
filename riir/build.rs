use std::{env, path::PathBuf};

fn main() {
    // println!("cargo:rustc-link-search=/Library/Java/JavaVirtualMachines/microsoft-21.jdk/Contents/Home/lib/");

    let java_home = env::var("JAVA_HOME") //
        .expect("env variable JAVA_HOME not set");

    let bindings = bindgen::builder()
        .header("wrapper.h")
        .clang_arg("-x")
        .clang_arg("c++")
        .clang_arg(format!("-I{0}/include", java_home))
        .clang_arg(format!("-I{0}/include/darwin", java_home))
        .parse_callbacks(Box::new(bindgen::CargoCallbacks::new()))
        .blocklist_function("Agent_OnLoad")
        .derive_default(true)
        .new_type_alias("jvmtiError")
        // .must_use_type("jvmtiError")
        .generate()
        .expect("Unable to generate bindings");

    let out_path = env::var("OUT_DIR") //
        .expect("No OUT_PATH specified");
    let out_path = PathBuf::from(out_path);

    bindings
        .write_to_file(out_path.join("bindings.rs"))
        .expect("Couldn't write bindings!");
}
