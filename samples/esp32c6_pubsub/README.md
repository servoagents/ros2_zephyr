# ESP32-C6 editable pub/sub application

Start with the [C6 application guide](../../docs/getting-started-c6.md).
This is a small ROS 2 C application, separate from the Ping64 benchmark.
A `std_msgs/msg/UInt32` command on `/c6/command` is returned as state on
`/c6/state`. Edit `subscription_callback()` in `src/main.c` to add application
behavior. This sample does not implement a real-time controller or GPIO policy.

The deployment is fixed: domain 73, one publisher, one subscription, best-effort,
volatile, keep-last depth 1, one remote participant and two remote endpoints.
The supplied C++ peer uses stock `rclcpp`, `std_msgs` and `rmw_cyclonedds_cpp`.
Remote graph discovery is disabled; use the explicit topics and types rather
than relying on `ros2 topic list`.

Copy the entire sample directory to make a new application. Keep the supplied
resource contract until a measured change justifies changing it. Edit
`deployment.json` as well as the C callback when changing messages or endpoints.
Scalar, fixed-array and nested fixed-size messages are the supported message
profile. A new message package also needs generation and build integration;
changing a type string alone does not create a package.

The build script produces linked-image and TLS-aware resource reports.
Static admission is not a runtime stack guarantee. The original A4 Ping64
qualification does not qualify this application; flash and test its exact
image before relying on it. Do not publish credential-bearing firmware.
