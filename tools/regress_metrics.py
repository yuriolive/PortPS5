"""Pure metric, hashing and pass-rule helpers for tools/regress.py.

Everything here is deterministic and free of I/O and game data, so hosted CI
can test it with synthetic frame times. Definitions follow docs/PRD.md 4.3 and
docs/spec/verification.md section 4:

* a stall is a gap of more than 1 s between presents; stalls are excluded from
  the fps statistics and counted separately;
* a softlock is no present for more than 30 s (the watchdog reports the guest
  thread half of the rule as a ``softlock`` event);
* 1% low = 1000 / mean of the slowest 1% of non-stall frame times.
"""

import hashlib
import json
import math
import tomllib
from pathlib import Path

STALL_MS = 1000.0
SOFTLOCK_MS = 30000.0
MIN_FULL_RUN_S = 30 * 60
MIN_AVG_FPS = 30.0
MIN_P1_LOW_FPS = 20.0
FMV_MIN_FRAME_RATIO = 0.9
FMV_MAX_AV_OFFSET_MS = 80.0
MIN_WIDTH, MIN_HEIGHT = 1920, 1080
BENCH_FLOOR_CPU = 18000
BENCH_FLOOR_GPU = 18000


def frame_stats(dts_ms):
    """Compute fps statistics from presented-frame intervals in milliseconds.

    Intervals above STALL_MS are counted as stalls and excluded from the
    statistics. Returns a dict with ``avg``, ``p1_low``, ``min``, ``stalls``
    and the internal ``frames`` and ``presented_s`` used by the pass rule.
    ``avg``, ``p1_low`` and ``min`` are 0.0 when no usable frame exists.
    """
    stalls = sum(1 for d in dts_ms if d > STALL_MS)
    good = sorted((d for d in dts_ms if 0 < d <= STALL_MS), reverse=True)
    out = {
        "avg": 0.0,
        "p1_low": 0.0,
        "min": 0.0,
        "stalls": stalls,
        "frames": len(good),
        "presented_s": round(sum(dts_ms) / 1000.0, 3),
    }
    if not good:
        return out
    out["avg"] = round(1000.0 * len(good) / sum(good), 2)
    # Slowest 1% is at least one frame so short runs still get a defined value.
    worst = good[: max(1, math.ceil(len(good) * 0.01))]
    out["p1_low"] = round(1000.0 / (sum(worst) / len(worst)), 2)
    out["min"] = round(1000.0 / good[0], 2)
    return out


def count_softlocks(dts_ms, reported):
    """Return the softlock count: watchdog reports plus present gaps over SOFTLOCK_MS.

    A gap above the limit is also a softlock if the watchdog missed it, so the
    runner never trusts the in-process watchdog alone. A gap the watchdog
    already reported is not double counted (max of the two sources).
    """
    return max(reported, sum(1 for d in dts_ms if d > SOFTLOCK_MS))


def merge_config(base, over):
    """Deep-merge TOML tables key by key, later layers winning (configuration.md layering)."""
    merged = dict(base)
    for key, value in over.items():
        if isinstance(value, dict) and isinstance(merged.get(key), dict):
            merged[key] = merge_config(merged[key], value)
        else:
            merged[key] = value
    return merged


def load_resolved_config(config_dir, title_id):
    """Load global.toml then games/<title_id>.toml from config_dir and merge them.

    Missing files are treated as empty, matching the runtime's default-with-
    warning behaviour. PORTPS5_DEBUG is not a layer here: the runner refuses
    to run with it set so the result is reproducible from the files alone.
    """
    cfg = {}
    for path in (Path(config_dir) / "global.toml", Path(config_dir) / "games" / f"{title_id}.toml"):
        if path.is_file():
            cfg = merge_config(cfg, tomllib.loads(path.read_text(encoding="utf-8")))
    return cfg


def config_sha256(cfg):
    """Hash the resolved config as canonical JSON (sorted keys, no whitespace)."""
    blob = json.dumps(cfg, sort_keys=True, separators=(",", ":"), ensure_ascii=True)
    return hashlib.sha256(blob.encode("utf-8")).hexdigest()


