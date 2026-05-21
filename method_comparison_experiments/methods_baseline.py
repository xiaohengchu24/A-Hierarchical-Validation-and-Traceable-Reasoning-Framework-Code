from __future__ import annotations

from typing import Optional, List

from schema import Scenario, Target, Decision


def current_plan_priority(s: Scenario) -> int:
    if not s.current_plan_target:
        return -1
    for t in s.targets:
        if t.id == s.current_plan_target:
            return t.priority
    return -1


def target_by_id(s: Scenario, tid: Optional[str]) -> Optional[Target]:
    if tid is None:
        return None
    for t in s.targets:
        if t.id == tid:
            return t
    return None


def is_executable(s: Scenario, t: Target) -> bool:
    if s.instrument_status != "normal":
        return False
    if s.cloud > 0.9:
        return False
    if not t.visible:
        return False
    if t.duration <= 0:
        return False
    if t.window_start >= t.window_end:
        return False
    if t.window_start + t.duration > t.window_end:
        return False
    return True


def choose_priority_window(targets: List[Target]) -> Optional[Target]:
    if not targets:
        return None
    return sorted(targets, key=lambda x: (-x.priority, x.window_end, x.duration, x.id))[0]


def choose_deadline_priority(targets: List[Target]) -> Optional[Target]:
    if not targets:
        return None
    return sorted(targets, key=lambda x: (x.window_end, -x.priority, x.duration, x.id))[0]


def _filter_switch_penalty(s: Scenario, t: Target) -> float:
    if not t.filter_required:
        return 0.0
    return 0.55 if s.current_filter != t.filter_required else 0.0


def _preemption_penalty(s: Scenario, t: Target) -> float:
    if s.current_plan_target is None:
        return 0.0
    if t.id == s.current_plan_target:
        return 0.0
    return 0.95 * s.current_plan_progress


def _moon_penalty(s: Scenario, t: Target) -> float:
    return s.moon_phase * t.sensitivity_to_moon

def greedy_score(s: Scenario, t: Target) -> float:
    """
    Simple utility-greedy baseline:
    favor immediate science value more than engineering conservatism.
    """
    af = t.alert_features or {}
    rb = float(af.get("rb", 0.0) or 0.0)
    drb = float(af.get("drb", 0.0) or 0.0)
    ndethist = float(af.get("ndethist", 0.0) or 0.0)
    tooflag = float(af.get("tooflag", 0.0) or 0.0)

    confidence = max(rb, drb)
    novelty_bonus = 1.0 if ndethist <= 3 else 0.0
    too_bonus = 1.0 if tooflag == 1 or t.target_type == "too" else 0.0
    urgency = 1.0 / max(1.0, (t.window_end - t.window_start))

    return (
        2.0 * t.priority
        + 1.2 * too_bonus
        + 1.5 * confidence
        + 1.0 * novelty_bonus
        + 3.0 * urgency
    )

def weighted_score(s: Scenario, t: Target) -> float:
    """
    Strong but not oracle-like heuristic.
    Priority and time window remain dominant.
    Moon, filter, progress, and small operational costs act as soft factors.
    """
    af = t.alert_features or {}
    rb = float(af.get("rb", 0.0) or 0.0)
    drb = float(af.get("drb", 0.0) or 0.0)
    ndethist = float(af.get("ndethist", 0.0) or 0.0)
    tooflag = float(af.get("tooflag", 0.0) or 0.0)

    confidence = max(rb, drb)
    urgency = 1.0 / max(1.0, (t.window_end - t.window_start))
    novelty_bonus = 1.0 if ndethist <= 3 else 0.0
    too_bonus = 1.0 if tooflag == 1 or t.target_type == "too" else 0.0

    filter_penalty = _filter_switch_penalty(s, t)
    preempt_penalty = _preemption_penalty(s, t)
    moon_penalty = _moon_penalty(s, t)

    score = (
        2.6 * t.priority
        + 0.9 * too_bonus
        + 1.1 * confidence
        + 0.8 * novelty_bonus
        + 4.0 * urgency
        - 0.5 * t.slew_cost
        - 0.40 * t.settling_cost
        - 1.40 * filter_penalty
        - 1.6 * preempt_penalty
        - 1.7 * moon_penalty
    )
    return score


