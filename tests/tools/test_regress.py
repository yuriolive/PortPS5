"""Tests for tools/regress.py and tools/regress_metrics.py.

Synthetic frame times, configs and telemetry only; the "game" is a tiny Python
script, so no dump, executable or GPU is involved. Each test names the rule it
pins (PRD 4.3 definitions, verification.md section 4 pass rule).
"""

import json
import os
import sys
from pathlib import Path

import pytest

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "tools"))

import regress
import regress_metrics as rm

HEADER = {
    "ev": "run.start",
    "schema": "portps5.telemetry/1",
    "resolution": "1920x1080",
    "pipeline_cache": "warm",
    "audio_device": "wasapi-default",
}


def write_log(path, records):
    """Write telemetry records as JSONL, creating parent dirs."""
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text("".join(json.dumps(r) + "\n" for r in records), encoding="utf-8")


def args_for(install, **kw):
    """Build a Namespace like the CLI would, with overridable fields."""
    import argparse

    base = {
        "install": str(install), "title_id": "PPSA00000", "region": "US", "patch": "01.000.000",
        "name": "Synthetic", "commit": "abc123", "run_type": "regression", "gpu_vendor": "amd",
        "driver_version": "1", "bench_cpu": 20000, "bench_gpu": 20000, "resolution": "1920x1080",
        "checks_file": None, "out": str(Path(install) / "r.json"),
    }  # fmt: skip
    base.update(kw)
    return argparse.Namespace(**base)


def test_frame_stats_excludes_stalls_and_counts_them():
    # 100 frames at 20 ms plus one 1.5 s stall: stall counted, not in avg.
    s = rm.frame_stats([20.0] * 100 + [1500.0])
    assert s["stalls"] == 1 and s["avg"] == 50.0 and s["frames"] == 100


def test_stall_boundary_is_strictly_greater_than_one_second():
    assert rm.frame_stats([1000.0, 1000.1])["stalls"] == 1


def test_one_percent_low_uses_slowest_one_percent_mean():
    # 99 x 10 ms + 1 x 100 ms: slowest 1% = the 100 ms frame -> 10 fps.
    s = rm.frame_stats([10.0] * 99 + [100.0])
    assert s["p1_low"] == 10.0 and s["min"] == 10.0


def test_one_percent_low_rounds_up_to_whole_frames():
    # 150 frames -> ceil(1.5) = 2 slowest frames averaged: (100+50)/2 = 75 ms.
    s = rm.frame_stats([10.0] * 148 + [100.0, 50.0])
    assert s["p1_low"] == round(1000 / 75, 2)


def test_frame_stats_empty_and_all_stalls():
    assert rm.frame_stats([])["avg"] == 0.0
    assert rm.frame_stats([2000.0])["avg"] == 0.0


def test_softlock_from_gap_even_if_watchdog_silent():
    assert rm.count_softlocks([16.0, 30001.0], 0) == 1
    assert rm.count_softlocks([16.0, 30000.0], 0) == 0
    assert rm.count_softlocks([30001.0], 1) == 1  # not double counted
    assert rm.count_softlocks([16.0], 2) == 2  # guest-thread hang with presents flowing


def test_config_hash_layering_and_stability(tmp_path):
    (tmp_path / "games").mkdir()
    (tmp_path / "global.toml").write_text("schema=1\n[display]\nfullscreen=false\nscale=1\n")
    (tmp_path / "games" / "T.toml").write_text(
        "[display]\nfullscreen=true\n[workarounds]\nfoo=true\nbar=false\n"
    )
    cfg = rm.load_resolved_config(tmp_path, "T")
    assert cfg["display"] == {"fullscreen": True, "scale": 1}
    assert rm.workarounds_set(cfg) == ["foo"]
    assert rm.config_sha256(cfg) == rm.config_sha256(json.loads(json.dumps(cfg)))
    assert rm.config_sha256({"a": 1, "b": 2}) == rm.config_sha256({"b": 2, "a": 1})
    assert rm.config_sha256({"a": 1}) != rm.config_sha256({"a": 2})


def test_missing_config_files_resolve_empty(tmp_path):
    assert rm.load_resolved_config(tmp_path, "X") == {}