def workarounds_set(cfg):
    """List the names of enabled [workarounds] keys (false, 0 and empty values are off)."""
    return sorted(k for k, v in cfg.get("workarounds", {}).items() if v not in (False, 0, "", []))


def debug_keys_set(cfg):
    """List every [debug] key present in the resolved config as dotted names, sorted."""
    keys = []

    def walk(prefix, table):
        for key, value in table.items():
            if isinstance(value, dict):
                walk(f"{prefix}{key}.", value)
            else:
                keys.append(f"{prefix}{key}")

    walk("", cfg.get("debug", {}))
    return sorted(keys)


def host_tier(cpu_score, gpu_score):
    """Derive host_tier from the two benchmark scores (PRD 4.4), never from a hand-typed name."""
    if cpu_score >= BENCH_FLOOR_CPU and gpu_score >= BENCH_FLOOR_GPU:
        return "upper-mid-tier (derived from bench scores)"
    return "below-reference-tier (derived from bench scores)"


def fmv_result(entry, av_offset_max):
    """Apply the "FMV played" rule (video-fmv.md) to one FMV check entry.

    ``entry`` has ``first_ref_ok``, ``end_ref_ok`` (SSIM checks already done by
    the frame-check step), ``presented_frames`` and ``reference_frames``.
    Returns "pass" only if both references matched, at least 90% of the
    reference frame count was presented and the A/V offset stayed within 80 ms.
    Reaching the post-FMV state without the end reference therefore fails.
    """
    ref = entry.get("reference_frames", 0)
    ok = (
        entry.get("first_ref_ok") is True
        and entry.get("end_ref_ok") is True
        and ref > 0
        and entry.get("presented_frames", 0) >= FMV_MIN_FRAME_RATIO * ref
        and av_offset_max <= FMV_MAX_AV_OFFSET_MS
    )
    return "pass" if ok else "fail"


def parse_resolution(text):
    """Parse "WxH" into an int pair, or return None when malformed."""
    try:
        w, h = text.lower().split("x")
        return int(w), int(h)
    except (ValueError, AttributeError):
        return None


def fail_reasons(res):
    """Return the sorted names of every failed pass-rule field in a results dict.

    Field names only, never values from the run, so the list is safe to publish.
    Implements the section 4 pass rule plus the PRD 4.3 bar for full runs.
    """
    bad = []
    if res["debug_keys_set"]:
        bad.append("debug_keys_set")
    if res["pipeline_cache"] == "warm" and res["spirv_compilations"] != 0:
        bad.append("spirv_compilations")
    if res["pipeline_cache"] == "warm" and res["pipeline_creations_after_warmup"] != 0:
        bad.append("pipeline_creations_after_warmup")
    if res["crashes"]:
        bad.append("crashes")
    if res["softlocks"]:
        bad.append("softlocks")
    if any(c.get("result") != "pass" for c in res["checkpoints"]):
        bad.append("checkpoints")
    if any(f.get("result") != "pass" for f in res.get("fmv", [])):
        bad.append("fmv")
    if res["run_type"] == "full_run":
        fps = res["fps"]
        res_wh = parse_resolution(res["resolution"])
        if fps["avg"] < MIN_AVG_FPS:
            bad.append("fps.avg")
        if fps["p1_low"] < MIN_P1_LOW_FPS:
            bad.append("fps.p1_low")
        if res["duration_s"] < MIN_FULL_RUN_S:
            bad.append("duration_s")
        if res_wh is None or res_wh[0] < MIN_WIDTH or res_wh[1] < MIN_HEIGHT:
            bad.append("resolution")
        if res["save_roundtrip"] != "pass":
            bad.append("save_roundtrip")
        if res["pipeline_cache"] != "warm":
            bad.append("pipeline_cache")
    elif res["save_roundtrip"] == "fail":
        bad.append("save_roundtrip")
    return sorted(bad)
