# Native DDSRT Milestone A audit and stop report

Milestone A established a native-simulator common baseline and qualified the
Zephyr kernel prerequisite. See [the baseline](native-ddsrt-baseline.md) for
commands, revisions and run evidence, and [the API inventory](native-ddsrt-api-inventory.md)
for the pinned interfaces, callers and linked pools. ESP32-S3 kernel and
loopback execution passed, but static-control startup remains blocked before
Zephyr's banner with the present board power arrangement; the Phase 3
static-control runtime checkpoint is not misrepresented as a new run.

The central finding is that Zephyr v4.4.0's `k_condvar_wait` drops the mutex
on zero timeout and fails to reacquire it on finite timeout. This affects
both direct native use and the pthread path that current Cyclone DDSRT
selects. The exact upstream kernel commit
`5c6c6837cc4026a70e670c265fc4d1e3b1f16379` is applied separately and
idempotently by `scripts/apply_zephyr_condvar_patch.sh`; it must remain a
kernel prerequisite, never be hidden in the Cyclone port. Native/underlying
POSIX tests go from 2/8 pass to 8/8 pass. The pre-existing Cyclone 4.4
timeout relock workaround is compiled only when this kernel repair is absent.

The pinned Cyclone port uses pthreads for every synchronization type and
thread lifecycle, although its worker stacks are already statically reserved
with Zephyr's stack macro. It also uses POSIX clocks and sleep. There are
four condition-variable type families, reader/writer locks, once, cleanup
keys/destructors, compiler TLS, thread attributes, IDs and names. RMW itself
uses DDS public APIs; ROS dependencies still contain pthread references.
Therefore a three-function mutex/cond/thread rewrite would be incomplete,
and no current measurement justifies claiming that native DDSRT eliminates
all POSIX storage or worker stack RAM. The Zephyr thread stack index is
monotonic, with no reuse after joins; that is a later lifecycle topic.

The smallest justified **next Cyclone patch**, after hardware confirmation,
is an opt-in Zephyr-native synchronization selector with matching public
types and source selection, initially covering mutex, all four condvar
families, rwlock and once while retaining POSIX threads/time. It needs a
source-selection proof and targeted tests for ownership, clock domains,
timeout rounding/range, reader concurrency and once publication. It must not
remove `Threads::Threads` or POSIX pools while pthread consumers remain.
Only after that comparison should native thread ownership/cleanup be designed
and implemented. No native threads, public selector, or Cyclone source patch
were implemented in Milestone A.

The local fork stays named `cyclonedds`; its experimental local branch is
`zephyr-native-ddsrt` at the exact accepted Cyclone SHA. The project manifest
points to that fork by URL and SHA, and does not vendor or copy Cyclone source
into `ros2_zephyr`. No rebase was performed: the locally known upstream
master adds only three unrelated OpenSSL/logging commits beyond the accepted pin.
The starting project worktree's unrelated untracked `ram.json` was preserved.
