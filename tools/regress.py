#!/usr/bin/env python3
"""PortPS5 local regression runner and results JSON writer (verification.md 2 and 4).

Subcommands:

* ``prepare``  converts a dump (relinker --windows, never --skip-sce-module,
  which crashes guest libc++ iostream code), copies the runtime libs and links
  ``app0`` to the dump. The dump is only ever read through the link.
* ``run``      launches the converted exe with the install dir as cwd, waits
  for the run, consumes the structured telemetry log (portps5.telemetry/1,
  written by the runtime, spec section 4.1) and writes ``portps5.results/1``.
* ``report``   builds the results JSON from an existing telemetry log, which
  is also how hosted CI exercises everything except the launch.

Privacy: stdout and stderr of the title are redirected to files that are never
read. Only whitelisted telemetry fields are extracted, and the results JSON
holds metrics, hashes and pass/fail only. All work files stay outside the repo.
"""

import argparse
import hashlib
import json
import math
import os
import shutil
import subprocess
import sys
import time
from pathlib import Path

import regress_metrics as rm

ROOT = Path(__file__).resolve().parent.parent
RESULTS_SCHEMA = "portps5.results/1"
TELEMETRY_SCHEMA = "portps5.telemetry/1"
TELEMETRY_REL = Path("logs") / "telemetry.jsonl"
RUNNER_REL = Path("logs") / "runner.json"


class RegressError(Exception):
    """A user-facing failure (bad input, unsafe path, missing telemetry)."""


def sha256_file(path):
    """Return the SHA-256 hex digest of a file, read in chunks."""
    h = hashlib.sha256()
    with open(path, "rb") as fh:
        for chunk in iter(lambda: fh.read(1 << 20), b""):
            h.update(chunk)
    return h.hexdigest()


def ensure_outside_repo(path):
    """Raise unless path is outside the repository, so dumps and logs never land in git."""
    p = Path(path).resolve()
    if p == ROOT or ROOT in p.parents:
        raise RegressError(f"refusing to use a path inside the repository: {p}")


def finite(value):
    """Convert to float and reject NaN and infinity (json.loads accepts both literals)."""
    number = float(value)
    if not math.isfinite(number):
        raise ValueError("non-finite number")
    return number


def event_value(ev, rec):
    """Extract and convert the numeric fields of a known event; None for other events.

    Every float must be finite: 1e309 parses to infinity and NaN is a valid JSON literal for
    Python, either of which would reach the results JSON as invalid standard JSON.
    Raises KeyError, TypeError or ValueError on a malformed field, which the caller turns
    into a RegressError carrying the line number.
    """
    if ev == "frame":
        dt = finite(rec["dt_ms"])
        if dt < 0:
            # A negative interval would silently lower duration_s (the 30 minute rule).
            raise ValueError("negative dt_ms")
        t_ms = rec.get("t_ms")
        return dt, None if t_ms is None else finite(t_ms)
    if ev == "av.offset":
        return abs(finite(rec["ms"]))
    if ev == "audio.underrun":
        return int(rec.get("n", 1))
    if ev == "warmup.end":
        return int(rec.get("warmup_ms", 0))
    if ev == "run.end":
        return int(rec.get("capture_split", 0)), int(rec.get("write_faults", 0))
    if ev == "heartbeat":
        return finite(rec["t_ms"])
    return None


