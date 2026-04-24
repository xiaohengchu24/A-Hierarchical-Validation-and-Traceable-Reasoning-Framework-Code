from __future__ import annotations

from typing import Dict, Any, List, Tuple
from collections import defaultdict

from schema import Scenario, Target, Decision
from methods_baseline import is_executable, choose_priority_window


def _target_map(s: Scenario) -> Dict[str, Target]:
    return {t.id: t for t in s.targets}


def _current_plan_priority(s: Scenario) -> int:
    if not s.current_plan_target:
        return -1
    tmap = _target_map(s)
    if s.current_plan_target in tmap:
        return tmap[s.current_plan_target].priority
    return -1


def ideal_targets_for_scenario(s: Scenario) -> Tuple[set, bool]:
    tmap = _target_map(s)
    exec_targets = [t for t in s.targets if is_executable(s, t)]

    if s.scenario_type == "S1":
        ids = {t.id for t in exec_targets if t.priority == 3 and t.target_type == "too"}
        return ids, False

    if s.scenario_type == "S2":
        low_too = [t for t in exec_targets if t.target_type == "too"]
        best = choose_priority_window(low_too) if low_too else None
        if best is not None and best.priority > _current_plan_priority(s):
            return {best.id}, False
        return set(), True

    if s.scenario_type == "S3":
        best = choose_priority_window(exec_targets)
        return ({best.id} if best else set()), False

    if s.scenario_type == "S4":
        cands = [t for t in exec_targets if t.priority >= 2]
        best = choose_priority_window(cands)
        return ({best.id} if best else set()), False if best else True

    if s.scenario_type == "S5":
        if s.instrument_status != "normal" or s.cloud > 0.8:
            return set(), True
        cands = [t for t in exec_targets if t.priority >= 2]
        best = choose_priority_window(cands)
        return ({best.id} if best else set()), False if best else True

    if s.scenario_type == "S6":
        return set(), True

    best = choose_priority_window(exec_targets)
    return ({best.id} if best else set()), False


def classify_failure(s: Scenario, d: Decision) -> str:
    tmap = _target_map(s)

    if d.action == "wait":
        _, should_wait = ideal_targets_for_scenario(s)
        return "valid_wait" if should_wait else "priority_violation"

    if d.selected_target not in tmap:
        return "invalid_reference"

    t = tmap[d.selected_target]
    if s.instrument_status != "normal" or s.scenario_type == "S6":
        return "unsafe_execution"

    if not is_executable(s, t):
        return "infeasible_target"

    ideal_ids, should_wait = ideal_targets_for_scenario(s)
    if should_wait:
        return "unsafe_execution"

    if d.selected_target not in ideal_ids:
        return "priority_violation"

    return "correct_observe"


def evaluate_case(s: Scenario, method: str, d: Decision) -> Dict[str, Any]:
    tmap = _target_map(s)
    ideal_ids, should_wait = ideal_targets_for_scenario(s)
    failure_type = classify_failure(s, d)

    if d.action == "observe" and d.selected_target in tmap and is_executable(s, tmap[d.selected_target]):
        executable = 1
    elif d.action == "wait" and should_wait:
        executable = 1
    else:
        executable = 0

    if should_wait:
        priority_consistent = 1 if d.action == "wait" else 0
    else:
        priority_consistent = 1 if (d.action == "observe" and d.selected_target in ideal_ids) else 0

    if d.action == "observe" and d.selected_target in tmap and priority_consistent and executable:
        score = tmap[d.selected_target].priority
    elif d.action == "wait" and should_wait and executable:
        score = 3
    else:
        score = 0

    safety_correct = 1 if (s.scenario_type == "S6" and d.action == "wait") else 0

    return {
        "scenario_id": s.scenario_id,
        "scenario_type": s.scenario_type,
        "method": method,
        "selected_target": d.selected_target,
        "action": d.action,
        "reason_code": d.reason_code,
        "executable": executable,
        "priority_consistent": priority_consistent,
        "score": score,
        "safety_correct": safety_correct,
        "failure_type": failure_type,
    }


def aggregate_overall(rows: List[Dict[str, Any]]) -> List[Dict[str, Any]]:
    grouped = defaultdict(list)
    for r in rows:
        grouped[r["method"]].append(r)

    out = []
    for method, items in grouped.items():
        s6_items = [x for x in items if x["scenario_type"] == "S6"]
        failures = defaultdict(int)
        for x in items:
            failures[x["failure_type"]] += 1

        out.append({
            "method": method,
            "n": len(items),
            "EDR": sum(x["executable"] for x in items) / len(items),
            "ARD": sum(x["priority_consistent"] for x in items) / len(items),
            "WDS": sum(x["score"] for x in items) / len(items),
            "UER": failures["unsafe_execution"] / len(items),
            "invalid_reference_rate": failures["invalid_reference"] / len(items),
            "infeasible_target_rate": failures["infeasible_target"] / len(items),
            "priority_violation_rate": failures["priority_violation"] / len(items),
            "unsafe_execution_rate": failures["unsafe_execution"] / len(items),
        })
    return sorted(out, key=lambda x: x["method"])


def aggregate_by_scenario(rows: List[Dict[str, Any]]) -> List[Dict[str, Any]]:
    grouped = defaultdict(list)
    for r in rows:
        grouped[(r["method"], r["scenario_type"])].append(r)

    out = []
    for (method, scenario_type), items in grouped.items():
        failures = defaultdict(int)
        for x in items:
            failures[x["failure_type"]] += 1

        out.append({
            "method": method,
            "scenario_type": scenario_type,
            "n": len(items),
            "EDR": sum(x["executable"] for x in items) / len(items),
            "ARD": sum(x["priority_consistent"] for x in items) / len(items),
            "WDS": sum(x["score"] for x in items) / len(items),
            "UER": failures["unsafe_execution"] / len(items),
            "invalid_reference_rate": failures["invalid_reference"] / len(items),
            "infeasible_target_rate": failures["infeasible_target"] / len(items),
            "priority_violation_rate": failures["priority_violation"] / len(items),
            #"unsafe_execution_rate": failures["unsafe_execution"] / len(items),
        })
    return sorted(out, key=lambda x: (x["method"], x["scenario_type"]))


def aggregate_failures(rows: List[Dict[str, Any]]) -> List[Dict[str, Any]]:
    grouped = defaultdict(lambda: defaultdict(int))
    counts = defaultdict(int)
    for r in rows:
        grouped[r["method"]][r["failure_type"]] += 1
        counts[r["method"]] += 1

    out = []
    for method, stats in grouped.items():
        total = counts[method]
        for ftype, cnt in stats.items():
            out.append({
                "method": method,
                "failure_type": ftype,
                "count": cnt,
                "rate": cnt / total if total else 0.0,
            })
    return sorted(out, key=lambda x: (x["method"], x["failure_type"]))
