#!/usr/bin/env python3
"""Attribute an ESP32-C6 Zephyr image's linked RAM to owners, and compare images.

The ledger attributes linked RAM from the linker map and ELF.
Padding and unmatched sections are reported separately.
It is static accounting: heaps are counted at their reserved size, and runtime
use inside them needs separate measurement.

    ram_ledger.py BUILD [--json OUT]
    ram_ledger.py BUILD_BEFORE BUILD_AFTER [--markdown OUT]
"""
import argparse
import json
import re
import sys
from collections import Counter
from pathlib import Path

from esp32c6_resources import inventory

RAM_OUTPUT_SECTIONS = (
    ".iram0.text", ".loader.text", ".iram0.bss", ".dram0.data", ".loader.data",
    ".dram0.noinit", ".dram0.bss",
)
# Iterable sections Zephyr places between .dram0.data and .dram0.noinit.
ITERABLE = re.compile(r"^(sw_isr_table|device_states|\w+_area)$")

OWNERS = (
    ("Cyclone DDS", re.compile(r"^libddsc\.a$")),
    ("ROS 2 client libraries", re.compile(
        r"^lib(rcl|rclc|rcutils|rmw|rmw_cyclonedds_c|rosidl\w*|rcl_\w+|"
        r"\w+__rosidl_\w+|ros2_zephyr|tracetools)\.a$")),
    ("Wi-Fi vendor stack", re.compile(
        r"^lib(net80211|pp|phy|core|espnow|mesh|wpa_supplicant|coexist|"
        r"drivers__wifi|btdm_app|smartconfig)\.a$")),
    ("Zephyr networking", re.compile(r"^libsubsys__net\w*\.a$")),
    ("Zephyr kernel", re.compile(r"^lib(kernel|arch__\w+)\.a$")),
    ("POSIX layer", re.compile(r"^liblib__posix\w*\.a$")),
    ("C library", re.compile(r"^lib(c|liblib__libc\w*|lib__libc\w*|m|gcc)\.a$")),
    ("Application", re.compile(r"^libapp\.a$")),
    ("Crypto", re.compile(r"^lib(tfpsacrypto|mbedtls\w*)\.a$")),
    ("Zephyr HAL and drivers", re.compile(r"^lib(zephyr|drivers__\w+|soc__\w+|lib__\w+)\.a$")),
)
HEAP_SECTIONS = (
    ("libc heap arena", re.compile(r"stdlib/malloc\.c")),
    ("kernel heap", re.compile(r"kheap_buf__system_heap")),
)


def owner_of(archive):
    for name, pattern in OWNERS:
        if pattern.match(archive):
            return name
    return "Other"


def input_sections(map_text):
    """Yield (output_section, input_section, address, size, archive, member)."""
    lines = map_text.splitlines()
    current = None
    started = False
    for i, line in enumerate(lines):
        if line.startswith("Linker script and memory map"):
            started = True
            continue
        if not started:
            continue
        head = re.match(r"^(\.?[A-Za-z_][\w.]*)(\s|$)", line)
        if head:
            current = head.group(1)
            continue
        if current not in RAM_OUTPUT_SECTIONS and not (current and ITERABLE.match(current)):
            continue
        m = re.match(r"^ (\S+)\s+(0x[0-9a-f]+)\s+(0x[0-9a-f]+)\s+(\S+)", line)
        if m:
            name, addr, size, origin = m.groups()
        else:
            m1 = re.match(r"^ (\S+)$", line)
            m2 = (re.match(r"^\s+(0x[0-9a-f]+)\s+(0x[0-9a-f]+)\s+(\S+)", lines[i + 1])
                  if m1 and i + 1 < len(lines) else None)
            if not m2:
                continue
            name = m1.group(1)
            addr, size, origin = m2.groups()
        # GNU ld reports pre-merge string sizes that can overlap live data.
        # Leave these bytes unattributed instead of counting them twice.
        if re.search(r"\.str\d+\.", name):
            continue
        size = int(size, 16)
        if size == 0 or int(addr, 16) == 0:
            continue
        am = re.match(r"(?:.*/)?([^/(]+\.a)\(([^)]+)\)$", origin)
        archive, member = (am.group(1), am.group(2)) if am else (Path(origin).name, "")
        yield current, name, int(addr, 16), size, archive, member


def stack_symbols(elf_path):
    from elftools.elf.elffile import ELFFile
    out = {}
    with open(elf_path, "rb") as f:
        elf = ELFFile(f)
        for sym in elf.get_section_by_name(".symtab").iter_symbols():
            if (sym["st_info"]["type"] == "STT_OBJECT" and sym["st_size"] >= 512
                    and re.search(r"stack", sym.name, re.I)):
                out[sym.name] = (int(sym["st_value"]), int(sym["st_size"]))
    return out