def test_debug_keys_flattened():
    cfg = {"debug": {"profile": ["gpu"], "gpu": {"trace": True}}}
    assert rm.debug_keys_set(cfg) == ["gpu.trace", "profile"]
    assert rm.debug_keys_set({}) == []


def test_host_tier_from_scores_only():
    assert rm.host_tier(18000, 18000).startswith("upper-mid-tier")
    assert rm.host_tier(17999, 99999).startswith("below")


def test_fmv_rule():
    ok = {"first_ref_ok": True, "end_ref_ok": True, "presented_frames": 90, "reference_frames": 100}
    assert rm.fmv_result(ok, 80.0) == "pass"
    assert rm.fmv_result(ok, 80.1) == "fail"
    assert rm.fmv_result({**ok, "presented_frames": 89}, 0) == "fail"
    # A skipped FMV reaches the next state without the end reference.
    assert rm.fmv_result({**ok, "end_ref_ok": False}, 0) == "fail"
    assert rm.fmv_result({}, 0) == "fail"


def good_results(**kw):
    """A passing full-run results dict, overridable per test."""
    res = {
        "debug_keys_set": [], "pipeline_cache": "warm", "spirv_compilations": 0,
        "pipeline_creations_after_warmup": 0, "crashes": 0, "softlocks": 0, "checkpoints": [],
        "run_type": "full_run", "fps": {"avg": 30.0, "p1_low": 20.0}, "resolution": "1920x1080",
        "duration_s": 1800, "save_roundtrip": "pass",
    }  # fmt: skip
    res.update(kw)
    return res


@pytest.mark.parametrize(
    ("change", "reason"),
    [
        ({"debug_keys_set": ["profile"]}, "debug_keys_set"),
        ({"spirv_compilations": 1}, "spirv_compilations"),
        ({"pipeline_creations_after_warmup": 1}, "pipeline_creations_after_warmup"),
        ({"crashes": 1}, "crashes"),
        ({"softlocks": 1}, "softlocks"),
        ({"checkpoints": [{"result": "fail"}]}, "checkpoints"),
        ({"fmv": [{"result": "fail"}]}, "fmv"),
        ({"fps": {"avg": 29.9, "p1_low": 20.0}}, "fps.avg"),
        ({"fps": {"avg": 30.0, "p1_low": 19.9}}, "fps.p1_low"),
        ({"duration_s": 1799}, "duration_s"),
        ({"resolution": "1280x720"}, "resolution"),
        ({"resolution": "bogus"}, "resolution"),
        ({"save_roundtrip": "not_run"}, "save_roundtrip"),
        ({"pipeline_cache": "cold"}, "pipeline_cache"),
    ],
)
def test_each_pass_rule_fails_independently(change, reason):
    assert rm.fail_reasons(good_results()) == []
    assert reason in rm.fail_reasons(good_results(**change))


def test_cold_cache_regression_ignores_compile_counters():
    r = good_results(run_type="regression", pipeline_cache="cold", spirv_compilations=9)
    assert rm.fail_reasons(r) == []


def test_parse_telemetry_requires_header_and_schema(tmp_path):
    log = tmp_path / "t.jsonl"
    write_log(log, [{"ev": "frame", "dt_ms": 16}])
    with pytest.raises(regress.RegressError):
        regress.parse_telemetry(log)
    write_log(log, [{**HEADER, "schema": "other/9"}])
    with pytest.raises(regress.RegressError):
        regress.parse_telemetry(log)
    log.write_text("not json\n")
    with pytest.raises(regress.RegressError):
        regress.parse_telemetry(log)
    log.write_text("")
    with pytest.raises(regress.RegressError):
        regress.parse_telemetry(log)