def parse_telemetry(path):
    """Parse a portps5.telemetry/1 JSONL file into the metrics the results need.

    Only whitelisted fields of known events are read; unknown events and keys
    are ignored so a newer runtime stays compatible. Malformed JSON, a missing
    ``run.start`` header or a wrong schema is an error, never a silent skip.
    """
    t = {
        "dts": [],
        "softlocks": 0,
        "crashes": 0,
        "underruns": 0,
        "av_offsets": [],
        "spirv": 0,
        "pipe_after_warmup": 0,
        "warmup_ms": 0,
        "resolution": None,
        "pipeline_cache": None,
        "audio_device": "none",
        "ended": False,
        "last_t_ms": None,
        "last_seen_t_ms": None,
        "capture_split": 0,
        "write_faults": 0,
    }
    warmed = False
    header = False
    with open(path, encoding="utf-8") as fh:
        for n, line in enumerate(fh, 1):
            if not line.strip():
                continue
            try:
                rec = json.loads(line)
                ev = rec["ev"]
                # Convert every numeric field here so a malformed known event is
                # a clean "line N" error instead of a traceback later.
                val = event_value(ev, rec)
                if ev == "frame" and val[1] is not None:
                    val = (val[0], float(val[1]))
            except (ValueError, KeyError, TypeError, AttributeError, OverflowError) as exc:
                raise RegressError(f"telemetry line {n} is not a valid record") from exc
            if not header:
                if ev != "run.start" or rec.get("schema") != TELEMETRY_SCHEMA:
                    raise RegressError(f"telemetry must start with run.start {TELEMETRY_SCHEMA}")
                header = True
                t["resolution"] = rec.get("resolution")
                t["pipeline_cache"] = rec.get("pipeline_cache")
                t["audio_device"] = rec.get("audio_device", "none")
            elif ev == "frame":
                t["dts"].append(val[0])
                if val[1] is not None:
                    t["last_t_ms"] = val[1]
            elif ev == "heartbeat":
                seen = t["last_seen_t_ms"]
                t["last_seen_t_ms"] = val if seen is None else max(seen, val)
            elif ev == "softlock":
                t["softlocks"] += 1
            elif ev == "crash":
                t["crashes"] += 1
            elif ev == "audio.underrun":
                t["underruns"] += val
            elif ev == "av.offset":
                t["av_offsets"].append(val)
            elif ev == "warmup.end":
                warmed = True
                t["warmup_ms"] = val
            elif ev == "spirv.compile":
                t["spirv"] += 1
            elif ev == "pipeline.create" and warmed:
                t["pipe_after_warmup"] += 1
            elif ev == "run.end":
                t["ended"] = True
                t["capture_split"], t["write_faults"] = val
    if not header:
        raise RegressError("telemetry log is empty")
    return t


def build_results(args, tel, cfg, checks, killed_by_runner, exit_code, log_sha, wall_ms=None):
    """Assemble the portps5.results/1 dict and apply the pass rule.

    ``killed_by_runner`` marks a run the runner ended at its time limit (not a
    crash). Any other end without ``run.end`` or with a non-zero exit code is
    counted as one crash on top of the in-process crash events. ``wall_ms`` is
    the launch-to-kill time (recorded, not used for judging). For a killed run,
    more than 30 s between the last present and the runtime's last heartbeat is
    one more softlock, since no later ``frame`` record exists to carry that gap.
    """
    stats = rm.frame_stats(tel["dts"])
    crashes = tel["crashes"]
    # A watchdog abort writes softlock then run.end and exits non-zero; that is
    # one softlock, not also a crash (verification.md 4.3).
    watchdog_end = tel["softlocks"] > 0 and tel["ended"]
    if (
        not killed_by_runner
        and not watchdog_end
        and (not tel["ended"] or exit_code not in (0, None))
    ):
        crashes += 1
    # Tail hang: compare two timestamps on the runtime's own clock (the last present
    # and the last heartbeat the runtime wrote before it was killed), never the
    # runner's wall clock, which starts earlier and stops after the shutdown grace wait.
    # Skipped when the runtime wrote no heartbeats (older runtime). A title that never
    # presented is measured from run start (t_ms 0): there is no loading exemption.
    if tel["last_t_ms"] is not None:
        last_frame = tel["last_t_ms"]
    elif not tel["dts"]:
        last_frame = 0.0  # never presented: measured from run start
    else:
        last_frame = None  # frames without t_ms: no common clock, skip the tail check
    seen = tel["last_seen_t_ms"]
    tail_softlock = int(
        killed_by_runner
        and seen is not None
        and last_frame is not None
        and seen - last_frame > rm.SOFTLOCK_MS
    )
    av_max = round(max(tel["av_offsets"], default=0.0), 1)
    duration = stats["presented_s"]
    # Per-10-minute rate over the presented span; short runs do not extrapolate below 1 minute.
    per10 = round(tel["underruns"] * 600.0 / max(duration, 60.0), 2)
    fmv = [{"name": e["name"], "result": rm.fmv_result(e, av_max)} for e in checks.get("fmv", [])]
    res = {
        "schema": RESULTS_SCHEMA,
        "commit": args.commit,
        "run_type": args.run_type,
        "title": {
            "id": args.title_id,
            "region": args.region,
            "patch": args.patch,
            "name": args.name,
        },
        "host_tier": rm.host_tier(args.bench_cpu, args.bench_gpu),
        "bench": {
            "cpu_cinebench_r23_multi": args.bench_cpu,
            "gpu_timespy_graphics": args.bench_gpu,
        },
        "gpu_vendor": args.gpu_vendor,
        "driver_version": args.driver_version,
        "resolution": tel["resolution"] or args.resolution,
        "pipeline_cache": tel["pipeline_cache"] or "cold",
        "fps": {k: stats[k] for k in ("avg", "p1_low", "min", "stalls")},
        "av_offset_ms_max": av_max,
        "audio_underruns_per_10min": per10,
        "audio_device": tel["audio_device"],
        "spirv_compilations": tel["spirv"],
        "pipeline_creations_after_warmup": tel["pipe_after_warmup"],
        "warmup_ms": tel["warmup_ms"],
        "capture_split": tel["capture_split"],
        "write_faults": tel["write_faults"],
        "config_sha256": rm.config_sha256(cfg),
        "workarounds_set": rm.workarounds_set(cfg),
        "debug_keys_set": rm.debug_keys_set(cfg),
        "crashes": crashes,
        "softlocks": rm.count_softlocks(tel["dts"], tel["softlocks"]) + tail_softlock,
        "checkpoints": checks.get("checkpoints", []),
        "fmv": fmv,
        "save_roundtrip": checks.get("save_roundtrip", "not_run"),
        "log_sha256": log_sha,
        # duration_s is the span of presented frames (first to last present),
        # the quantity the 30 minute full-run rule is defined on (PRD 4.3).
        "duration_s": duration,
    }
    reasons = rm.fail_reasons(res)
    res["fail_reasons"] = reasons
    res["result"] = "fail" if reasons else "pass"
    return res


