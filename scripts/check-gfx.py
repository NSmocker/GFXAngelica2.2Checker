"""Запуск справжнього checker для вибірки GFX, аналіз PNG і збереження доказів."""

import argparse
import csv
import json
import hashlib
import random
import shutil
import subprocess
import time
from pathlib import Path

from PIL import Image, ImageDraw, ImageFont


def select_files(root, limit, seed=None):
    # Рівномірна вибірка за верхньою папкою; формат GFX читає виключно рушій.
    groups = {}
    for file in sorted((root / "gfx").rglob("*.gfx")):
        relative = file.relative_to(root / "gfx")
        groups.setdefault(relative.parts[0], []).append(str(relative))
    if seed is not None:
        rng = random.Random(seed)
        for files in groups.values():
            rng.shuffle(files)
    selected = []
    index = 0
    while len(selected) < limit:
        added = False
        for files in groups.values():
            if index < len(files):
                selected.append(files[index])
                added = True
                if len(selected) == limit:
                    break
        if not added:
            break
        index += 1
    return selected


def render_case(exe, root, file, frames, case_dir, timeout):
    case_dir.mkdir()
    png = case_dir / "frame.png"
    started = time.monotonic()
    record = dict(status="FAIL", exit_code="", seconds=0, frames=frames, nonblack=0, maximum=0)
    with (case_dir / "stdout.log").open("wb") as stdout, (case_dir / "stderr.log").open("wb") as stderr:
        try:
            process = subprocess.run([str(exe), "--preview", "--root", str(root),
                                      "--gfx", str(root / "gfx" / file), "--frames", str(frames), "--output", str(png)],
                                     stdout=stdout, stderr=stderr, timeout=timeout,
                                     creationflags=subprocess.CREATE_NO_WINDOW)
            record["exit_code"] = process.returncode
        except subprocess.TimeoutExpired:
            record["status"] = "TIMEOUT"
        except OSError as error:
            stderr.write(str(error).encode("utf-8"))
    record["seconds"] = round(time.monotonic() - started, 3)
    for log in ("checker.log", "A3D.log", "AF.log", "AM.log"):
        source = exe.parent / log
        if source.exists():
            shutil.copy2(source, case_dir / log)
    if png.exists():
        with Image.open(png) as image:
            rgb = image.convert("RGB")
            record["nonblack"] = sum(max(pixel) > 0 for pixel in rgb.getdata())
            record["maximum"] = max(max(channel) for channel in rgb.getextrema())
            if record["exit_code"] == 0:
                record["status"] = "VISIBLE" if record["nonblack"] else "EMPTY"
    return record, png


def contact_sheet(output, records):
    font = ImageFont.truetype("C:/Windows/Fonts/segoeui.ttf", 13)
    for page_start in range(0, len(records), 48):
        page = records[page_start:page_start + 48]
        canvas = Image.new("RGB", (6 * 180, ((len(page) + 5) // 6) * 210), (35, 35, 35))
        draw = ImageDraw.Draw(canvas)
        for index, record in enumerate(page):
            x, y = (index % 6) * 180, (index // 6) * 210
            png = output / record["png"]
            if png.is_file():
                with Image.open(png) as image:
                    image.thumbnail((176, 176))
                    canvas.paste(image.convert("RGB"), (x, y))
            color = (120, 230, 140) if record["status"] == "VISIBLE" else (255, 170, 90)
            draw.text((x + 4, y + 178), f'{record["idx"]}: {record["status"]}', font=font, fill=color)
            draw.text((x + 4, y + 194), f'{record["nonblack"]} px / max {record["maximum"]}', font=font, fill="white")
        canvas.save(output / f"contact-{page_start // 48 + 1:03}.png")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", type=Path, default=Path("B:/Git/PW146RUUnpackedAssets"))
    parser.add_argument("--exe", type=Path, default=Path(__file__).resolve().parents[1] / "bin/x64/Release/GFXAngelica2.2Checker.exe")
    parser.add_argument("--out", type=Path, required=True)
    parser.add_argument("--manifest", type=Path)
    parser.add_argument("--limit", type=int, default=48)
    parser.add_argument("--frames", type=int, default=10)
    parser.add_argument("--retry-frames", type=int, default=0, help="Repeat empty renders with a longer simulation; preserve both PNGs/logs.")
    parser.add_argument("--seed", type=int, help="Shuffle within each directory for a reproducible broader sample.")
    parser.add_argument("--timeout", type=float, default=20)
    args = parser.parse_args()
    if args.limit < 1 or args.frames < 1 or args.retry_frames < 0 or args.timeout <= 0:
        parser.error("Counts and timeout must be positive (retry-frames may be zero).")
    args.root = args.root.resolve()
    output = args.out.resolve()
    output.mkdir(parents=True, exist_ok=True)
    if (output / "report.csv").exists():
        parser.error("Use a new output directory to preserve earlier evidence.")
    files = json.loads(args.manifest.read_text(encoding="utf-8")) if args.manifest else select_files(args.root, args.limit, args.seed)
    (output / "manifest.json").write_text(json.dumps(files, ensure_ascii=False, indent=2), encoding="utf-8")
    # Кожен прогін має власний exe та журнали; наступна збірка не змінює його посеред роботи.
    runtime = output / "runtime"
    runtime.mkdir()
    exe = runtime / args.exe.name
    shutil.copy2(args.exe, exe)
    for dll in args.exe.parent.glob("*.dll"):
        shutil.copy2(dll, runtime / dll.name)
    (output / "run.json").write_text(json.dumps(dict(root=str(args.root), frames=args.frames,
        retry_frames=args.retry_frames, seed=args.seed, exe_sha256=hashlib.sha256(exe.read_bytes()).hexdigest()), indent=2), encoding="utf-8")
    records = []
    columns = ["idx", "file", "status", "exit_code", "seconds", "frames", "attempts", "nonblack", "maximum", "png"]
    with (output / "report.csv").open("w", encoding="utf-8-sig", newline="") as report:
        writer = csv.DictWriter(report, fieldnames=columns, delimiter=";")
        writer.writeheader()
        for index, file in enumerate(files, 1):
            case_dir = output / f"{index:05}"
            record, png = render_case(exe, args.root, file, args.frames, case_dir, args.timeout)
            attempts = 1
            if record["status"] == "EMPTY" and args.retry_frames > args.frames:
                first_seconds = record["seconds"]
                record, png = render_case(exe, args.root, file, args.retry_frames, case_dir / "retry", args.timeout)
                record["seconds"] += first_seconds
                attempts = 2
            record.update(idx=index, file=file, attempts=attempts, png=str(png.relative_to(output)))
            records.append(record)
            writer.writerow(record)
            report.flush()
            print(json.dumps(record, ensure_ascii=True), flush=True)
    contact_sheet(output, records)
    counts = {status: sum(r["status"] == status for r in records) for status in ("VISIBLE", "EMPTY", "FAIL", "TIMEOUT")}
    (output / "summary.json").write_text(json.dumps(counts, indent=2), encoding="utf-8")
    print(json.dumps(counts), flush=True)


if __name__ == "__main__":
    main()
