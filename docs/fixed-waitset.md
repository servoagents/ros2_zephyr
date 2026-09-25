# Fixed Cyclone wait-set attachments

Milestone E optimizes the largest recurring allocation source measured after
the fixed-deployment and resource-contract work. It does not add a new runtime
or change the ROS boundary: the application still uses rclc, rcl,
`rmw_cyclonedds_c`, standard message definitions, and stock DDS peers.

## Measured call path

A native `static_control` run was stopped after executor preparation and its
middleware allocator was sampled under GDB. The dominant recurring 32-byte
allocation followed this path:

```text
rclc_executor_spin_some
  -> rcl_wait
  -> rmw_wait
  -> attach_entity
  -> dds_waitset_attach
  -> dds_entity_observer_register
  -> ddsrt_malloc
```

`rmw_wait()` detached and reattached the same subscription condition on every
executor iteration. Cyclone allocates observer bookkeeping for each attach,
so a fixed one-subscription executor paid this cost repeatedly even though its
entity set never changed.

Other sampled middleware allocations belonged to application publication,
participant liveliness metadata, discovery packets, serialization, and
garbage collection. Those operations have distinct lifetimes and are not
removed by this optimization.

## Ownership and concurrency

The RMW wait set owns its `attached` array and `attached_count`; the DDS wait
set owns the registered observers. The static-control main thread is the only
thread that constructs the RMW input arrays and calls `rmw_wait()`. Cyclone may
signal observers concurrently, but the observer remains registered with the
same DDS wait set and entity. Closing the DDS wait set unregisters its
observers through the existing Cyclone cleanup path.

The patch first validates every requested subscription and guard condition,
checks capacity, and compares the ordered DDS entity handles with the saved
attachment set. An exact match goes directly to `dds_waitset_wait()`. A count,
order, or entity change uses the original detach-then-attach behavior. This
keeps dynamic callers valid and makes the optimization useful without adding
deployment-specific APIs to the RMW.

The repository applies
`patches/rmw-cyclonedds-c-fixed-waitset.patch` to the exact pinned
`servoagents/rmw_cyclonedds_c` revision
`039e85860dc6032d51bd25dc552e5bdbbb0e686a`. The upstream default source tree
remains unmodified.

## Regression and measurement

`tools/check_static_control_metrics.py` consumes consecutive
`STATIC_CONTROL_METRICS` records. It requires zero ROS steady-state allocation
attempts, a nondecreasing middleware counter, at least two samples, and at
most 32 new middleware allocation attempts between adjacent one-second
samples. The first cumulative sample includes the first reporting interval and
is not treated as a delta.

The bound describes a quiescent fixed graph. New remote participants and
endpoint churn can legitimately allocate while Cyclone creates and destroys
discovery state; those intervals are measured separately and must not be used
to weaken the fixed-graph regression.

Identical six-second native runs produced:

| Image | Cumulative middleware allocation attempts | Maximum one-second delta |
|---|---|---:|
| before wait-set reuse | 100, 184, 270, 354, 440 | 86 |
| after wait-set reuse | 34, 50, 68, 84, 102, 118 | 18 |

At the fifth common sample, the counter fell from 440 to 102: 338 fewer
attempts, or 76.8 percent. ROS steady-state attempts remained zero, allocator
high-water remained 86,777 bytes, control-stack unused space remained 2,008
bytes, and no release was skipped. The regression rejects the pre-optimization
trace and accepts the optimized trace.

The native linked image changed from 722,607 to 722,773 bytes of flash
(+166 bytes) and remained at 1,224,121 bytes of static RAM. This small code
cost is accepted for the recurring allocation reduction.

## Hardware and interoperability

The optimized ESP32-S3 image passed the post-link resource contract at
1,015,940 bytes of flash and 262,120 bytes of linked DRAM. A quiescent
12-sample UART run reached READY with zero ROS steady-state allocation
attempts, a maximum middleware delta of 25, no skipped release, zero measured
maximum lateness, and 1,664 unused control-stack bytes.

The same image preserved both directions of standard ROS interoperability with
an unmodified Lyrical `rmw_cyclonedds_cpp` peer. The desktop received a typed
`ControlState`. The board accepted typed `ControlCommand` messages with
sequence 17, reported no rejection, and alternated ACTIVE and STALE according
to the existing 250 ms command-expiry policy. This confirms that persistent
wait-set attachment does not change message, QoS, or command-expiry semantics.

This is the first meaningful Milestone E optimization. Per the phase plan,
deeper work stops here until the remaining discovery/publication allocations
are selected as a separate measured target.