def test_parse_telemetry_counts_and_ignores_unknown(tmp_path):
    log = tmp_path / "t.jsonl"
    write_log(log, [
        HEADER, {"ev": "pipeline.create"},  # before warm-up: not counted
        {"ev": "warmup.end", "warmup_ms": 4200}, {"ev": "pipeline.create"},
        {"ev": "frame", "dt_ms": 16.0}, {"ev": "audio.underrun", "n": 2}, {"ev": "av.offset", "ms": -42},
        {"ev": "spirv.compile"}, {"ev": "crash"}, {"ev": "softlock"}, {"ev": "future.thing", "x": 1},
        {"ev": "run.end", "capture_split": 12, "write_faults": 7}, {},
    ])  # fmt: skip
    # The trailing {} has no "ev": that is a malformed record, not ignorable.
    with pytest.raises(regress.RegressError):
        regress.parse_telemetry(log)
    write_log(log, [json.loads(x) for x in log.read_text().splitlines()[:-1]])
    t = regress.parse_telemetry(log)
    assert (t["pipe_after_warmup"], t["underruns"], t["av_offsets"], t["spirv"]) == (
        1,
        2,
        [42.0],
        1,
    )
    assert (t["crashes"], t["softlocks"], t["warmup_ms"], t["write_faults"]) == (1, 1, 4200, 7)
    assert t["ended"] and t["capture_split"] == 12


def make_install(tmp_path, records, games=""):
    """Create an install dir with a telemetry log and optional game config."""
    inst = tmp_path / "inst"
    write_log(inst / regress.TELEMETRY_REL, records)
    (inst / "config" / "games").mkdir(parents=True)
    (inst / "config" / "games" / "PPSA00000.toml").write_text(games)
    return inst


def test_report_end_to_end_passes_and_hides_game_text(tmp_path):
    frames = [{"ev": "frame", "dt_ms": 16.0}] * 120
    inst = make_install(tmp_path, [HEADER, *frames, {"ev": "run.end"}])
    (inst / "stdout.txt").write_text("SECRET GAME TEXT")
    res = regress.report(args_for(inst), {"killed": False, "exit_code": 0})
    assert res["result"] == "pass" and res["fps"]["avg"] == 62.5 and res["crashes"] == 0
    blob = (inst / "r.json").read_text()
    assert "SECRET" not in blob and res["log_sha256"] == regress.sha256_file(
        inst / regress.TELEMETRY_REL
    )
    assert res["schema"] == "portps5.results/1" and res["save_roundtrip"] == "not_run"


def test_report_flags_debug_key_and_workaround(tmp_path):
    inst = make_install(
        tmp_path, [HEADER, {"ev": "run.end"}], "[debug]\nprofile=['gpu']\n[workarounds]\nx=true\n"
    )
    res = regress.report(args_for(inst), {"killed": False, "exit_code": 0})
    assert res["result"] == "fail" and "debug_keys_set" in res["fail_reasons"]
    assert res["debug_keys_set"] == ["profile"] and res["workarounds_set"] == ["x"]


def test_crash_without_run_end_counts_unless_runner_killed(tmp_path):
    inst = make_install(tmp_path, [HEADER, {"ev": "frame", "dt_ms": 16}])
    assert regress.report(args_for(inst), {"killed": False, "exit_code": 0})["crashes"] == 1
    assert regress.report(args_for(inst), {"killed": True, "exit_code": None})["crashes"] == 0
    inst2 = make_install(tmp_path / "b", [HEADER, {"ev": "run.end"}])
    assert regress.report(args_for(inst2), {"killed": False, "exit_code": 3})["crashes"] == 1


def test_checks_file_drives_checkpoints_fmv_and_save(tmp_path):
    inst = make_install(tmp_path, [HEADER, {"ev": "av.offset", "ms": 10}, {"ev": "run.end"}])
    checks = tmp_path / "c.json"
    fmv = {
        "name": "intro",
        "first_ref_ok": True,
        "end_ref_ok": True,
        "presented_frames": 95,
        "reference_frames": 100,
    }
    checks.write_text(
        json.dumps(
            {
                "save_roundtrip": "pass",
                "checkpoints": [{"name": "a", "result": "pass"}],
                "fmv": [fmv],
            }
        )
    )
    res = regress.report(args_for(inst, checks_file=str(checks)), {"killed": False, "exit_code": 0})
    assert res["fmv"] == [{"name": "intro", "result": "pass"}] and res["save_roundtrip"] == "pass"
    checks.write_text("[]")
    with pytest.raises(regress.RegressError):
        regress.load_checks(checks)
    assert regress.load_checks(None) == {}


def test_report_without_telemetry_is_an_error(tmp_path):
    with pytest.raises(regress.RegressError):
        regress.report(args_for(tmp_path))


