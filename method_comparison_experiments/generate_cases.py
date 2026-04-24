from __future__ import annotations

from pathlib import Path
from typing import List, Optional
import json
import random
import copy

from schema import Scenario, Target
from ztf_loader import build_target_pool_from_real_alerts


def _save_case(out_dir: Path, scenario: Scenario) -> None:
    out_dir.mkdir(parents=True, exist_ok=True)
    fp = out_dir / f"{scenario.scenario_id}.json"
    with open(fp, "w", encoding="utf-8") as f:
        json.dump(scenario.to_dict(), f, ensure_ascii=False, indent=2)


def _clone_target(base: Target) -> Target:
    return copy.deepcopy(base)


def _sample_targets(pool: List[Target], n: int, rng: random.Random) -> List[Target]:
    if len(pool) < n:
        raise RuntimeError(f"Target pool too small: need {n}, have {len(pool)}")
    idxs = rng.sample(range(len(pool)), n)
    return [_clone_target(pool[i]) for i in idxs]


def _set_target(
    base: Target,
    *,
    priority: Optional[int] = None,
    target_type: Optional[str] = None,
    window_start: Optional[int] = None,
    window_end: Optional[int] = None,
    duration: Optional[int] = None,
    visible: Optional[bool] = None,
    filter_required: Optional[str] = None,
    slew_cost: Optional[float] = None,
    settling_cost: Optional[float] = None,
    sensitivity_to_moon: Optional[float] = None,
) -> Target:
    t = _clone_target(base)
    if priority is not None:
        t.priority = priority
    if target_type is not None:
        t.target_type = target_type
    if window_start is not None:
        t.window_start = window_start
    if window_end is not None:
        t.window_end = window_end
    if duration is not None:
        t.duration = duration
    if visible is not None:
        t.visible = visible
    if filter_required is not None:
        t.filter_required = filter_required
    if slew_cost is not None:
        t.slew_cost = slew_cost
    if settling_cost is not None:
        t.settling_cost = settling_cost
    if sensitivity_to_moon is not None:
        t.sensitivity_to_moon = sensitivity_to_moon
    return t


def gen_s1(i: int, pool: List[Target], rng: random.Random) -> Scenario:
    transient_base, current_plan_base = _sample_targets(pool, 2, rng)

    high_priority_transient = _set_target(
        transient_base,
        priority=3,
        target_type="too",
        window_start=100,
        window_end=rng.choice([150, 160, 170]),
        duration=15,
        sensitivity_to_moon=max(transient_base.sensitivity_to_moon, 0.6),
    )

    current_scheduled_target = _set_target(
        current_plan_base,
        priority=rng.choice([1, 2]),
        target_type="routine",
        window_start=60,
        window_end=260,
        duration=25,
        sensitivity_to_moon=min(current_plan_base.sensitivity_to_moon, 0.4),
    )

    return Scenario(
        scenario_id=f"S1_{i:03d}",
        scenario_type="S1",
        instrument_status="normal",
        cloud=rng.choice([0.10, 0.20, 0.25]),
        current_plan_target=current_scheduled_target.id,
        current_filter=current_scheduled_target.filter_required or "r",
        current_plan_progress=rng.choice([0.10, 0.20, 0.25]),
        moon_phase=rng.choice([0.05, 0.15, 0.25]),
        ambiguous_text=(
            "A newly validated high-priority transient candidate should be considered "
            "for immediate follow-up."
        ),
        targets=[high_priority_transient, current_scheduled_target],
    )


def gen_s2(i: int, pool: List[Target], rng: random.Random) -> Scenario:
    low_priority_base, stronger_plan_base = _sample_targets(pool, 2, rng)

    low_priority_transient = _set_target(
        low_priority_base,
        priority=1,
        target_type="too",
        window_start=100,
        window_end=220,
        duration=20,
        sensitivity_to_moon=max(low_priority_base.sensitivity_to_moon, 0.5),
    )

    stronger_current_plan = _set_target(
        stronger_plan_base,
        priority=3,
        target_type="routine",
        window_start=80,
        window_end=270,
        duration=25,
        sensitivity_to_moon=min(stronger_plan_base.sensitivity_to_moon, 0.4),
    )

    return Scenario(
        scenario_id=f"S2_{i:03d}",
        scenario_type="S2",
        instrument_status="normal",
        cloud=rng.choice([0.10, 0.20]),
        current_plan_target=stronger_current_plan.id,
        current_filter=stronger_current_plan.filter_required or "r",
        current_plan_progress=rng.choice([0.65, 0.75, 0.85]),
        moon_phase=rng.choice([0.10, 0.25, 0.40]),
        ambiguous_text=(
            "A low-priority transient alert is present, but the currently scheduled "
            "target is more important and already partially executed."
        ),
        targets=[low_priority_transient, stronger_current_plan],
    )