def load_checks(path):
    """Load the optional operator/frame-check inputs (checkpoints, fmv, save_roundtrip)."""
    if not path:
        return {}
    data = json.loads(Path(path).read_text(encoding="utf-8"))
    if not isinstance(data, dict):
        raise RegressError("--checks-file must hold a JSON object")
    return data


def write_results(res, out):
    """Write the results JSON (sorted, trailing newline), creating parent directories."""
    out = Path(out)
    out.parent.mkdir(parents=True, exist_ok=True)
    out.write_text(json.dumps(res, indent=2, allow_nan=False) + "\n", encoding="utf-8")


def link_app0(install, dump):
    """Link <install>/app0 to the dump (junction on Windows, symlink elsewhere), read-only use."""
    link = Path(install) / "app0"
    if link.exists() or link.is_symlink():
        return
    if os.name == "nt":
        subprocess.run(
            ["cmd", "/c", "mklink", "/J", str(link), str(dump)],
            check=True,
            stdout=subprocess.DEVNULL,
        )
    else:
        link.symlink_to(Path(dump), target_is_directory=True)


def prepare(args):
    """Convert the dump and lay out the install dir (exe, libs, sce_module, app0, config)."""
    install = Path(args.install)
    ensure_outside_repo(install)
    dump = Path(args.dump)
    elf = dump / "eboot.elf"
    if not elf.is_file():
        raise RegressError("dump has no eboot.elf")
    install.mkdir(parents=True, exist_ok=True)
    # Full conversion on purpose: --skip-sce-module makes titles crash in guest libc++.
    subprocess.run(
        [args.relinker, "--windows", str(elf), str(install / "game.exe")],
        check=True,
        stdout=subprocess.DEVNULL,
    )
    if args.libs:
        shutil.copytree(args.libs, install / "libs", dirs_exist_ok=True)
    link_app0(install, dump)


def launch(install, duration_s):
    """Run game.exe with install as cwd; return and persist the runner outcome.

    The outcome ``{"killed", "exit_code", "wall_ms"}`` is also written to
    logs/runner.json so a later ``report`` on the same install judges the run
    the same way (a timed-out run is not a crash, and a tail hang is visible).

    stdout and stderr go to files in the install dir and are never read, so no
    game text can reach the results. The run is ended at duration_s if the
    title is still running (a normal end for a timed run, not a crash).
    """
    install = Path(install)
    (install / TELEMETRY_REL).parent.mkdir(parents=True, exist_ok=True)
    (install / TELEMETRY_REL).unlink(missing_ok=True)
    env = {k: v for k, v in os.environ.items() if k != "PORTPS5_DEBUG"}
    exe = install / "game.exe"
    start = time.monotonic()
    with open(install / "stdout.txt", "wb") as out, open(install / "stderr.txt", "wb") as err:
        proc = subprocess.Popen([str(exe)], cwd=install, stdout=out, stderr=err, env=env)
        try:
            proc.wait(timeout=duration_s)
            killed, code = False, proc.returncode
        except subprocess.TimeoutExpired:
            proc.terminate()
            try:
                proc.wait(timeout=30)
            except subprocess.TimeoutExpired:
                proc.kill()
                proc.wait()
            killed, code = True, None
    outcome = {"killed": killed, "exit_code": code, "wall_ms": (time.monotonic() - start) * 1000.0}
    (install / RUNNER_REL).write_text(json.dumps(outcome), encoding="utf-8")
    return outcome