def test_paths_inside_repo_are_refused():
    with pytest.raises(regress.RegressError):
        regress.ensure_outside_repo(regress.ROOT / "tools")
    regress.ensure_outside_repo(Path("/somewhere/else"))


def fake_exe(inst, body):
    """Create install/game.exe as an executable Python script (POSIX test double)."""
    exe = inst / "game.exe"
    exe.write_text(f"#!{sys.executable}\n{body}")
    exe.chmod(0o755)


@pytest.mark.skipif(os.name == "nt", reason="uses a POSIX shebang script as the fake title")
def test_launch_uses_install_cwd_strips_debug_env_and_discards_output(tmp_path, monkeypatch):
    inst = tmp_path / "inst"
    inst.mkdir()
    fake_exe(inst, "import os,json\nprint('SECRET')\nos.makedirs('logs',exist_ok=True)\n"
        "open('logs/telemetry.jsonl','w').write(json.dumps({'ev':'run.start','schema':'portps5.telemetry/1',"
        "'dbg':os.environ.get('PORTPS5_DEBUG','')})+'\\n'+json.dumps({'ev':'run.end'})+'\\n')\n")  # fmt: skip
    monkeypatch.setenv("PORTPS5_DEBUG", "trace=audio")
    out = regress.launch(inst, 30)
    assert (out["killed"], out["exit_code"]) == (False, 0)
    assert '"dbg": ""' in (inst / regress.TELEMETRY_REL).read_text()


@pytest.mark.skipif(os.name == "nt", reason="uses a POSIX shebang script as the fake title")
def test_launch_ends_a_running_title_at_the_time_limit(tmp_path):
    inst = tmp_path / "inst"
    inst.mkdir()
    fake_exe(inst, "import time\ntime.sleep(60)\n")
    out = regress.launch(inst, 0.5)
    assert (out["killed"], out["exit_code"]) == (True, None)
    assert json.loads((inst / regress.RUNNER_REL).read_text())["killed"] is True


@pytest.mark.skipif(os.name == "nt", reason="uses a POSIX shebang script as the fake relinker")
def test_prepare_converts_fully_links_app0_and_never_copies_dump(tmp_path):
    dump = tmp_path / "dump dir [x]"
    dump.mkdir()
    (dump / "eboot.elf").write_text("elf")
    libs = tmp_path / "libs"
    libs.mkdir()
    (libs / "a.prx").write_text("p")
    rel = tmp_path / "relinker"
    rel.write_text(f"#!{sys.executable}\nimport sys\nassert '--skip-sce-module' not in sys.argv\n"
        "assert sys.argv[1]=='--windows'\nopen(sys.argv[3],'w').write('exe')\n")  # fmt: skip
    rel.chmod(0o755)
    inst = tmp_path / "out"
    a = [
        "prepare",
        "--dump",
        str(dump),
        "--install",
        str(inst),
        "--relinker",
        str(rel),
        "--libs",
        str(libs),
    ]
    assert regress.main(a) == 0
    assert (inst / "game.exe").is_file() and (inst / "libs" / "a.prx").is_file()
    assert (inst / "app0").is_symlink() and not (inst / "eboot.elf").exists()
    assert regress.main(a) == 0  # idempotent
    (dump / "eboot.elf").unlink()
    assert regress.main(a) == 1  # no eboot.elf


def test_main_report_exit_codes(tmp_path, capsys):
    inst = make_install(tmp_path, [HEADER, {"ev": "run.end"}])
    out = tmp_path / "o.json"
    cli = ["report", "--install", str(inst), "--title-id", "PPSA00000", "--region", "US", "--patch", "1",
        "--name", "S", "--commit", "c", "--gpu-vendor", "amd", "--driver-version", "1",
        "--bench-cpu", "1", "--bench-gpu", "1", "--out", str(out)]  # fmt: skip
    assert regress.main(cli) == 0
    assert "result=pass" in capsys.readouterr().out
    (inst / regress.TELEMETRY_REL).unlink()
    assert regress.main(cli) == 1
    assert regress.main(["report", *cli[1:], "--install", str(regress.ROOT / "x")]) == 1


