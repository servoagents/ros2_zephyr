#!/usr/bin/env python3
"""Image-bound admission for controlled hardware qualification, not a safety proof.

Execution budgets are explicit engineering allocations. Watermarks from startup,
traffic, failure and shutdown must subsequently qualify the frozen image.
"""
import argparse, hashlib, json, re, subprocess
from pathlib import Path
from elftools.elf.elffile import ELFFile
from esp32c6_resources import inventory

ROOT = Path(__file__).resolve().parents[1]
STATIC = {
    "z_main_stack": ("main", 6144),
    "z_idle_stacks": ("idle", 512),
    "sys_work_q_stack": ("system workqueue", 3072),
    "logging_stack": ("logger", 1024),
    "mgmt_stack": ("network management", 1408),
    "rx_stack": ("network RX", 1408),
    "work_q_stack": ("TCP workqueue", 1408),
    "timer_task_stack": ("ESP timer", 3072),
    "z_interrupt_stacks": ("ISR (no TLS)", 1536),
}
BASE_CALLERS = {
    "bg_thread_main",
    "z_init_cpu",
    "z_cstart",
    "z_impl_k_thread_create",
    "k_work_queue_start",
    "k_sys_work_q_init",
    "enable_logger",
    "esp_timer_init",
    "task_create_wrapper",
    "task_create_pinned_to_core_wrapper",
    "net_mgmt_event_init",
    "net_tc_rx_init",
    "net_tcp_init",
}


def stack_budget(
    backing, execution, tls_size, tls_alignment, overhead, margin=256, isr=False
):
    """Account for a TLS copy inside each reserved stack, including alignment loss."""
    if min(backing, execution, tls_size, overhead, margin) < 0 or tls_alignment < 1:
        raise ValueError("Invalid stack budget")
    reserve = 0 if isr else tls_size + tls_alignment - 1
    required = reserve + overhead + execution + margin
    return dict(
        backing_bytes=backing,
        tls_upper_bytes=reserve,
        other_upper_bytes=overhead,
        execution_budget_bytes=execution,
        margin_bytes=margin,
        required_bytes=required,
        remaining_after_budget_bytes=backing - required,
        status="PASS" if backing >= required else "FAIL",
    )


