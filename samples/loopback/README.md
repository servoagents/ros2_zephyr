# ROS 2 loopback sample

This sample creates an `rclc` publisher and subscription for a generated
nested fixed-size message. It publishes one sample, receives it through
Cyclone DDS over Zephyr's loopback interface, reports allocation and stack
figures, and cleans up every ROS and DDS object.

From the repository root:

```sh
scripts/run.sh native
scripts/run.sh esp32
```

The ESP32 configuration uses loopback networking and contains no Wi-Fi
credentials. `run-esp32` flashes the image and validates the bounded UART
result.