def test_run_refuses_portps5_debug(tmp_path, monkeypatch):
    monkeypatch.setenv("PORTPS5_DEBUG", "trace=audio")
    cli = ["run", "--install", str(tmp_path), "--title-id", "T", "--region", "US", "--patch", "1",
        "--name", "S", "--commit", "c", "--gpu-vendor", "amd", "--driver-version", "1",
        "--bench-cpu", "1", "--bench-gpu", "1", "--duration-s", "1"]  # fmt: skip
    assert regress.main(cli) == 1


def test_watchdog_abort_is_a_softlock_not_also_a_crash(tmp_path):
    # Regression for the telemetry watchdog: softlock + run.end + non-zero exit.
    inst = make_install(tmp_path, [HEADER, {"ev": "softlock", "idle_ms": 31000}, {"ev": "run.end"}])
    res = regress.report(args_for(inst), {"killed": False, "exit_code": 1})
    assert res["softlocks"] == 1 and res["crashes"] == 0 and res["result"] == "fail"


def test_malformed_known_event_fields_are_clean_errors_with_line_number(tmp_path):
    # Regression: a frame without dt_ms, a null underrun count and a null av offset
    # used to raise KeyError/TypeError tracebacks instead of RegressError.
    log = tmp_path / "t.jsonl"
    for bad in (
        {"ev": "frame"},
        {"ev": "audio.underrun", "n": None},
        {"ev": "av.offset", "ms": None},
    ):
        write_log(log, [HEADER, bad])
        with pytest.raises(regress.RegressError, match="line 2"):
            regress.parse_telemetry(log)


def test_hang_before_the_timeout_kill_is_a_softlock(tmp_path):
    # Regression: no frame record follows the last present, so only the runtime's own
    # heartbeat clock reveals a hang the watchdog missed. Runner wall time is ignored.
    frames = [{"ev": "frame", "dt_ms": 16, "t_ms": 5000}]
    killed = {"killed": True, "exit_code": None, "wall_ms": 999999.0}
    hung = make_install(tmp_path / "a", [HEADER, *frames, {"ev": "heartbeat", "t_ms": 35001}])
    res = regress.report(args_for(hung), killed)
    assert res["softlocks"] == 1 and res["crashes"] == 0 and "softlocks" in res["fail_reasons"]
    ok = make_install(tmp_path / "b", [HEADER, *frames, {"ev": "heartbeat", "t_ms": 35000}])
    assert regress.report(args_for(ok), killed)["softlocks"] == 0  # exactly 30 s is fine


def test_tail_check_ignores_runner_clock_and_missing_heartbeats(tmp_path):
    # Regression (review): start-up delay and shutdown grace in wall time must not create a
    # softlock, and an older runtime without heartbeats gets no tail check at all.
    frames = [{"ev": "frame", "dt_ms": 16, "t_ms": 5000}]
    killed = {"killed": True, "exit_code": None, "wall_ms": 999999.0}
    inst = make_install(tmp_path, [HEADER, *frames, {"ev": "heartbeat", "t_ms": 6000}])
    assert regress.report(args_for(inst), killed)["softlocks"] == 0
    old = make_install(tmp_path / "old", [HEADER, *frames])
    assert regress.report(args_for(old), killed)["softlocks"] == 0


def test_title_that_never_presents_is_a_softlock_when_killed(tmp_path):
    # No loading-screen exemption (PRD 4.3): 31 s of heartbeats and no present is a softlock.
    inst = make_install(tmp_path, [HEADER, {"ev": "heartbeat", "t_ms": 31000}])
    killed = {"killed": True, "exit_code": None, "wall_ms": 1.0}
    assert regress.report(args_for(inst), killed)["softlocks"] == 1


def test_report_reuses_the_persisted_runner_outcome(tmp_path):
    # Regression: re-reporting a timed-out run must not record a phantom crash.
    inst = make_install(tmp_path, [HEADER, {"ev": "frame", "dt_ms": 16}])
    (inst / regress.RUNNER_REL).write_text(
        json.dumps({"killed": True, "exit_code": None, "wall_ms": 100})
    )
    assert regress.report(args_for(inst))["crashes"] == 0
    (inst / regress.RUNNER_REL).unlink()
    assert regress.report(args_for(inst))["crashes"] == 1