def choose_weighted_heuristic(s: Scenario, targets: List[Target]) -> Optional[Target]:
    if not targets:
        return None
    return sorted(targets, key=lambda x: (-weighted_score(s, x), x.window_end, x.id))[0]


def run_baseline_rule_priority(s: Scenario) -> Decision:
    if s.scenario_type == "S6" or s.instrument_status != "normal":
        return Decision(selected_target=None, action="wait", reason_code="RULE_PRIORITY_WAIT")

    if not s.targets:
        return Decision(selected_target=None, action="wait", reason_code="RULE_PRIORITY_EMPTY")

    chosen = sorted(s.targets, key=lambda x: (-x.priority, x.window_end, x.duration, x.id))[0]
    return Decision(selected_target=chosen.id, action="observe", reason_code="RULE_PRIORITY")


def run_baseline_rule_deadline(s: Scenario) -> Decision:
    if s.scenario_type == "S6" or s.instrument_status != "normal":
        return Decision(selected_target=None, action="wait", reason_code="RULE_DEADLINE_WAIT")

    if not s.targets:
        return Decision(selected_target=None, action="wait", reason_code="RULE_DEADLINE_EMPTY")

    chosen = sorted(s.targets, key=lambda x: (x.window_end, -x.priority, x.duration, x.id))[0]
    return Decision(selected_target=chosen.id, action="observe", reason_code="RULE_DEADLINE")


def run_baseline_greedy(s: Scenario) -> Decision:
    """
    Greedy scheduling baseline:
    select the executable target with the highest immediate science utility.
    """
    if s.scenario_type == "S6" or s.instrument_status != "normal":
        return Decision(selected_target=None, action="wait", reason_code="GREEDY_WAIT")

    exec_targets = [t for t in s.targets if is_executable(s, t)]
    if not exec_targets:
        return Decision(selected_target=None, action="wait", reason_code="GREEDY_EMPTY")

    chosen = sorted(exec_targets, key=lambda x: (-greedy_score(s, x), x.window_end, x.id))[0]
    return Decision(selected_target=chosen.id, action="observe", reason_code="GREEDY")


def run_baseline_weighted_heuristic(s: Scenario) -> Decision:
    """
    Weighted heuristic baseline:
    a traditional scoring-based scheduler that selects the executable target
    with the highest weighted utility.

    It uses a fixed scoring function only, without scenario-specific hard-coded
    validation or fallback logic.
    """
    if s.scenario_type == "S6" or s.instrument_status != "normal":
        return Decision(selected_target=None, action="wait", reason_code="WEIGHTED_HEURISTIC_WAIT")

    exec_targets = [t for t in s.targets if is_executable(s, t)]

    if not exec_targets:
        return Decision(selected_target=None, action="wait", reason_code="WEIGHTED_HEURISTIC_EMPTY")

    chosen = choose_weighted_heuristic(s, exec_targets)
    if chosen is None:
        return Decision(selected_target=None, action="wait", reason_code="WEIGHTED_HEURISTIC_EMPTY")

    return Decision(selected_target=chosen.id, action="observe", reason_code="WEIGHTED_HEURISTIC")

def _too_costly_preemption(s: Scenario, t: Target) -> bool:
    """
    Traditional heuristic safeguard:
    avoid interrupting a nearly finished current plan unless the new target
    has a clearly stronger priority advantage and low operational disruption.
    """
    if s.current_plan_target is None:
        return False
    if t.id == s.current_plan_target:
        return False
    current = target_by_id(s, s.current_plan_target)
    if current is None:
        return False

    high_progress = s.current_plan_progress > 0.70
    small_priority_gap = (t.priority - current.priority) <= 1
    filter_mismatch = (t.filter_required is not None and s.current_filter != t.filter_required)
    operational_costly = (t.slew_cost + t.settling_cost) > 0.45

    return high_progress and small_priority_gap and (filter_mismatch or operational_costly)


def _moon_sensitive_bad_choice(s: Scenario, t: Target) -> bool:
    """
    Traditional observing safeguard:
    if moon is bright and the target is moon-sensitive, allow it only when priority is very high.
    """
    if s.moon_phase < 0.70:
        return False
    if t.sensitivity_to_moon < 0.70:
        return False
    return t.priority < 3