def admit(build, objdump, main_execution_bytes=8704):
    b = Path(build)
    r = inventory(b)
    failures = []
    cfg = dict(
        re.findall(r"^(CONFIG_\w+)=(.*)$", (b / "zephyr/.config").read_text(), re.M)
    )
    elfpath = b / "zephyr/zephyr.elf"
    with elfpath.open("rb") as f:
        symbols = {
            s.name: (int(s["st_value"]), int(s["st_size"]), s["st_info"]["type"])
            for s in ELFFile(f).get_section_by_name(".symtab").iter_symbols()
        }
    tls = r["tls"]
    t = (tls["__tdata_size"] or 0) + (tls["__tbss_size"] or 0)
    ta = max(tls["__tdata_align"] or 1, tls["__tbss_align"] or 1)
    # 80-byte arch_esf (generated offsets), up to 15-byte SP alignment, 4 sentinel.
    offsets = (b / "zephyr/include/generated/zephyr/offsets.h").read_text()
    esf = int(
        re.search(r"#define __struct_arch_esf_SIZEOF (0x[0-9a-f]+)", offsets)[1], 16
    )
    other = esf + 15 + (4 if cfg.get("CONFIG_STACK_SENTINEL") == "y" else 0)
    margin = 256
    rows = []
    a_profile = cfg.get("CONFIG_ROS2_ZEPHYR_RMW_CYCLONEDDS_C") == "y"
    c_profile = cfg.get("CONFIG_NROS_C_API") == "y"
    allowed_callers = set(BASE_CALLERS)
    allowed_stacks = set(STATIC)

    def add(name, backing, execution, source, isr=False):
        row = stack_budget(backing, execution, t, ta, other, margin, isr)
        row.update(thread=name, source=source)
        rows.append(row)

    for symbol, (name, budget) in STATIC.items():
        if symbol in symbols:
            add(
                name,
                symbols[symbol][1],
                (
                    (
                        12288
                        if c_profile
                        else main_execution_bytes if a_profile else budget
                    )
                    if symbol == "z_main_stack"
                    else budget
                ),
                "ELF:" + symbol,
                symbol == "z_interrupt_stacks",
            )
    # Vendor function disassembly proves all four returns; no unknown stack argument.
    objdump = Path(objdump)
    dis = subprocess.check_output([str(objdump), "-d", str(elfpath)], text=True)
    (b / "disassembly.txt").write_text(dis)
    match = re.search(r"<config_get_wifi_task_stack_size>:\n(.*?)(?=\n\n)", dis, re.S)
    vendor_known = bool(
        match
        and "0x2" in match[1]
        and "-2048" in match[1]
        and "-1024" in match[1]
        and "512" in match[1]
    )
    if vendor_known:
        if a_profile:
            wrapped = "__wrap_config_get_wifi_task_stack_size" in symbols
            pp = re.search(r"<pp_create_task>:\n(.*?)(?=\n\n)", dis, re.S)
            if (
                not wrapped
                or not pp
                or "<__wrap_config_get_wifi_task_stack_size>" not in pp[1]
            ):
                failures.append(
                    "A vendor provider wrapper missing from actual pp_create_task call"
                )
            reserve = t + ta - 1
            backing = ((3072 + reserve + 99 + 256 + 15) // 16) * 16 if wrapped else 3072
            add(
                "vendor Wi-Fi minimum (A explicit provider)",
                backing,
                3072,
                "A vendor_stack.c and linked pp_create_task call",
            )
        else:
            add(
                "vendor Wi-Fi (minimum of 3072/3584/6144/6656)",
                3072,
                2048,
                "config_get_wifi_task_stack_size -> pp_create_task -> OS adapter -> k_thread_stack_alloc (system heap)",
            )
    else:
        failures.append("Unknown vendor Wi-Fi stack argument")
    callers = set()
    name = ""
    for line in dis.splitlines():
        m = re.match(r"^[0-9a-f]+ <(.+)>:", line)
        if m:
            name = m[1]
            continue
        if any(
            "<" + x + ">" in line
            for x in (
                "z_impl_k_thread_create",
                "k_work_queue_start",
                "z_setup_new_thread",
                "pthread_create",
            )
        ):
            callers.add(name)
    if a_profile:
        report = b / "generated/deployment/qualification-resource-report.json"
        command = [
            str(ROOT / "tools/verify_resources.py"),
            "verify",
            "--resource-plan",
            str(b / "generated/deployment/resource-plan.json"),
            "--build-dir",
            str(b),
            "--json",
            str(report),
            "--markdown",
            str(report.with_suffix(".md")),
        ]
        import sys

        checked = subprocess.run(
            [sys.executable, *command], capture_output=True, text=True
        )
        if checked.returncode:
            failures.append(
                "Deployment resource check failed: " + checked.stderr.strip()
            )
        allowed_callers.update(("ddsrt_thread_create", "pthread_create"))
        allowed_stacks.add("zephyr_stacks")
        slots = int(cfg["CONFIG_ROS2_ZEPHYR_CYCLONEDDS_THREAD_COUNT"])
        each = int(cfg["CONFIG_ROS2_ZEPHYR_CYCLONEDDS_THREAD_STACK_SIZE"])
        if symbols.get("zephyr_stacks", (0, 0))[1] != slots * each:
            failures.append("Cyclone stack pool extent mismatch")
        for i in range(slots):
            add(
                "Cyclone reserved slot " + str(i),
                each,
                8192,
                "ELF zephyr_stacks / configured count; DDSRT pthread_attr_setstack",
            )
    if c_profile:
        allowed_callers.update(("nros_zephyr_task_create_prio", "pthread_create"))
        allowed_stacks.add("nros_thread_stacks")
        slots = int(cfg["CONFIG_NROS_ZEPHYR_TASK_SLOTS"])
        each = int(cfg["CONFIG_NROS_ZEPHYR_TASK_STACK_SIZE"]) or int(
            cfg["CONFIG_MAIN_STACK_SIZE"]
        )
        if symbols.get("nros_thread_stacks", (0, 0))[1] != slots * each:
            failures.append("nano-ROS stack pool extent mismatch")
        for i in range(slots):
            add(
                "nano-ROS reserved task slot " + str(i),
                each,
                6144,
                "ELF nros_thread_stacks; official nros_zephyr_task_create_prio pthread_attr_setstack",
            )
    unknown = sorted(callers - allowed_callers)
    if unknown:
        failures.append(
            "Unreviewed linked thread/workqueue callers: " + ", ".join(unknown)
        )
    # Catch newly linked static stack backing. Pools and ISR are reported separately.
    unknown_stacks = [
        n
        for n, (_, size, typ) in symbols.items()
        if typ == "STT_OBJECT"
        and size >= 256
        and ("stack" in n.lower())
        and n not in allowed_stacks
    ]
    if unknown_stacks:
        failures.append("Unreviewed stack backing: " + ", ".join(unknown_stacks))
    if int(cfg.get("CONFIG_DYNAMIC_THREAD_POOL_SIZE", "0")):
        failures.append("Reserved dynamic stack pool needs explicit review")
    if tls["emulated_tls_present"]:
        failures.append("Emulated TLS")
    if any(x["status"] == "FAIL" for x in rows):
        failures.append("TLS + overhead + execution + margin exceeds backing")
    if r["ram_committed_bytes"] + 16384 > 450000:
        failures.append(
            "RAM plus 16384-byte application reserve exceeds 450000-byte contract"
        )
    if r["image_bytes"] > 2097152:
        failures.append("Flash contract 2097152 bytes exceeded")
    return dict(
        status="NOT_ADMITTED" if failures else "ADMITTED_FOR_QUALIFICATION",
        elf_sha256=hashlib.sha256(elfpath.read_bytes()).hexdigest(),
        image_sha256=r["image_sha256"],
        ram_committed_bytes=r["ram_committed_bytes"],
        application_reserve_bytes=16384,
        ram_contract_bytes=450000,
        image_bytes=r["image_bytes"],
        tls_template_bytes=t,
        stack_checks=rows,
        linked_thread_creation_callers=sorted(callers),
        unreviewed_stack_symbols=unknown_stacks,
        dynamic_stack_pool_count=int(cfg.get("CONFIG_DYNAMIC_THREAD_POOL_SIZE", "0")),
        posix_thread_control_pool_bytes=symbols.get("posix_thread_pool", (0, 0))[1],
        vendor_dynamic_stack_from_already_counted_system_heap=True,
        failures=failures,
        observed_margins_required=True,
        note="Preflash qualification admission only. 256-byte common margin policy; per-thread execution budgets differ. Vendor dynamic allocation is inside the counted heap, never added twice. Dead POSIX create paths must be absent or reviewed. ISR has no TLS. Watermarks do not prove worst-case safety.",
    )


if __name__ == "__main__":
    p = argparse.ArgumentParser()
    p.add_argument("build")
    p.add_argument("output")
    p.add_argument("--objdump", required=True)
    p.add_argument("--main-execution-bytes", type=int, default=8704)
    a = p.parse_args()
    d = admit(a.build, a.objdump, a.main_execution_bytes)
    Path(a.output).write_text(json.dumps(d, indent=2) + "\n")
    print(d["status"])
    print("\n".join(d["failures"]))
    raise SystemExit(bool(d["failures"]))