def load_runner(install):
    """Read the persisted runner outcome, or a clean-exit default when there is none."""
    path = Path(install) / RUNNER_REL
    if not path.is_file():
        return {"killed": False, "exit_code": None, "wall_ms": None}
    data = json.loads(path.read_text(encoding="utf-8"))
    # report reads this file independently of run, so it may be stale or hand-edited.
    if not isinstance(data, dict) or "killed" not in data or "exit_code" not in data:
        raise RegressError(f"{RUNNER_REL} is not a valid runner outcome")
    return data


def report(args, runner=None):
    """Build and write the results JSON from <install>/logs/telemetry.jsonl.

    ``runner`` overrides the outcome persisted by ``run`` (logs/runner.json).
    """
    install = Path(args.install)
    log = install / TELEMETRY_REL
    if not log.is_file():
        raise RegressError(f"no telemetry log at {TELEMETRY_REL} (runtime telemetry is required)")
    tel = parse_telemetry(log)
    cfg = rm.load_resolved_config(install / "config", args.title_id)
    runner = runner if runner is not None else load_runner(install)
    res = build_results(
        args,
        tel,
        cfg,
        load_checks(args.checks_file),
        runner["killed"],
        runner["exit_code"],
        sha256_file(log),
        runner.get("wall_ms"),
    )
    out = (
        args.out
        or ROOT / "compat" / "results" / args.title_id / f"{args.commit}-{args.run_type}.json"
    )
    write_results(res, out)
    return res


def add_common(p):
    """Add the arguments shared by run and report."""
    p.add_argument("--install", required=True, help="install dir (outside the repo)")
    p.add_argument("--title-id", required=True)
    p.add_argument("--region", required=True)
    p.add_argument("--patch", required=True)
    p.add_argument("--name", required=True)
    p.add_argument("--commit", required=True, help="git sha of the build under test")
    p.add_argument("--run-type", choices=["regression", "full_run"], default="regression")
    p.add_argument("--gpu-vendor", choices=["nvidia", "amd", "intel"], required=True)
    p.add_argument("--driver-version", required=True)
    p.add_argument("--bench-cpu", type=int, required=True, help="Cinebench R23 multi score")
    p.add_argument("--bench-gpu", type=int, required=True, help="Time Spy graphics score")
    p.add_argument("--resolution", default="1920x1080")
    p.add_argument("--checks-file", help="JSON with checkpoints, fmv and save_roundtrip")
    p.add_argument("--out", help="results JSON path (default compat/results/...)")


def main(argv=None):
    """CLI entry point; returns the process exit code (1 on a failed run or bad input)."""
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    sub = ap.add_subparsers(dest="cmd", required=True)
    p = sub.add_parser("prepare")
    p.add_argument("--dump", required=True)
    p.add_argument("--install", required=True)
    p.add_argument("--relinker", required=True)
    p.add_argument("--libs", help="build/release/core/libs/libs")
    p = sub.add_parser("run")
    add_common(p)
    p.add_argument("--duration-s", type=float, required=True)
    p = sub.add_parser("report")
    add_common(p)
    args = ap.parse_args(argv)
    try:
        if args.cmd == "prepare":
            prepare(args)
            return 0
        ensure_outside_repo(args.install)
        runner = None
        if args.cmd == "run":
            if os.environ.get("PORTPS5_DEBUG"):
                raise RegressError("unset PORTPS5_DEBUG: debug keys must come from config files")
            runner = launch(args.install, args.duration_s)
        res = report(args, runner)
    except (RegressError, OSError, ValueError, KeyError, subprocess.CalledProcessError) as exc:
        print(f"regress: {exc}", file=sys.stderr)
        return 1
    print(f"result={res['result']} fail_reasons={res['fail_reasons']}")
    return 0 if res["result"] == "pass" else 1


if __name__ == "__main__":
    sys.exit(main())