def gen_s3(i: int, pool: List[Target], rng: random.Random) -> Scenario:
    base_a, base_b, base_c = _sample_targets(pool, 3, rng)

    current_filter = rng.choice(["g", "r", "i"])
    moon_phase = rng.choice([0.15, 0.35, 0.55, 0.75])

    # two high-priority candidates with different trade-offs
    competing_a = _set_target(
        base_a,
        priority=3,
        target_type="too",
        window_start=100,
        window_end=155,
        duration=18,
        filter_required=current_filter,
        slew_cost=0.10,
        settling_cost=0.05,
        sensitivity_to_moon=max(base_a.sensitivity_to_moon, 0.45),
    )

    competing_b = _set_target(
        base_b,
        priority=3,
        target_type="too",
        window_start=98,
        window_end=175,
        duration=15,
        filter_required=rng.choice([f for f in ["g", "r", "i"] if f != current_filter]),
        slew_cost=0.45,
        settling_cost=0.18,
        sensitivity_to_moon=max(base_b.sensitivity_to_moon, 0.75),
    )

    supporting_c = _set_target(
        base_c,
        priority=2,
        target_type="too",
        window_start=95,
        window_end=185,
        duration=20,
        filter_required=current_filter,
        slew_cost=0.08,
        settling_cost=0.04,
        sensitivity_to_moon=max(base_c.sensitivity_to_moon, 0.35),
    )

    return Scenario(
        scenario_id=f"S3_{i:03d}",
        scenario_type="S3",
        instrument_status="normal",
        cloud=rng.choice([0.05, 0.10, 0.15]),
        current_plan_target=None,
        current_filter=current_filter,
        current_plan_progress=0.0,
        moon_phase=moon_phase,
        noise_targets=["GhostObj", "ArchiveCandidate", "PhantomTransient"],
        ambiguous_text=(
            "Multiple validated transient candidates are simultaneously available and "
            "must be ranked under time, filter, and environmental trade-offs."
        ),
        targets=[competing_a, competing_b, supporting_c],
    )


def gen_s4(i: int, pool: List[Target], rng: random.Random) -> Scenario:
    low_priority_base, existing_plan_base = _sample_targets(pool, 2, rng)

    ongoing_routine_target = _set_target(
        existing_plan_base,
        priority=2,
        target_type="routine",
        window_start=90,
        window_end=240,
        duration=20,
        sensitivity_to_moon=min(existing_plan_base.sensitivity_to_moon, 0.35),
    )

    low_priority_transient = _set_target(
        low_priority_base,
        priority=1,
        target_type="too",
        window_start=105,
        window_end=220,
        duration=20,
        filter_required=rng.choice(["g", "r", "i"]),
        slew_cost=0.25,
        settling_cost=0.10,
        sensitivity_to_moon=max(low_priority_base.sensitivity_to_moon, 0.65),
    )

    return Scenario(
        scenario_id=f"S4_{i:03d}",
        scenario_type="S4",
        instrument_status="normal",
        cloud=rng.choice([0.20, 0.25, 0.30]),
        current_plan_target=ongoing_routine_target.id,
        current_filter=ongoing_routine_target.filter_required or "r",
        current_plan_progress=rng.choice([0.45, 0.60, 0.75]),
        moon_phase=rng.choice([0.30, 0.50, 0.70]),
        noise_targets=["OldBurstA", "ArchiveSignalB", "UnverifiedNote"],
        ambiguous_text=(
            "Several unrelated historical notes and weak auxiliary alerts are present. "
            "The scheduler should avoid being distracted by noisy context and "
            "unnecessary plan interruption."
        ),
        targets=[low_priority_transient, ongoing_routine_target],
    )


