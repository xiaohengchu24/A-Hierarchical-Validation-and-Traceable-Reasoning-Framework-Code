from __future__ import annotations

import json
from schema import Scenario


SYSTEM_PROMPT = """You are an astronomical scheduling assistant.

You must make exactly one scheduling decision for the current case.

Decision goal:
Choose the most reasonable next action based on a balanced judgment of:
- target priority
- urgency (observation window)
- observational feasibility
- scientific value inferred from alert-related features
- current scheduling context

Important guidance:
1. You must choose ONLY from the target IDs explicitly listed in the input `targets`.
2. You must NEVER invent any target ID.
3. You should NOT blindly follow any single rule such as priority only or deadline only.
4. A higher-priority target is not always the best choice if it is infeasible or risky.
5. If no target is a reasonable choice, return action="wait".
6. If the instrument is abnormal, return wait.
7. Prefer conservative decisions over unsafe decisions.
8. Output JSON only. No markdown. No explanations outside JSON.

Required JSON schema:
{
  "selected_target": "target_id" or null,
  "action": "observe" or "wait",
  "reason_code": "SHORT_CODE"
}
"""


FEW_SHOT = """
Example 1
Input:
{
  "scenario_id": "S1_EXAMPLE",
  "scenario_type": "S1",
  "instrument_status": "normal",
  "cloud": 0.2,
  "current_plan_target": "R1",
  "noise_targets": [],
  "ambiguous_text": "A validated high-priority transient candidate has arrived.",
  "targets": [
    {"id": "TOO1", "priority": 3, "window_start": 100, "window_end": 175, "duration": 20, "target_type": "too", "visible": true, "filter_required": null, "alert_features": {"rb": 0.95, "drb": 0.97, "ndethist": 2, "tooflag": 1}},
    {"id": "R1", "priority": 2, "window_start": 50, "window_end": 260, "duration": 30, "target_type": "routine", "visible": true, "filter_required": null, "alert_features": {"rb": 0.82, "drb": 0.84, "ndethist": 15, "tooflag": 0}}
  ]
}
Output:
{"selected_target":"TOO1","action":"observe","reason_code":"URGENT_TRANSIENT"}

Example 2
Input:
{
  "scenario_id": "S2_EXAMPLE",
  "scenario_type": "S2",
  "instrument_status": "normal",
  "cloud": 0.1,
  "current_plan_target": "R2",
  "noise_targets": [],
  "ambiguous_text": "A low-priority transient alert is present, but the current plan is stronger.",
  "targets": [
    {"id": "TOO2", "priority": 1, "window_start": 100, "window_end": 220, "duration": 20, "target_type": "too", "visible": true, "filter_required": null, "alert_features": {"rb": 0.78, "drb": 0.79, "ndethist": 6, "tooflag": 0}},
    {"id": "R2", "priority": 3, "window_start": 80, "window_end": 270, "duration": 25, "target_type": "routine", "visible": true, "filter_required": null, "alert_features": {"rb": 0.88, "drb": 0.90, "ndethist": 12, "tooflag": 0}}
  ]
}
Output:
{"selected_target":null,"action":"wait","reason_code":"KEEP_CURRENT_PLAN"}

Example 3
Input:
{
  "scenario_id": "S3_EXAMPLE",
  "scenario_type": "S3",
  "instrument_status": "normal",
  "cloud": 0.15,
  "current_plan_target": null,
  "noise_targets": ["GhostObj", "ArchiveCandidate"],
  "ambiguous_text": "Several validated transient candidates are simultaneously available.",
  "targets": [
    {"id": "A", "priority": 2, "window_start": 95, "window_end": 155, "duration": 20, "target_type": "too", "visible": true, "filter_required": null, "alert_features": {"rb": 0.84, "drb": 0.85, "ndethist": 8, "tooflag": 0}},
    {"id": "B", "priority": 3, "window_start": 100, "window_end": 160, "duration": 15, "target_type": "too", "visible": true, "filter_required": null, "alert_features": {"rb": 0.93, "drb": 0.95, "ndethist": 2, "tooflag": 1}},
    {"id": "C", "priority": 3, "window_start": 100, "window_end": 210, "duration": 20, "target_type": "too", "visible": true, "filter_required": null, "alert_features": {"rb": 0.89, "drb": 0.91, "ndethist": 4, "tooflag": 1}}
  ]
}
Output:
{"selected_target":"B","action":"observe","reason_code":"BEST_BALANCED_CHOICE"}

Example 4
Input:
{
  "scenario_id": "S6_EXAMPLE",
  "scenario_type": "S6",
  "instrument_status": "abnormal",
  "cloud": 0.3,
  "current_plan_target": null,
  "noise_targets": [],
  "ambiguous_text": "The instrument is abnormal.",
  "targets": [
    {"id": "BAD1", "priority": 3, "window_start": 150, "window_end": 140, "duration": 20, "target_type": "too", "visible": true, "filter_required": null, "alert_features": {"rb": 0.92, "drb": 0.94, "ndethist": 1, "tooflag": 1}}
  ]
}
Output:
{"selected_target":null,"action":"wait","reason_code":"INSTRUMENT_ABNORMAL"}
"""


def build_user_prompt(case: Scenario) -> str:
    compact_case = json.dumps(case.to_dict(), ensure_ascii=False, indent=2)
    return (
        "Make one scheduling decision for the following astronomical scheduling case.\n\n"
        "You should balance multiple factors and avoid unsafe or unreasonable choices.\n\n"
        + FEW_SHOT
        + "\nNow solve this new case.\nInput:\n"
        + compact_case
        + "\nOutput:\n"
    )