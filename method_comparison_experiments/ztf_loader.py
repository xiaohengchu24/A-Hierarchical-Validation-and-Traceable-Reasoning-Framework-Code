from __future__ import annotations

import os
import random
from typing import List, Dict, Any, Optional
from fastavro import reader

from schema import Target


def _safe_get(d: Dict[str, Any], key: str, default=None):
    if d is None:
        return default
    return d.get(key, default)


def _fid_to_filter(fid: Optional[int]) -> str:
    if fid == 1:
        return "g"
    if fid == 2:
        return "r"
    if fid == 3:
        return "i"
    return "r"


def _moon_sensitivity_from_alert(
    target_type: str,
    magpsf: float,
    ndethist: int,
    rb: float,
    drb: float,
) -> float:
    """
    Lightweight heuristic:
    - fainter targets: more sensitive to moonlight
    - early/rare transient-like candidates: more sensitive
    """
    confidence = max(rb, drb)

    base = 0.2
    if target_type == "too":
        base += 0.25
    if ndethist <= 3:
        base += 0.15
    if magpsf >= 20.5:
        base += 0.25
    elif magpsf >= 19.5:
        base += 0.15
    if confidence >= 0.9:
        base += 0.05

    return max(0.0, min(1.0, round(base, 3)))


def load_ztf_alerts_from_dir(folder: str) -> List[dict]:
    alerts: List[dict] = []

    for fname in os.listdir(folder):
        if not fname.lower().endswith(".avro"):
            continue

        fpath = os.path.join(folder, fname)

        try:
            with open(fpath, "rb") as f:
                avro_reader = reader(f)
                for record in avro_reader:
                    alerts.append(record)
        except Exception as e:
            print(f"[WARN] Failed to read {fname}: {e}")

    return alerts


def filter_valid_candidates(alerts: List[dict]) -> List[dict]:
    valid: List[dict] = []

    for a in alerts:
        try:
            if not isinstance(a, dict):
                continue

            cand = a.get("candidate", None)
            if cand is None or not isinstance(cand, dict):
                continue

            object_id = a.get("objectId", None)
            ra = cand.get("ra", None)
            dec = cand.get("dec", None)

            if object_id is None and (ra is None or dec is None):
                continue

            rb = _safe_get(cand, "rb", None)
            drb = _safe_get(cand, "drb", None)
            magpsf = _safe_get(cand, "magpsf", None)
            ndethist = _safe_get(cand, "ndethist", None)
            ssdistnr = _safe_get(cand, "ssdistnr", None)

            if rb is not None and drb is not None:
                if rb < 0.30 and drb < 0.30:
                    continue
            elif rb is not None and rb < 0.30:
                continue
            elif drb is not None and drb < 0.30:
                continue

            if magpsf is not None and magpsf > 22.5:
                continue

            if ndethist is not None and ndethist < 0:
                continue

            if ssdistnr is not None and ssdistnr < 0.5:
                continue

            valid.append(a)

        except Exception:
            continue

    return valid


def alert_to_target(a: dict, rng: Optional[random.Random] = None, index: int = 0) -> Target:
    if rng is None:
        rng = random.Random(42)

    cand = a["candidate"]
    object_id = str(a.get("objectId", f"ZTF_{index:05d}"))

    rb = float(_safe_get(cand, "rb", 0.0) or 0.0)
    drb = float(_safe_get(cand, "drb", 0.0) or 0.0)
    ndethist = int(_safe_get(cand, "ndethist", 0) or 0)
    tooflag = int(_safe_get(cand, "tooflag", 0) or 0)
    magpsf = float(_safe_get(cand, "magpsf", 99.0) or 99.0)
    fid = _safe_get(cand, "fid", None)
    ra = _safe_get(cand, "ra", None)
    dec = _safe_get(cand, "dec", None)

    if tooflag == 1 and (rb >= 0.90 or drb >= 0.90) and ndethist <= 3:
        priority = 3
        target_type = "too"
    elif (rb >= 0.80 or drb >= 0.80) and ndethist <= 10:
        priority = 2
        target_type = "too" if tooflag == 1 else "routine"
    else:
        priority = 1
        target_type = "routine"

    window_start = rng.randint(80, 180)
    if priority == 3:
        window_length = rng.choice([35, 40, 45, 50])
    elif priority == 2:
        window_length = rng.choice([45, 55, 65])
    else:
        window_length = rng.choice([60, 70, 80])

    window_end = window_start + window_length
    duration = 10 + int((ndethist or 0) % 10)
    duration = max(8, min(duration, max(10, window_length - 5)))

    filter_required = _fid_to_filter(fid)

    # keep these small: auxiliary realism, not dominant factors
    slew_cost = round(rng.uniform(0.05, 0.50), 3)
    settling_cost = round(rng.uniform(0.02, 0.20), 3)

    sensitivity_to_moon = _moon_sensitivity_from_alert(
        target_type=target_type,
        magpsf=magpsf,
        ndethist=ndethist,
        rb=rb,
        drb=drb,
    )

    return Target(
        id=object_id,
        priority=priority,
        window_start=window_start,
        window_end=window_end,
        duration=duration,
        target_type=target_type,
        visible=True,
        filter_required=filter_required,
        alert_features={
            "rb": rb,
            "drb": drb,
            "ndethist": ndethist,
            "tooflag": tooflag,
            "magpsf": magpsf,
            "fid": fid,
            "ra": ra,
            "dec": dec,
        },
        slew_cost=slew_cost,
        settling_cost=settling_cost,
        sensitivity_to_moon=sensitivity_to_moon,
    )


def build_target_pool_from_real_alerts(
    folder: str,
    max_targets: int = 100,
    seed: int = 42,
) -> List[Target]:
    rng = random.Random(seed)

    alerts = load_ztf_alerts_from_dir(folder)
    print(f"[INFO] Loaded raw alerts: {len(alerts)}")

    vetted = filter_valid_candidates(alerts)
    print(f"[INFO] Vetted alerts after filtering: {len(vetted)}")

    if not vetted:
        raise RuntimeError(
            f"No valid ZTF alerts found in folder: {folder}. "
            "Try using more alert files or relaxing filter thresholds."
        )

    rng.shuffle(vetted)
    selected = vetted[:max_targets]

    targets: List[Target] = []
    for i, a in enumerate(selected):
        try:
            targets.append(alert_to_target(a, rng=rng, index=i))
        except Exception as e:
            print(f"[WARN] Failed to convert alert #{i}: {e}")

    print(f"[INFO] Converted targets: {len(targets)}")
    return targets