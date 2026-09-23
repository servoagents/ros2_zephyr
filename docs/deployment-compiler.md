# Fixed deployment compiler

The deployment compiler turns a strictly validated JSON description of a
fixed ROS boundary into ordinary `rclc`, `rcl`, and selected-RMW setup. It does
not interpret a deployment at runtime and does not describe application logic.

The input owns the node identity, endpoints, message types, topics, QoS,
startup lifetime, graph requirements, and permitted RMW backends. Application
code owns callbacks and all non-ROS behavior. The loopback and static-control
samples are complete examples.

## Build

The sample CMake projects run the compiler before Zephyr evaluates Kconfig and
write all outputs below the build directory:

```sh
samples/loopback/build_native.sh

ROS2_ZEPHYR_DEPLOYMENT_BACKEND=rmw_zenoh_pico \
  ROS2_ZEPHYR_NATIVE_BUILD_DIR="$PWD/build/lyrical/loopback-zenoh" \
  samples/loopback/build_native.sh
```

The generated directory contains:

```text
generated_ros_init.c
generated_ros_init.h
generated_ros2_zephyr.conf
deployment-plan.json
deployment-report.md
```

`generated_ros_init.c` creates every endpoint once, gives the executor its
exact subscription capacity, and prepares its wait set before returning to the
application. Endpoint definitions must not be repeated in handwritten source.

## Validation boundary

The schema is [`schema/deployment.schema.json`](../schema/deployment.schema.json).
The compiler also performs the cross-field checks that JSON Schema does not
express, including unique endpoint identifiers and callback placement.

Backend claims are machine-readable files under `capabilities/`. A deployment
is rejected before C compilation when its requested semantics exceed the
selected backend. For example, Zenoh-Pico does not claim Transient Local:

```text
E_QOS_UNSUPPORTED: endpoint ros2_zephyr_loopback requests
durability=transient_local; backend rmw_zenoh_pico does not support it
```

The compiler never weakens QoS or graph requirements. Both current sample
deployments request the common Best Effort/Volatile, fixed-startup subset and
therefore compile for Cyclone DDS and Zenoh-Pico from the same JSON files.
Running a Zenoh-Pico image also requires its configured Zenoh router; endpoint
generation and the router lifecycle are deliberately separate concerns.

Run the host checks with:

```sh
python3 -m unittest discover -s tests -p 'test_compile_deployment.py'
```
