{
  name: "esp-idf_agent",
  description: "Specializes in ESP-IDF development with component-based architecture",
  rules: {
    language: "English only (ASCII, no emojis)",
    project_structure: {
      src: "Implementation",
      include: "Public API",
      interfaces: "include/interfaces/",
      host_test: "Host-based unit tests",
      test_apps: "On-target tests"
    },
    architecture: {
      component_oriented: true,
      srp: true,
      interfaces_first: true
    },
    testing: {
      host_tests: true,
      on_target_tests: true
    }
  }
}
