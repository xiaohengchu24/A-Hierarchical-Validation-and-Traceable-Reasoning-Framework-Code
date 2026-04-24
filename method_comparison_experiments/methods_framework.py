from __future__ import annotations

from typing import Optional, Tuple, List

from schema import Scenario, Target, Decision
from methods_baseline import (
    is_executable,
    choose_priority_window,
)
from methods_qwen3 import (
    run_qwen3_only,
    Qwen3GGUFRunner,
    _extract_json_block,
    _sanitize_decision,
)
from prompts import SYSTEM_PROMPT, build_user_prompt


# -----------------------------
# Basic helpers
# -----------------------------

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


# -----------------------------
# Soft reasoning audit only
# These issues are NOT blocking failures.
# They support your "reasoning audit" part.
# -----------------------------

def _too_costly_preemption(s: Scenario, t: Target) -> bool:
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
    Soft audit rule only:
    if moon is bright and target is moon-sensitive, mark it for reasoning review,
    but do NOT block execution if the target is otherwise executable.
    """
    if s.moon_phase < 0.70:
        return False
    if t.sensitivity_to_moon < 0.70:
        return False
    return t.priority < 3


def audit_reasoning(s: Scenario, d: Decision) -> List[str]:
    """
    Non-blocking reasoning audit.
    These flags are used for traceability / reasoning review only.
    They must NOT trigger repair or fallback.
    """
    flags: List[str] = []

    if d.action != "observe":
        return flags

    t = target_by_id(s, d.selected_target)
    if t is None:
        return flags

    if not is_executable(s, t):
        return flags

    if _too_costly_preemption(s, t):
        flags.append("suboptimal_preemption")

    if _moon_sensitive_bad_choice(s, t):
        flags.append("suboptimal_moon_choice")

    # Optional soft checks by scenario: keep for audit only, not blocking.
    exec_targets = [x for x in s.targets if is_executable(s, x)]

    if s.scenario_type == "S1":
        preferred = [x for x in exec_targets if x.priority == 3 and x.target_type == "too"]
        if preferred:
            best = sorted(preferred, key=lambda x: (-x.priority, x.window_end, x.id))[0]
            if best and t.id != best.id:
                flags.append("suboptimal_choice")

    elif s.scenario_type == "S3":
        best = choose_priority_window(exec_targets)
        if best and t.id != best.id:
            flags.append("suboptimal_choice")

    elif s.scenario_type == "S5":
        cands = [x for x in exec_targets if x.priority >= 2]
        best = choose_priority_window(cands) if cands else None
        if best and t.id != best.id:
            flags.append("suboptimal_choice")

    return flags


# -----------------------------
# Hard validation only
# These are true "errors / unsafe / not executable / should block" cases.
# -----------------------------

HARD_FAILURES = {
    "invalid_reference",
    "infeasible_target",
    "unsafe_execution",
    "unnecessary_wait",
    "priority_violation",
}


def validate_hierarchical(s: Scenario, d: Decision) -> Tuple[bool, Optional[str]]:
    """
    Hard validation aligned with the paper's main goal:
    - filter or repair erroneous / unsafe / non-executable decisions
    - do NOT optimize merely suboptimal but executable decisions
    """

    # 1) Global hard safety
    if s.instrument_status != "normal":
        if d.action == "wait":
            return True, None
        return False, "unsafe_execution"

    if s.scenario_type == "S6":
        if d.action == "wait":
            return True, None
        return False, "unsafe_execution"

    # 2) Wait branch
    if d.action == "wait":
        exec_targets = [t for t in s.targets if is_executable(s, t)]

        # These scenarios are explicitly designed so that waiting
        # while an executable candidate exists is considered an error.
        if s.scenario_type in {"S1", "S3", "S5"} and len(exec_targets) > 0:
            return False, "unnecessary_wait"

        return True, None

    # 3) Observe branch: target must exist
    t = target_by_id(s, d.selected_target)
    if t is None:
        return False, "invalid_reference"

    # 4) Target must be executable
    if not is_executable(s, t):
        return False, "infeasible_target"

    # 5) Hard scenario-level violations only
    # Keep only those that match your "error / should not execute" logic,
    # not soft optimality preferences.

    if s.scenario_type == "S2":
        # Interrupting a stronger ongoing plan with a lower-priority target
        # is treated as a hard rule violation in this controlled scenario.
        if t.priority < current_plan_priority(s):
            return False, "priority_violation"

    if s.scenario_type == "S4":
        # In this scenario, priority-1 noisy targets should not trigger execution.
        if t.priority < 2:
            return False, "priority_violation"

    return True, None


# -----------------------------
# One-step feedback repair
# Only for HARD failures.
# -----------------------------

def run_qwen3_feedback_once(case: Scenario, runner: Qwen3GGUFRunner, failure_type: str) -> Decision:
    """
    Give the model one explicit correction chance after a HARD validation failure.
    """
    runner.load()

    feedback_instruction = f"""
