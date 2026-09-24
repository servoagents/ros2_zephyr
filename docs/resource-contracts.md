# Resource admission and post-link verification

A generated deployment carries an explicit resource contract. The build first
checks declared requirements against the target and effective Zephyr
configuration, then checks the resulting image against the same contract after
linking. Both stages are deterministic host tools; firmware does not parse a
resource description at runtime.

## Deployment contract

Deployment schema version 2 adds a required `resources` object:

```json
{
  "resources": {
    "local_endpoints": 2,
    "remote": {
      "participants": 0,
      "nodes": 0,
      "endpoints": 0
    },
    "workers": {
      "maximum": 8
    },
    "application": {
      "static_reserve_bytes": 16384
    },
    "targets": {
      "esp32s3_devkitc/esp32s3/procpu": {
        "flash_bytes": 1100000,
        "ram_bytes": 300000
      }
    }
  }
}
```

`local_endpoints` bounds the fixed publishers and subscriptions generated for
the node. `remote` bounds the required discovery state independently from the
local endpoint count. `workers.maximum` limits backend worker threads.
`static_reserve_bytes` leaves application headroom above the linked image.
Each supported target has a flash and static-RAM budget.

## Two-stage admission

During CMake configuration, `tools/verify_resources.py plan` combines the
validated deployment plan, selected target, backend capability description,
and effective `.config`. It writes:

```text
generated/deployment/resource-plan.json
generated/deployment/resource-plan.md
```

The plan separates local endpoint resources, remote discovery bounds, thread
stacks, static network and POSIX buffer settings, heap backing, middleware
workers, and the application reserve. A failed check stops configuration.

After Zephyr links the image, `tools/verify_resources.py verify` reads the real
ELF, linker map, `.config`, and `zephyr.bin` when present. It writes:

```text
generated/deployment/resource-report.json
generated/deployment/resource-report.md
```

The report compares linked flash and static RAM with the declared limits and
records linker-resolved stack and heap reservations. On ESP32-S3, flash is the
generated binary size and RAM is the occupied high-water span of the
`dram0_0_seg` linker region, matching Zephyr's memory report. On native_sim,
allocated ELF sections provide the corresponding measurements.

## Accounting rule

Thread stacks, fixed buffers, and heap backing are explanatory slices of
linked static RAM. They are not added to the linked-RAM total a second time.
Runtime allocator high-water counters describe use inside already-linked heap
backing and are also never added to static RAM. An optional runtime log adds a
third, clearly labeled evidence class:

```sh
python3 tools/verify_resources.py verify \
  --resource-plan build/lyrical/static-control-esp32s3/generated/deployment/resource-plan.json \
  --build-dir build/lyrical/static-control-esp32s3 \
  --runtime-log /tmp/static-control-uart.log \
  --json /tmp/resource-report.json \
  --markdown /tmp/resource-report.md
```

The resulting report therefore keeps `planned`, `linked`, and
`runtime_measured` data separate.

## Rejection diagnostics

Admission uses stable error identifiers for failures including endpoint and
graph capacities, worker limits, target selection, flash, RAM, and physical
target-region capacity. The deliberately undersized fixture demonstrates a
pre-build rejection:

```sh
python3 tools/compile_deployment.py \
  tests/fixtures/undersized-resources.json \
  --backend rmw_cyclonedds_c \
  --capabilities-dir capabilities \
  --output-dir /tmp/undersized-deployment
```

It exits with status 2 before CMake compilation or flashing:

```text
E_RESOURCE_ENDPOINT_CAPACITY: deployment declares 2 endpoints but resources.local_endpoints is 1
```

The resource verifier reports the first failing check with its required and
available values, while retaining every check in its JSON and Markdown output.

## Accepted measurements

The static-control CycloneDDS reference passed the integrated verifier on
`native_sim/native/64` with 722,607 bytes of linked flash and 1,224,121 bytes
of linked static RAM. With the 16,384-byte application reserve, it retained
159,495 bytes of declared RAM headroom.

The specialized ESP32-S3 image passed with 1,015,956 bytes of flash and
262,120 bytes of linked static RAM. With the same application reserve, it
retained 21,496 bytes under the 300,000-byte deployment budget. These are
image-specific acceptance measurements, not general platform minima.

A post-flash UART capture parsed 1,095 bytes of ROS allocator high-water,
61,986 bytes of middleware allocator high-water, and 1,664 unused bytes in the
control stack. The verifier retained 262,120 bytes as the linked-RAM value; it
did not add either allocator observation to heap backing or static RAM.
