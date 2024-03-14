use std::process::Command;

#[test]
fn jvm_test() {
    let output = Command::new("javac")
        .arg("Testo.java")
        .output()
        .expect("Failed to compile Java program for testing");
    assert!(output.status.success());

    let agent_path = test_cdylib::build_current_project();
    let agent_path = agent_path.as_os_str().as_encoded_bytes();
    let output = Command::new("java")
        .arg(format!(
            "-agentpath:{}=foo",
            String::from_utf8_lossy(agent_path)
        ))
        .arg("Testo")
        .output()
        .expect("Failed to run Java program");
    assert!(output.status.success());

    assert_eq!(String::from_utf8_lossy(&output.stderr), "")
}
