from __future__ import annotations

import argparse
import csv
import json
from pathlib import Path
from typing import List, Dict, Any

import matplotlib.pyplot as plt

from generate_cases import generate_all_cases, load_cases
from methods_baseline import (
    run_baseline_rule_priority,
    run_baseline_rule_deadline,
    run_baseline_greedy,
    run_baseline_weighted_heuristic,

)
from methods_qwen3 import Qwen3GGUFRunner, run_qwen3_only
from methods_framework import run_qwen3_framework
from evaluate import evaluate_case, aggregate_overall, aggregate_by_scenario, aggregate_failures


def save_csv(path: Path, rows: List[Dict[str, Any]]) -> None:
    if not rows:
        return
    fieldnames = list(rows[0].keys())
    with open(path, "w", newline="", encoding="utf-8") as f:
        writer = csv.DictWriter(f, fieldnames=fieldnames)
        writer.writeheader()
        writer.writerows(rows)


def save_json(path: Path, obj: Dict[str, Any]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    with open(path, "w", encoding="utf-8") as f:
        json.dump(obj, f, ensure_ascii=False, indent=2)


def plot_metric(rows: List[Dict[str, Any]], metric: str, out_path: Path, title: str):
    methods = [r["method"] for r in rows]
    vals = [0 if r[metric] is None else r[metric] for r in rows]
    plt.figure(figsize=(9, 5))
    plt.bar(methods, vals)
    plt.title(title)
    plt.ylabel(metric)
    plt.xticks(rotation=20, ha="right")
    plt.ylim(0, max(1.0, max(vals) * 1.15 if vals else 1.0))
    plt.tight_layout()
    plt.savefig(out_path, dpi=180)
    plt.close()


def main():
    parser = argparse.ArgumentParser(description="Final ZTF-driven Qwen3 scheduling comparison experiment")
    parser.add_argument("--case_dir", type=str, default="input_cases")
    parser.add_argument("--output_dir", type=str, default="output_results")
    parser.add_argument("--alert_dir", type=str, default="alarm", help="Directory containing real ZTF .avro alerts")
    parser.add_argument("--model_path", type=str, default="models/Qwen3-Coder-30B-A3B-Instruct-UD-IQ2_M.gguf")
    parser.add_argument("--generate_cases", action="store_true")
    parser.add_argument("--n_per_scenario", type=int, default=6)
    parser.add_argument("--seed", type=int, default=42)
    parser.add_argument("--skip_model", action="store_true")
    parser.add_argument("--ctx_size", type=int, default=2048)
    parser.add_argument("--threads", type=int, default=8)
    parser.add_argument("--n_gpu_layers", type=int, default=0)
    parser.add_argument("--temperature", type=float, default=0.0)
    parser.add_argument("--max_tokens", type=int, default=80)
    args = parser.parse_args()

    case_dir = Path(args.case_dir)
    output_dir = Path(args.output_dir)
    raw_dir = output_dir / "raw_results"
    fig_dir = output_dir / "figures"
    output_dir.mkdir(parents=True, exist_ok=True)
    raw_dir.mkdir(parents=True, exist_ok=True)
    fig_dir.mkdir(parents=True, exist_ok=True)

    if args.generate_cases:
        cases = generate_all_cases(
            out_dir=case_dir,
            n_per_scenario=args.n_per_scenario,
            seed=args.seed,
            alert_dir=args.alert_dir,
            max_alert_targets=300,
        )
    else:
        cases = load_cases(case_dir)

    if not cases:
        raise RuntimeError("No cases found. Use --generate_cases to create them first.")

    runner = None if args.skip_model else Qwen3GGUFRunner(
        model_path=args.model_path,
        ctx_size=args.ctx_size,
        threads=args.threads,
        temperature=args.temperature,
        max_tokens=args.max_tokens,
        n_gpu_layers=args.n_gpu_layers,
    )

    case_rows: List[Dict[str, Any]] = []

    for case in cases:
        # baseline_rule_priority
        d0 = run_baseline_rule_priority(case)
        r0 = evaluate_case(case, "baseline_rule_priority", d0)
        case_rows.append(r0)
        save_json(raw_dir / f"{case.scenario_id}__baseline_rule_priority.json",
                  {"decision": d0.to_dict(), "evaluation": r0})

        # baseline_rule_deadline
        d1 = run_baseline_rule_deadline(case)
        r1 = evaluate_case(case, "baseline_rule_deadline", d1)
        case_rows.append(r1)
        save_json(raw_dir / f"{case.scenario_id}__baseline_rule_deadline.json",
                  {"decision": d1.to_dict(), "evaluation": r1})

        # baseline_greedy
        d2 = run_baseline_greedy(case)
        r2 = evaluate_case(case, "baseline_greedy", d2)
        case_rows.append(r2)
        save_json(raw_dir / f"{case.scenario_id}__baseline_greedy.json", {"decision": d2.to_dict(), "evaluation": r2})

        # baseline_weighted_heuristic
        d3 = run_baseline_weighted_heuristic(case)
        r3 = evaluate_case(case, "baseline_weighted_heuristic", d3)
        case_rows.append(r3)
        save_json(raw_dir / f"{case.scenario_id}__baseline_weighted_heuristic.json",
                  {"decision": d3.to_dict(), "evaluation": r3})


        
        if runner is not None:
            dq = run_qwen3_only(case, runner)
            rq = evaluate_case(case, "qwen3_only", dq)
            case_rows.append(rq)
            save_json(raw_dir / f"{case.scenario_id}__qwen3_only.json", {"decision": dq.to_dict(), "evaluation": rq})

            dfw = run_qwen3_framework(case, runner)
            rfw = evaluate_case(case, "qwen3_framework", dfw)
            case_rows.append(rfw)
            save_json(raw_dir / f"{case.scenario_id}__qwen3_framework.json", {"decision": dfw.to_dict(), "evaluation": rfw})

    overall = aggregate_overall(case_rows)
    by_scenario = aggregate_by_scenario(case_rows)
    failure_summary = aggregate_failures(case_rows)

    save_csv(output_dir / "case_level_results.csv", case_rows)
    save_csv(output_dir / "overall_summary.csv", overall)
    save_csv(output_dir / "scenario_summary.csv", by_scenario)
    save_csv(output_dir / "failure_summary.csv", failure_summary)

    plot_metric(overall, "EDR", fig_dir / "overall_edr.png", "Executable Decision Rate by Method")
    plot_metric(overall, "ARD", fig_dir / "overall_pcsr.png", "Priority-Consistent Selection Rate by Method")
    plot_metric(overall, "WDS", fig_dir / "overall_wds.png", "Weighted Decision Score by Method")
    plot_metric(overall, "UER", fig_dir / "overall_sr.png", "Unsafe Execution Rate by Method")
    plot_metric(overall, "invalid_reference_rate", fig_dir / "overall_invalid_reference_rate.png", "Invalid Reference Rate by Method")
    plot_metric(overall, "infeasible_target_rate", fig_dir / "overall_infeasible_target_rate.png", "Infeasible Target Rate by Method")
    plot_metric(overall, "priority_violation_rate", fig_dir / "overall_priority_violation_rate.png", "Priority Violation Rate by Method")
    plot_metric(overall, "unsafe_execution_rate", fig_dir / "overall_unsafe_execution_rate.png", "Unsafe Execution Rate by Method")

    print(f"Saved case-level results to: {output_dir / 'case_level_results.csv'}")
    print(f"Saved overall summary to: {output_dir / 'overall_summary.csv'}")
    print(f"Saved scenario summary to: {output_dir / 'scenario_summary.csv'}")
    print(f"Saved failure summary to: {output_dir / 'failure_summary.csv'}")
    print(f"Saved figures to: {fig_dir}")


if __name__ == "__main__":
    main()