def ledger(build):
    build = Path(build)
    zdir = build / "zephyr"
    map_text = (zdir / "zephyr.map").read_text(errors="replace")
    stacks = stack_symbols(zdir / "zephyr.elf")
    measured = inventory(build)
    regions = [(name, region["origin"], region["capacity_bytes"])
               for name, region in measured["ram_regions"].items()]
    tls = sum(measured["tls"][key] or 0 for key in ("__tdata_size", "__tbss_size"))
    rows = Counter()
    total = 0
    for out, name, addr, size, archive, member in input_sections(map_text):
        if not any(o <= addr < o + n for _, o, n in regions):
            continue
        total += size
        category = None
        for label, pattern in HEAP_SECTIONS:
            if pattern.search(name):
                category = ("Heaps (reserved)", label)
        if category is None and out in (".iram0.text", ".loader.text"):
            category = ("Code in RAM", owner_of(archive))
        if category is None and re.search(r"stack", name, re.I):
            category = ("Thread stacks", name.split(".")[-1])
        if category is None:
            inside = [sym for sym, (sa, ss) in stacks.items() if addr <= sa < addr + size]
            if inside:
                category = ("Thread stacks", " + ".join(sorted(inside)))
        if category is None:
            category = ("Static data", owner_of(archive))
        rows[category] += size
    linked = measured["linked_ram_span_bytes"]
    if total > linked:
        raise ValueError("Attributed sections exceed linked RAM")
    rows[("Unattributed", "Padding and unmatched sections")] += linked - total
    return {
        "build": str(build),
        "linked_span_bytes": linked,
        "section_bytes": total,
        "tls_template_bytes": tls,
        "ram_regions": [{"name": n, "origin": o, "length": l} for n, o, l in regions],
        "stacks": {k: v[1] for k, v in sorted(stacks.items(), key=lambda kv: -kv[1][1])},
        "rows": [
            {"group": g, "owner": o, "bytes": b}
            for (g, o), b in sorted(rows.items(), key=lambda kv: (kv[0][0], -kv[1]))
        ],
    }


def group_totals(led):
    totals = Counter()
    for r in led["rows"]:
        totals[r["group"]] += r["bytes"]
    return totals


def print_single(led):
    print(f"Linked RAM span: {led['linked_span_bytes']:,} bytes "
          f"(sections {led['section_bytes']:,}); TLS template {led['tls_template_bytes']} bytes")
    totals = group_totals(led)
    for group, size in totals.most_common():
        print(f"\n{group}: {size:,}")
        for r in led["rows"]:
            if r["group"] == group and r["bytes"] > 0:
                print(f"  {r['bytes']:9,}  {r['owner']}")


def compare(a, b):
    ka = {(r["group"], r["owner"]): r["bytes"] for r in a["rows"]}
    kb = {(r["group"], r["owner"]): r["bytes"] for r in b["rows"]}
    keys = sorted(set(ka) | set(kb), key=lambda k: (k[0], -(ka.get(k, 0) + kb.get(k, 0))))
    lines = ["| Group | Owner | Before | After | Change |", "|---|---|---:|---:|---:|"]
    for k in keys:
        x, y = ka.get(k, 0), kb.get(k, 0)
        if x != y:
            lines.append(f"| {k[0]} | {k[1]} | {x:,} | {y:,} | {y - x:+,} |")
    lines.append(f"| **Linked RAM span** | | **{a['linked_span_bytes']:,}** | "
                 f"**{b['linked_span_bytes']:,}** | "
                 f"**{b['linked_span_bytes'] - a['linked_span_bytes']:+,}** |")
    lines.append(f"| TLS template per thread | | {a['tls_template_bytes']} | "
                 f"{b['tls_template_bytes']} | {b['tls_template_bytes'] - a['tls_template_bytes']:+} |")
    return "\n".join(lines)


def main():
    p = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    p.add_argument("builds", nargs="+")
    p.add_argument("--json")
    p.add_argument("--markdown")
    a = p.parse_args()
    if len(a.builds) not in (1, 2):
        p.error("provide one build to inspect or two builds to compare")
    leds = [ledger(b) for b in a.builds]
    if a.json:
        Path(a.json).write_text(json.dumps(leds if len(leds) > 1 else leds[0], indent=2) + "\n")
    if len(leds) == 1:
        print_single(leds[0])
    else:
        table = compare(leds[0], leds[-1])
        print(table)
        if a.markdown:
            Path(a.markdown).write_text(table + "\n")
    return 0


if __name__ == "__main__":
    sys.exit(main())
