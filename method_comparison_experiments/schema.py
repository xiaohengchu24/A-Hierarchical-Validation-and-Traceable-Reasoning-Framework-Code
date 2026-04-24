from __future__ import annotations

from dataclasses import dataclass, field, asdict
from typing import List, Optional, Dict, Any


@dataclass
class Target:
    id: str
    priority: int
    window_start: int
    window_end: int
    duration: int
    target_type: str = "routine"
    visible: bool = True
    filter_required: Optional[str] = None
    alert_features: Dict[str, Any] = field(default_factory=dict)

    # lightweight operational realism
    slew_cost: float = 0.0
    settling_cost: float = 0.0

    # lunar sensitivity: 0.0 ~ 1.0
    sensitivity_to_moon: float = 0.0

    @staticmethod
    def from_dict(d: Dict[str, Any]) -> "Target":
        return Target(
            id=str(d["id"]),
            priority=int(d["priority"]),
            window_start=int(d["window_start"]),
            window_end=int(d["window_end"]),
            duration=int(d["duration"]),
            target_type=str(d.get("target_type", "routine")),
            visible=bool(d.get("visible", True)),
            filter_required=d.get("filter_required"),
            alert_features=dict(d.get("alert_features", {})),
            slew_cost=float(d.get("slew_cost", 0.0)),
            settling_cost=float(d.get("settling_cost", 0.0)),
            sensitivity_to_moon=float(d.get("sensitivity_to_moon", 0.0)),
        )

    def to_dict(self) -> Dict[str, Any]:
        return asdict(self)


@dataclass
class Scenario:
    scenario_id: str
    scenario_type: str
    instrument_status: str
    cloud: float
    current_plan_target: Optional[str]
    noise_targets: List[str] = field(default_factory=list)
    ambiguous_text: str = ""
    targets: List[Target] = field(default_factory=list)

    # operational context
    current_filter: str = "r"
    current_plan_progress: float = 0.0

    # 0.0 = new moon, 1.0 = full moon
    moon_phase: float = 0.0

    @staticmethod
    def from_dict(d: Dict[str, Any]) -> "Scenario":
        return Scenario(
            scenario_id=str(d["scenario_id"]),
            scenario_type=str(d["scenario_type"]),
            instrument_status=str(d.get("instrument_status", "normal")),
            cloud=float(d.get("cloud", 0.0)),
            current_plan_target=d.get("current_plan_target"),
            noise_targets=list(d.get("noise_targets", [])),
            ambiguous_text=str(d.get("ambiguous_text", "")),
            targets=[Target.from_dict(x) for x in d.get("targets", [])],
            current_filter=str(d.get("current_filter", "r")),
            current_plan_progress=float(d.get("current_plan_progress", 0.0)),
            moon_phase=float(d.get("moon_phase", 0.0)),
        )

    def to_dict(self) -> Dict[str, Any]:
        return {
            "scenario_id": self.scenario_id,
            "scenario_type": self.scenario_type,
            "instrument_status": self.instrument_status,
            "cloud": self.cloud,
            "current_plan_target": self.current_plan_target,
            "noise_targets": self.noise_targets,
            "ambiguous_text": self.ambiguous_text,
            "targets": [t.to_dict() for t in self.targets],
            "current_filter": self.current_filter,
            "current_plan_progress": self.current_plan_progress,
            "moon_phase": self.moon_phase,
        }


@dataclass
class Decision:
    selected_target: Optional[str]
    action: str
    reason_code: Optional[str] = None
    raw_text: Optional[str] = None

    @staticmethod
    def from_dict(d: Dict[str, Any]) -> "Decision":
        return Decision(
            selected_target=d.get("selected_target"),
            action=str(d.get("action", "wait")),
            reason_code=d.get("reason_code"),
            raw_text=d.get("raw_text"),
        )

    def to_dict(self) -> Dict[str, Any]:
        return {
            "selected_target": self.selected_target,
            "action": self.action,
            "reason_code": self.reason_code,
            "raw_text": self.raw_text,
        }