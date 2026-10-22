#!/usr/bin/env python3
"""ESP32-C6 HP-core ELF/map inventory. Never add observed usage to backing reserves."""
import argparse, hashlib, json, re
from pathlib import Path
from elftools.elf.elffile import ELFFile


def inventory(build):
    z = Path(build) / "zephyr"
    config = dict(re.findall(r"^(CONFIG_\w+)=(.*)$", (z / ".config").read_text(), re.M))
    if config.get("CONFIG_SOC_ESP32C6_HPCORE") != "y":
        raise ValueError("C6 HP-core only")
    memory = (
        (z / "zephyr.map")
        .read_text()
        .split("Memory Configuration\n", 1)[1]
        .split("Linker script", 1)[0]
    )
    regions = {}
    for name, origin, length in re.findall(
        r"^(sram0_0_seg|lp_ram_seg)\s+(0x\w+)\s+(0x\w+)", memory, re.M
    ):
        regions[name] = {
            "origin": int(origin, 16),
            "capacity_bytes": int(length, 16),
            "sections": [],
        }
    if set(regions) != {"sram0_0_seg", "lp_ram_seg"}:
        raise ValueError("Unrecognized C6 linker regions")
    with (z / "zephyr.elf").open("rb") as f:
        elf = ELFFile(f)
        symbols = {
            s.name: int(s["st_value"])
            for s in elf.get_section_by_name(".symtab").iter_symbols()
        }
        alloc = []
        for sec in elf.iter_sections():
            if not int(sec["sh_flags"]) & 2 or not sec["sh_size"]:
                continue
            a, n = int(sec["sh_addr"]), int(sec["sh_size"])
            item = {
                "name": sec.name,
                "address": a,
                "bytes": n,
                "nobits": sec["sh_type"] == "SHT_NOBITS",
            }
            alloc.append(item)
            for region in regions.values():
                o, c = region["origin"], region["capacity_bytes"]
                if o <= a < o + c:
                    if a + n > o + c:
                        raise ValueError("Section crosses physical RAM boundary")
                    region["sections"].append(item)
        for region in regions.values():
            end = max(
                (s["address"] + s["bytes"] for s in region["sections"]),
                default=region["origin"],
            )
            region["linked_span_bytes"] = end - region["origin"]
            region["section_bytes"] = sum(s["bytes"] for s in region["sections"])
        tls = {
            k: symbols.get(k)
            for k in ("__tdata_size", "__tbss_size", "__tdata_align", "__tbss_align")
        }
        tls["emulated_tls_present"] = any(s.startswith("__emutls") for s in symbols)
    arena = int(config.get("CONFIG_COMMON_LIBC_MALLOC_ARENA_SIZE", "0"))
    dynamic_tail = (
        max(0, symbols.get("_heap_sentry", 0) - symbols.get("_end", 0))
        if arena == -1
        else 0
    )
    # _libc_heap_size is printed by the ESP loader even for a fixed malloc arena.
    # Do not count it as an allocation when COMMON_LIBC_MALLOC_ARENA_SIZE >= 0.
    return {
        "board": config["CONFIG_BOARD_TARGET"].strip(chr(34)),
        "image_bytes": (z / "zephyr.bin").stat().st_size,
        "image_sha256": hashlib.sha256((z / "zephyr.bin").read_bytes()).hexdigest(),
        "ram_regions": regions,
        "linked_ram_span_bytes": sum(v["linked_span_bytes"] for v in regions.values()),
        "malloc_arena_config_bytes": arena,
        "additional_dynamic_tail_commitment_bytes": dynamic_tail,
        "ram_committed_bytes": sum(v["linked_span_bytes"] for v in regions.values())
        + dynamic_tail,
        "tls": tls,
        "allocated_sections": alloc,
        "observed_stack_margins": None,
        "qualification": "inventory only; not a full candidate stack-budget admission",
        "notes": [
            "RAM spans count alignment holes and .noinit; flash aliases are excluded.",
            "Stacks, static heaps and network pools already in these regions are counted once.",
            "TLS copies consume stack reservations; TLS template sections are not multiplied here.",
            "Image bytes are the actual zephyr.bin programmed by the simple-boot runner.",
            "No claim of minimum RAM or measured stack margin.",
        ],
    }


if __name__ == "__main__":
    p = argparse.ArgumentParser()
    p.add_argument("build")
    p.add_argument("output")
    a = p.parse_args()
    Path(a.output).write_text(json.dumps(inventory(a.build), indent=2) + "\n")
