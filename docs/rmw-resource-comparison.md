# ESP32-S3 RMW resource comparison

These are physical-board high-water measurements from the same ESP32-S3,
Zephyr 4.4.0 toolchain, Wi-Fi configuration, `std_msgs/msg/UInt32` schedule,
and depth-5 Volatile profiles. Cyclone DDS C was accepted after the backend
refactor on 2026-09-21; Zenoh-Pico was accepted later that day. They are
observations for this sample, not general minimum requirements.

| Role and QoS | RMW | Linked flash | Linked DRAM | ROS allocator peak | Middleware allocator peak | External heap peak | Threads | Reserved stacks | Main stack used | Final live bytes |
| --- | --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| Publisher, Best Effort | Cyclone DDS C | 1,010,756 B | 261,368 B | 23,586 B | 107,780 B | 39,376 B | 12 | 70,144 B | 11,344 B | 0 B |
| Publisher, Best Effort | Zenoh-Pico | 669,076 B | 263,696 B | 522 B | 13,088 B | 0 B | 9 | 43,520 B | 8,928 B | 0 B |
| Publisher, Reliable | Cyclone DDS C | 1,010,644 B | 261,368 B | 23,586 B | 109,746 B | 39,376 B | 12 | 70,144 B | 11,344 B | 0 B |
| Publisher, Reliable | Zenoh-Pico | 668,948 B | 263,696 B | 522 B | 13,088 B | 0 B | 9 | 43,520 B | 8,928 B | 0 B |
| Subscriber, Best Effort | Cyclone DDS C | 1,019,476 B | 261,384 B | 23,797 B | 107,148 B | 39,376 B | 12 | 70,144 B | 11,456 B | 0 B |
| Subscriber, Best Effort | Zenoh-Pico | 746,064 B | 263,704 B | 574 B | 12,860 B | 0 B | 9 | 43,520 B | 9,024 B | 0 B |
| Subscriber, Reliable | Cyclone DDS C | 1,019,320 B | 261,384 B | 23,797 B | 107,228 B | 39,376 B | 12 | 70,144 B | 11,456 B | 0 B |
| Subscriber, Reliable | Zenoh-Pico | 745,876 B | 263,704 B | 574 B | 12,860 B | 0 B | 9 | 43,520 B | 9,024 B | 0 B |

For the Reliable profiles, Zenoh-Pico reduced linked flash by 33.8% for the
publisher and 26.8% for the subscriber. It reserved 38.0% less stack and its
combined tracked ROS and middleware allocation peaks were about 89.8% lower.
Linked DRAM was 0.9% higher. The allocator columns are reported separately
because the two RMWs place different classes of object in the ROS and
middleware domains.

The Cyclone worker set includes `recv`, `tev`, `dq.user`, `dq.builtins`, and
`gc`. Zenoh-Pico uses the common network and application threads; its Zenoh
executor stack comes from a fixed static pool. No accepted run left tracked
ROS or middleware allocations live after cleanup.

Evidence is under `results/lyrical/esp32s3-step1-refactor-accepted-20260921`
and `results/lyrical/esp32s3-hardware-zenoh-pico-step2-final`.
