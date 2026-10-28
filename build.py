"""Build the original ARM32 Forge plugin without shipping or reading game data."""
from __future__ import annotations
import argparse
import hashlib
import json
import os
from pathlib import Path
import struct
import subprocess

HERE = Path(__file__).resolve().parent
ALLOWED_IMPORTS = {"forge_mem_getMainTextAddr", "forge_hook_create", "forge_log",
                   "igSliderInt", "igTextUnformatted"}


def main() -> None:
    ap = argparse.ArgumentParser()
    ap.add_argument("--cc", type=Path, required=True)
    ap.add_argument("--output", type=Path, required=True)
    args = ap.parse_args()
    out = args.output.resolve()
    out.mkdir(parents=True, exist_ok=True)
    env = dict(os.environ, PATH=str(args.cc.parent) + os.pathsep + os.environ.get("PATH", ""))
    commands = []

    def run(cmd):
        commands.append([str(x) for x in cmd])
        done = subprocess.run(cmd, capture_output=True, text=True, env=env, timeout=60)
        if done.returncode:
            raise RuntimeError(done.stdout + done.stderr)
        if done.stderr.strip():
            print(done.stderr)

    flags = ["--target=armv7a-none-eabi", "-mcpu=cortex-a57", "-marm", "-mfloat-abi=softfp",
             "-fPIC", "-fvisibility=hidden", "-ffreestanding", "-fno-builtin", "-nostdlib", "-O2",
             "-Wall", "-Wextra", "-Werror", "-fno-unwind-tables", "-fno-asynchronous-unwind-tables"]
    objects = []
    for file in ["hud_timing.c", "hud_hooks.c", "hud_hooks.S", "start.S"]:
        obj = out / (file + ".o")
        run([str(args.cc), *flags, "-c", str(HERE / "src" / file), "-o", str(obj)])
        objects.append(str(obj))
    elf = out / "hud-speed.elf"
    run([str(args.cc.parent / "lld.exe"), "-flavor", "gnu", "-m", "armelf", "-shared", "-T", str(HERE / "src/link.ld"),
         "--hash-style=sysv", "-z", "norelro", "-z", "max-page-size=0x1000", "-o", str(elf), *objects])
    data = elf.read_bytes()
    assert data[:7] == b"\x7fELF\x01\x01\x01" and struct.unpack_from("<H", data, 18)[0] == 40
    phoff, shoff = struct.unpack_from("<II", data, 28)
    phsize, phnum, shsize, shnum = struct.unpack_from("<HHHH", data, 42)
    segments = []
    for i in range(phnum):
        kind, off, addr, _, size, memsize, flags, align = struct.unpack_from("<8I", data, phoff + i * phsize)
        if kind == 1:
            assert off + size <= len(data) and size <= memsize
            segments.append((off, addr, size, memsize, flags))
    image = bytearray(max(addr + size for _, addr, size, _, _ in segments))
    for off, addr, size, _, _ in segments:
        image[addr:addr + size] = data[off:off + size]
    assert image[0x10:0x14] == b"NRO0"
    image_size = struct.unpack_from("<I", image, 0x18)[0]
    assert image_size >= len(image) and image_size < 1024 * 1024
    image.extend(b"\0" * (image_size - len(image)))
    image[0x40:0x60] = hashlib.sha256(data).digest()
    sections = [struct.unpack_from("<10I", data, shoff + i * shsize) for i in range(shnum)]
    imports, exports, export_sizes = [], {}, {}
    for sec in sections:
        if sec[1] != 11:
            continue
        strings = sections[sec[6]]
        strings = data[strings[4]:strings[4] + strings[5]]
        for off in range(sec[4] + 16, sec[4] + sec[5], 16):
            name, value, size, info, other, index = struct.unpack_from("<IIIBBH", data, off)
            name = strings[name:strings.index(b"\0", name)].decode()
            if not index:
                imports.append(name)
            else:
                exports[name] = value
                export_sizes[name] = size
    expected_imports = ALLOWED_IMPORTS
    assert set(imports) == expected_imports, imports
    assert all(name in exports for name in ["forge_onLoad", "forge_onInit", "forge_onImGuiRender"])
    assert export_sizes.get('buff_timing_diagnostics') == 14400
    nro = out / "hud-speed" / "romfs/nativeNX/plugins/hud-speed.nro"
    nro.parent.mkdir(parents=True, exist_ok=True)
    nro.write_bytes(image)
    result = dict(nro=str(nro), nro_sha256=hashlib.sha256(image).hexdigest(), size=len(image),
                  elf=str(elf), elf_sha256=hashlib.sha256(data).hexdigest(), imports=sorted(imports),
                  exports=exports, commands=commands,
                  sources={str(p.relative_to(HERE)): hashlib.sha256(p.read_bytes()).hexdigest()
                           for p in sorted((HERE / "src").iterdir())})
    result['standalone_forge'] = True
    result['diagnostics'] = dict(
        symbol='buff_timing_diagnostics', module_relative_offset=exports['buff_timing_diagnostics'],
        address_rule='loaded NRO base + module_relative_offset; resolve the export if the plugin load order changes',
        byte_size=14400, version=1, magic=0x47445442, header_size=64, record_size=56, capacity=256,
        tick_frequency=19200000, byte_order='little', default_enabled=False,
        controls={'enabled': {'offset':20, 'type':'u32', 'record':1, 'off':0},
                  'wrap': {'offset':24, 'type':'u32', 'stop_when_full':0, 'overwrite_oldest':1},
                  'reset_request': {'offset':28, 'type':'u32', 'use':'write a new nonce; the next admitted selector call acknowledges it, even when disabled'}},
        header_fields=dict(magic=0,version=4,header_size=8,record_size=12,capacity=16,enabled=20,wrap=24,
                           reset_request=28,reset_ack=32,sequence=36,record_count=40,retained=44,next_slot=48,
                           dropped=52,observed_calls=56,reserved=60),
        record_fields=dict(serial=0,event=4,ticks=8,state=16,table=20,before_index=24,index=28,before_active=32,
                           active=36,dwell_ms=40,countdown=44,timing_flags=48,table_count=52),
        record_types={'ticks':'u64','countdown':'i32','all_other_fields':'u32'},
        event_bits={'1':'index changed','2':'active changed'}, timing_flag_bits={'1':'elapsed timing enabled','2':'hold icon enabled'},
        snapshot_rule='Pause guest for memsave, or read sequence before and after the complete buffer and accept only an unchanged even value. Controls are aligned u32 writes. Do not write other fields.',
        counters='record_count, observed_calls and record serial wrap modulo 2^32; dropped saturates. retained never exceeds capacity. next_slot is the next write; when full it is the oldest retained record.',
        transitions='Only admitted table calls whose index or active value changes; countdown-only changes and unknown tables produce no records. observed_calls counts all admitted calls while recording.',
        reset='Counts and retained length reset; old record bytes remain but are outside the retained range. Disable, request reset, wait for reset_ack, set wrap policy, then enable.')
    (out / "build.json").write_text(json.dumps(result, indent=2) + "\n")
    print(json.dumps(result, indent=2))


if __name__ == "__main__":
    main()