def gen_s5(i: int, pool: List[Target], rng: random.Random) -> Scenario:
    base_a, base_b, base_c, base_d = _sample_targets(pool, 4, rng)

    current_filter = rng.choice(["g", "r", "i"])
    moon_phase = rng.choice([0.40, 0.55, 0.70, 0.85])

    # intentionally complex but not trivially unique
    target_a = _set_target(
        base_a,
        priority=3,
        target_type="too",
        window_start=100,
        window_end=150,
        duration=20,
        filter_required=current_filter,
        slew_cost=0.08,
        settling_cost=0.04,
        sensitivity_to_moon=max(base_a.sensitivity_to_moon, 0.35),
    )

    target_b = _set_target(
        base_b,
        priority=3,
        target_type="too",
        window_start=100,
        window_end=180,
        duration=18,
        filter_required=rng.choice([f for f in ["g", "r", "i"] if f != current_filter]),
        slew_cost=0.40,
        settling_cost=0.16,
        sensitivity_to_moon=max(base_b.sensitivity_to_moon, 0.85),
    )

    target_c = _set_target(
        base_c,
        priority=2,
        target_type="too",
        window_start=95,
        window_end=165,
        duration=16,
        filter_required=current_filter,
        slew_cost=0.05,
        settling_cost=0.03,
        sensitivity_to_moon=max(base_c.sensitivity_to_moon, 0.25),
    )

    target_d = _set_target(
        base_d,
        priority=1,
        target_type="routine",
        window_start=90,
        window_end=250,
        duration=20,
        filter_required=current_filter,
        slew_cost=0.02,
        settling_cost=0.02,
        sensitivity_to_moon=min(base_d.sensitivity_to_moon, 0.25),
    )

    return Scenario(
        scenario_id=f"S5_{i:03d}",
        scenario_type="S5",
        instrument_status="normal",
        cloud=rng.choice([0.35, 0.45, 0.55, 0.65]),
        current_plan_target=None,
        current_filter=current_filter,
        current_plan_progress=0.0,
        moon_phase=moon_phase,
        noise_targets=["GhostCritical", "FakeUrgentA", "StaleArchiveItem"],
        ambiguous_text=(
            "Several urgent transient opportunities coexist under complex conditions, "
            "including noisy context, different filters, and environmental trade-offs."
        ),
        targets=[target_a, target_b, target_c, target_d],
    )


def gen_s6(i: int, pool: List[Target], rng: random.Random) -> Scenario:
    base_a, base_b = _sample_targets(pool, 2, rng)

    candidate_under_invalid_window = _set_target(
        base_a,
        priority=3,
        target_type="too",
        window_start=150,
        window_end=140,
        duration=20,
    )

    candidate_with_infeasible_duration = _set_target(
        base_b,
        priority=2,
        target_type="routine",
        window_start=100,
        window_end=200,
        duration=150,
    )

    return Scenario(
        scenario_id=f"S6_{i:03d}",
        scenario_type="S6",
        instrument_status="abnormal",
        cloud=rng.choice([0.20, 0.30, 0.40]),
        current_plan_target=None,
        current_filter=rng.choice(["g", "r", "i"]),
        current_plan_progress=0.0,
        moon_phase=rng.choice([0.10, 0.50, 0.90]),
        ambiguous_text="The instrument is abnormal; no observing action should be issued.",
        targets=[candidate_under_invalid_window, candidate_with_infeasible_duration],
    )


GEN_MAP = {
    "S1": gen_s1,
    "S2": gen_s2,
    "S3": gen_s3,
    "S4": gen_s4,
    "S5": gen_s5,
    "S6": gen_s6,
}


def generate_all_cases(
    out_dir: Path,
    n_per_scenario: int,
    seed: int = 42,
    alert_dir: str = ".",
    max_alert_targets: int = 300,
) -> List[Scenario]:
    rng = random.Random(seed)

    target_pool = build_target_pool_from_real_alerts(
        folder=alert_dir,
        max_targets=max_alert_targets,
        seed=seed,
    )

    all_cases: List[Scenario] = []

    for scenario_type, generator in GEN_MAP.items():
        for i in range(1, n_per_scenario + 1):
            case = generator(i, target_pool, rng)
            _save_case(out_dir, case)
            all_cases.append(case)

    return all_cases


def load_cases(case_dir: Path) -> List[Scenario]:
    out: List[Scenario] = []
    for fp in sorted(case_dir.glob("*.json")):
        with open(fp, "r", encoding="utf-8") as f:
            data = json.load(f)
        out.append(Scenario.from_dict(data))
    return out