The previous decision failed validation with error type: {failure_type}.

Please revise the decision once.

Requirements:
1. If the previous target is invalid, infeasible, or unsafe, you must not keep it.
2. If there exists an executable and reasonable target, do not choose "wait".
3. You must choose ONLY from the listed target IDs.
4. If no target is executable or the instrument state is unsafe, choose "wait".
5. Output JSON only. No explanations outside JSON.
"""

    prompt = (
        "<|im_start|>system\n" + SYSTEM_PROMPT + "\n<|im_end|>\n"
        "<|im_start|>user\n"
        + build_user_prompt(case)
        + "\n\nValidation feedback:\n"
        + feedback_instruction
        + "\n<|im_end|>\n"
        "<|im_start|>assistant\n"
    )

    out = runner.llm(
        prompt,
        max_tokens=runner.max_tokens,
        temperature=runner.temperature,
        stop=["<|im_end|>", "\nInput:", "\nOutput:", "\n\nExample"],
    )

    raw_text = out["choices"][0]["text"].strip()
    parsed = _extract_json_block(raw_text)
    return _sanitize_decision(case, parsed, raw_text)


# -----------------------------
# Main framework method
# -----------------------------

def run_qwen3_framework(case: Scenario, runner: Qwen3GGUFRunner) -> Decision:
    """
    Framework pipeline:
    1) first-pass AI output
    2) hard validation
    3) if hard error -> feedback once to AI for repair
    4) if still fails -> block (wait)
    5) soft reasoning issues are audited only, not used for optimization
    """

    # First-pass model output
    raw_decision = run_qwen3_only(case, runner)

    # Hard validation
    valid, failure_type = validate_hierarchical(case, raw_decision)

    if valid:
        audit_flags = audit_reasoning(case, raw_decision)
        reason_code = raw_decision.reason_code or "VALIDATED_PASS"
        if audit_flags:
            reason_code = f"{reason_code}|AUDIT:{','.join(audit_flags)}"

        return Decision(
            selected_target=raw_decision.selected_target,
            action=raw_decision.action,
            reason_code=reason_code,
            raw_text=raw_decision.raw_text,
        )

    # Only HARD failures can trigger repair
    if failure_type not in HARD_FAILURES:
        audit_flags = audit_reasoning(case, raw_decision)
        reason_code = raw_decision.reason_code or "VALIDATED_PASS"
        if audit_flags:
            reason_code = f"{reason_code}|AUDIT:{','.join(audit_flags)}"

        return Decision(
            selected_target=raw_decision.selected_target,
            action=raw_decision.action,
            reason_code=reason_code,
            raw_text=raw_decision.raw_text,
        )

    # One correction chance: feed hard validation error back to the model
    revised_decision = run_qwen3_feedback_once(case, runner, failure_type)
    valid2, failure_type2 = validate_hierarchical(case, revised_decision)

    if valid2:
        audit_flags = audit_reasoning(case, revised_decision)
        reason_code = f"FEEDBACK_FIX_AFTER_{failure_type}"
        if audit_flags:
            reason_code = f"{reason_code}|AUDIT:{','.join(audit_flags)}"

        return Decision(
            selected_target=revised_decision.selected_target,
            action=revised_decision.action,
            reason_code=reason_code,
            raw_text=revised_decision.raw_text,
        )

    # If still invalid after one repair chance, BLOCK instead of fallback optimization
    return Decision(
        selected_target=None,
        action="wait",
        reason_code=f"BLOCK_AFTER_{failure_type2}",
        raw_text=revised_decision.raw_text,
    )