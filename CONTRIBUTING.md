# Contributing

Please open an issue before changing the supported ROS or middleware profile.
Small fixes and documentation improvements can go directly to a pull request.

Keep platform claims tied to a specific test. A successful native simulation
does not establish hardware behavior, and an ESP32 compile does not establish
network interoperability.

Before submitting a change, run:

```sh
scripts/check.sh
scripts/run.sh native
scripts/run.sh esp32
```

C and C++ follow `.clang-format`, Python follows PEP 8, shell follows `shfmt`
and ShellCheck, and CMake files use two-space indentation.

By contributing, you agree that your work is licensed under Apache-2.0.
