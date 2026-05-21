#!/usr/bin/env python3
# -*- coding: utf-8 -*-

from __future__ import annotations

import argparse
import hashlib
import json
import re
import sqlite3
import sys
from collections import defaultdict, deque
from dataclasses import dataclass
from typing import Dict, Iterable, List, Optional, Set, Tuple


# ============================================================
# 数据结构
# ============================================================

@dataclass(frozen=True)
class Rule:
    rule_id: int
    raw_chain_text: str
    inputs: Tuple[str, ...]
    outputs: Tuple[str, ...]
    use_count: int = 1
    first_seen: Optional[str] = None
    last_used: Optional[str] = None
    signature: Optional[str] = None


# ============================================================
# 基础工具
# ============================================================

RULE_PATTERNS = [
    re.compile(r"^\((.*?)\)\s*-->\s*\((.*?)\)\s*$"),
    re.compile(r"^\((.*?)\)\s*->\s*\((.*?)\)\s*$"),
    re.compile(r"^\((.*?)\)\s*-->\s*([^\(\)].*?)\s*$"),
    re.compile(r"^\((.*?)\)\s*->\s*([^\(\)].*?)\s*$"),
]


def normalize_fact(fact: str) -> str:
    return re.sub(r"\s+", "", fact.strip())


def is_action_fact(fact: str) -> bool:
    return normalize_fact(fact).startswith("action_")


def split_items(text: str) -> List[str]:
    if text is None:
        return []
    items = [normalize_fact(x) for x in text.split(",")]
    return [x for x in items if x]


def parse_chain_text(chain_text: str) -> Tuple[List[str], List[str]]:
    text = chain_text.strip()
    for pattern in RULE_PATTERNS:
        m = pattern.match(text)
        if m:
            lhs = split_items(m.group(1))
            rhs = split_items(m.group(2))
            if not lhs:
                raise ValueError(f"规则左侧为空: {chain_text}")
            if not rhs:
                raise ValueError(f"规则右侧为空: {chain_text}")
            return lhs, rhs
    raise ValueError(f"无法解析规则: {chain_text}")


def canonical_signature(inputs: Iterable[str], outputs: Iterable[str]) -> str:
    inps = sorted({normalize_fact(x) for x in inputs if normalize_fact(x)})
    outs = sorted({normalize_fact(x) for x in outputs if normalize_fact(x)})
    text = f"({','.join(inps)})-->({','.join(outs)})"
    return hashlib.sha1(text.encode("utf-8")).hexdigest()


# ============================================================
# SQLite：建表 / 迁移
# ============================================================

def table_exists(conn: sqlite3.Connection, table_name: str) -> bool:
    cur = conn.cursor()
    cur.execute("""
        SELECT name
        FROM sqlite_master
        WHERE type='table' AND name=?
    """, (table_name,))
    return cur.fetchone() is not None


def get_table_columns(conn: sqlite3.Connection, table_name: str) -> Set[str]:
    cur = conn.cursor()
    cur.execute(f"PRAGMA table_info({table_name})")
    rows = cur.fetchall()
    return {row[1] for row in rows}


def ensure_normalized_tables(conn: sqlite3.Connection) -> None:
    cur = conn.cursor()

    if not table_exists(conn, "rules"):
        cur.execute("""
            CREATE TABLE rules (
                rule_id INTEGER PRIMARY KEY,
                atomic_chain_id INTEGER,
                raw_chain_text TEXT NOT NULL,
                signature TEXT NOT NULL UNIQUE,
                use_count INTEGER DEFAULT 1,
                first_seen DATETIME,
                last_used DATETIME
            )
        """)
    else:
        cols = get_table_columns(conn, "rules")
        if "atomic_chain_id" not in cols:
            cur.execute("ALTER TABLE rules ADD COLUMN atomic_chain_id INTEGER")
        if "raw_chain_text" not in cols:
            cur.execute("ALTER TABLE rules ADD COLUMN raw_chain_text TEXT")
        if "signature" not in cols:
            cur.execute("ALTER TABLE rules ADD COLUMN signature TEXT")
        if "use_count" not in cols:
            cur.execute("ALTER TABLE rules ADD COLUMN use_count INTEGER DEFAULT 1")
        if "first_seen" not in cols:
            cur.execute("ALTER TABLE rules ADD COLUMN first_seen DATETIME")
        if "last_used" not in cols:
            cur.execute("ALTER TABLE rules ADD COLUMN last_used DATETIME")

    if not table_exists(conn, "rule_inputs"):
        cur.execute("""
            CREATE TABLE rule_inputs (
                rule_id INTEGER NOT NULL,
                fact TEXT NOT NULL,
                PRIMARY KEY (rule_id, fact),
                FOREIGN KEY (rule_id) REFERENCES rules(rule_id)
            )
        """)

    if not table_exists(conn, "rule_outputs"):
        cur.execute("""
            CREATE TABLE rule_outputs (
                rule_id INTEGER NOT NULL,
                fact TEXT NOT NULL,
                PRIMARY KEY (rule_id, fact),
                FOREIGN KEY (rule_id) REFERENCES rules(rule_id)
            )
        """)

    conn.commit()

    cols = get_table_columns(conn, "rules")
    if "signature" in cols and "raw_chain_text" in cols:
        cur.execute("SELECT rule_id, raw_chain_text, signature FROM rules")
        for rule_id, raw_chain_text, signature in cur.fetchall():
            if raw_chain_text:
                try:
                    inputs, outputs = parse_chain_text(raw_chain_text)
                    sig = canonical_signature(inputs, outputs)
                    if not signature:
                        cur.execute(
                            "UPDATE rules SET signature = ? WHERE rule_id = ?",
                            (sig, rule_id)
                        )
                except Exception:
                    pass

    conn.commit()

    cols = get_table_columns(conn, "rules")
    if "signature" in cols:
        cur.execute("CREATE INDEX IF NOT EXISTS idx_rules_signature ON rules(signature)")
    if "atomic_chain_id" in cols:
        cur.execute("CREATE INDEX IF NOT EXISTS idx_rules_atomic_chain_id ON rules(atomic_chain_id)")
    cur.execute("CREATE INDEX IF NOT EXISTS idx_rule_inputs_fact ON rule_inputs(fact)")
    cur.execute("CREATE INDEX IF NOT EXISTS idx_rule_outputs_fact ON rule_outputs(fact)")
    cur.execute("CREATE INDEX IF NOT EXISTS idx_rule_inputs_rule ON rule_inputs(rule_id)")
    cur.execute("CREATE INDEX IF NOT EXISTS idx_rule_outputs_rule ON rule_outputs(rule_id)")
    conn.commit()


def validate_atomic_chains_exists(conn: sqlite3.Connection) -> None:
    cur = conn.cursor()
    cur.execute("""
        SELECT name
        FROM sqlite_master
        WHERE type='table' AND name='atomic_chains'
    """)
    row = cur.fetchone()
    if not row:
        raise RuntimeError("数据库中不存在 atomic_chains 表。")


# ============================================================
# 同步 atomic_chains -> 规范化表
# ============================================================

def sync_from_atomic_chains(conn: sqlite3.Connection, verbose: bool = True) -> Dict[str, int]:
    ensure_normalized_tables(conn)
    cur = conn.cursor()

    cur.execute("""
        SELECT id, chain_text, use_count, first_seen, last_used
        FROM atomic_chains
        ORDER BY id
    """)
    rows = cur.fetchall()

    cur.execute("SELECT signature FROM rules WHERE signature IS NOT NULL")
    existing_signatures = {row[0] for row in cur.fetchall()}

    inserted = 0
    skipped = 0
    failed = 0
    parsed_rows = []

    for idx, (atomic_id, chain_text, use_count, first_seen, last_used) in enumerate(rows, start=1):
        try:
            inputs, outputs = parse_chain_text(chain_text)
            sig = canonical_signature(inputs, outputs)

            if sig in existing_signatures:
                skipped += 1
                continue

            parsed_rows.append({
                "atomic_id": atomic_id,
                "chain_text": chain_text.strip(),
                "use_count": use_count or 1,
                "first_seen": first_seen,
                "last_used": last_used,
                "inputs": tuple(sorted(set(normalize_fact(x) for x in inputs))),
                "outputs": tuple(sorted(set(normalize_fact(x) for x in outputs))),
                "signature": sig,
            })
            existing_signatures.add(sig)

            if verbose and idx % 1000 == 0:
                print(f"[SYNC] 已扫描 {idx}/{len(rows)} 条，待插入 {len(parsed_rows)} 条...")

        except Exception as e:
            failed += 1
            if verbose:
                print(f"[WARN] 跳过 atomic_chains.id={atomic_id}: {e}", file=sys.stderr)

    for idx, item in enumerate(parsed_rows, start=1):
        cur.execute("""
            INSERT INTO rules (
                atomic_chain_id, raw_chain_text, signature, use_count, first_seen, last_used
            )
            VALUES (?, ?, ?, ?, ?, ?)
        """, (
            item["atomic_id"],
            item["chain_text"],
            item["signature"],
            item["use_count"],
            item["first_seen"],
            item["last_used"],
        ))
        rule_id = cur.lastrowid

        cur.executemany(
            "INSERT OR IGNORE INTO rule_inputs (rule_id, fact) VALUES (?, ?)",
            [(rule_id, fact) for fact in item["inputs"]]
        )
        cur.executemany(
            "INSERT OR IGNORE INTO rule_outputs (rule_id, fact) VALUES (?, ?)",
            [(rule_id, fact) for fact in item["outputs"]]
        )

        inserted += 1

        if verbose and idx % 1000 == 0:
            print(f"[SYNC] 已插入 {idx}/{len(parsed_rows)} 条...")

    conn.commit()

    stats = {
        "inserted": inserted,
        "skipped": skipped,
        "failed": failed,
        "total_atomic_rows": len(rows),
    }
    if verbose:
        print(json.dumps(stats, ensure_ascii=False, indent=2))
    return stats


# ============================================================
# 规则加载
# ============================================================

def load_rules(conn: sqlite3.Connection) -> List[Rule]:
    cur = conn.cursor()

    cur.execute("""
        SELECT
            r.rule_id,
            r.raw_chain_text,
            r.use_count,
            r.first_seen,
            r.last_used,
            r.signature
        FROM rules r
        ORDER BY r.rule_id
    """)
    rule_rows = cur.fetchall()

    inputs_map: Dict[int, List[str]] = defaultdict(list)
    outputs_map: Dict[int, List[str]] = defaultdict(list)

    cur.execute("SELECT rule_id, fact FROM rule_inputs ORDER BY rule_id, fact")
    for rid, fact in cur.fetchall():
        inputs_map[rid].append(fact)

    cur.execute("SELECT rule_id, fact FROM rule_outputs ORDER BY rule_id, fact")
    for rid, fact in cur.fetchall():
        outputs_map[rid].append(fact)

    rules: List[Rule] = []
    for rid, raw_chain_text, use_count, first_seen, last_used, sig in rule_rows:
        rules.append(Rule(
            rule_id=rid,
            raw_chain_text=raw_chain_text,
            inputs=tuple(inputs_map.get(rid, [])),
            outputs=tuple(outputs_map.get(rid, [])),
            use_count=use_count or 1,
            first_seen=first_seen,
            last_used=last_used,
            signature=sig,
        ))
    return rules


def build_indexes(rules: List[Rule]):
    rules_by_output: Dict[str, List[Rule]] = defaultdict(list)
    rules_by_input_fact: Dict[str, List[Rule]] = defaultdict(list)

    for r in rules:
        for out_fact in r.outputs:
            rules_by_output[out_fact].append(r)
        for in_fact in r.inputs:
            rules_by_input_fact[in_fact].append(r)

    for fact in rules_by_output:
        rules_by_output[fact].sort(key=lambda x: (-x.use_count, x.rule_id))
    for fact in rules_by_input_fact:
        rules_by_input_fact[fact].sort(key=lambda x: (-x.use_count, x.rule_id))

    return rules_by_output, rules_by_input_fact


# ============================================================
# 规则过滤：action_* 终态化
# ============================================================

def rule_is_usable(rule: Rule) -> bool:
    """
    关键约束：
    任何输入里包含 action_* 的规则都不参与推理。
    action_* 只能做输出终点，不能继续作为中间前提。
    """
    return not any(is_action_fact(inp) for inp in rule.inputs)


# ============================================================
# 前向闭包
# ============================================================

def forward_closure(input_facts: Set[str], rules: List[Rule]) -> Tuple[Set[str], Set[int], Dict[int, int]]:
    known = set(normalize_fact(x) for x in input_facts if normalize_fact(x))
    activated_rules: Set[int] = set()
    rule_activation_round: Dict[int, int] = {}

    usable_rules = [r for r in rules if rule_is_usable(r)]

    round_idx = 0
    while True:
        round_idx += 1
        new_fact_added = False
        any_rule_activated_this_round = False

        for r in usable_rules:
            if r.rule_id in activated_rules:
                continue

            if set(r.inputs).issubset(known):
                activated_rules.add(r.rule_id)
                rule_activation_round[r.rule_id] = round_idx
                any_rule_activated_this_round = True

                before_size = len(known)
                known.update(r.outputs)
                if len(known) > before_size:
                    new_fact_added = True

        if not any_rule_activated_this_round and not new_fact_added:
            break
        if any_rule_activated_this_round and not new_fact_added:
            break

    return known, activated_rules, rule_activation_round


# ============================================================
# SCC / Fact 图
# ============================================================

def build_active_fact_graph(
    active_rules: List[Rule],
    known_facts: Set[str]
) -> Tuple[Dict[str, Set[str]], List[Tuple[str, str, int]]]:
    """
    fact 图：inp_fact -> out_fact
    由于 action_* 规则输入已被禁止，这里不会再出现 action_* 作为源点的边。
    """
    graph: Dict[str, Set[str]] = defaultdict(set)
    edges_with_rule: List[Tuple[str, str, int]] = []

    for f in known_facts:
        graph.setdefault(f, set())

    for r in active_rules:
        ins = [i for i in r.inputs if i in known_facts]
        outs = [o for o in r.outputs if o in known_facts]
        for i in ins:
            for o in outs:
                graph[i].add(o)
                edges_with_rule.append((i, o, r.rule_id))

    return graph, edges_with_rule


def tarjan_scc(graph: Dict[str, Set[str]]) -> Tuple[List[List[str]], Dict[str, int]]:
    index = 0
    stack: List[str] = []
    onstack: Set[str] = set()
    indices: Dict[str, int] = {}
    lowlink: Dict[str, int] = {}
    sccs: List[List[str]] = []
    comp_id: Dict[str, int] = {}

    sys.setrecursionlimit(1000000)

    def strongconnect(v: str):
        nonlocal index
        indices[v] = index
        lowlink[v] = index
        index += 1
        stack.append(v)
        onstack.add(v)

        for w in graph.get(v, set()):
            if w not in indices:
                strongconnect(w)
                lowlink[v] = min(lowlink[v], lowlink[w])
            elif w in onstack:
                lowlink[v] = min(lowlink[v], indices[w])

        if lowlink[v] == indices[v]:
            comp = []
            while True:
                w = stack.pop()
                onstack.remove(w)
                comp_id[w] = len(sccs)
                comp.append(w)
                if w == v:
                    break
            sccs.append(sorted(comp))

    for v in sorted(graph.keys()):
        if v not in indices:
            strongconnect(v)

    return sccs, comp_id


def build_component_dag(
    graph: Dict[str, Set[str]],
    sccs: List[List[str]],
    comp_id: Dict[str, int]
) -> Tuple[Dict[int, Set[int]], Dict[int, int]]:
    comp_graph: Dict[int, Set[int]] = defaultdict(set)
    indeg: Dict[int, int] = {i: 0 for i in range(len(sccs))}

    for u, nbrs in graph.items():
        cu = comp_id[u]
        for v in nbrs:
            cv = comp_id[v]
            if cu != cv and cv not in comp_graph[cu]:
                comp_graph[cu].add(cv)
                indeg[cv] += 1

    for i in range(len(sccs)):
        comp_graph.setdefault(i, set())
        indeg.setdefault(i, 0)

    return comp_graph, indeg


def topo_sort_components(comp_graph: Dict[int, Set[int]], indeg: Dict[int, int]) -> List[int]:
    q = deque(sorted([c for c, d in indeg.items() if d == 0]))
    order = []
    indeg_local = dict(indeg)

    while q:
        c = q.popleft()
        order.append(c)
        for nxt in sorted(comp_graph[c]):
            indeg_local[nxt] -= 1
            if indeg_local[nxt] == 0:
                q.append(nxt)

    if len(order) != len(comp_graph):
        raise RuntimeError("组件 DAG 拓扑排序失败。")
    return order


# ============================================================
# 精确引擎：SCC + bitset + DP
# ============================================================

class ExactReasoningEngine:
    """
    精确定义：
    - 输入 fact 视为已知叶子，不反推
    - action_* 强制终态，不允许作为规则输入
    - 一条链按 rule_id 集合定义
    - 同一组 rule_id 只算 1 条
    - 顺序不同不重复
    - SCC 外按 DAG DP
    - SCC 内按 simple internal fact derivation 精确枚举
    """

    def __init__(self, rules: List[Rule], input_facts: Set[str], verbose: bool = False):
        self.verbose = verbose
        self.rules = rules
        self.input_facts = frozenset(normalize_fact(x) for x in input_facts if normalize_fact(x))
        self.rules_by_output, self.rules_by_input_fact = build_indexes(rules)

        self.known_facts, self.activated_rule_ids, self.rule_activation_round = forward_closure(set(self.input_facts), rules)
        self.active_rules = [r for r in rules if r.rule_id in self.activated_rule_ids and rule_is_usable(r)]

        self.graph, self.edges_with_rule = build_active_fact_graph(self.active_rules, self.known_facts)
        self.sccs, self.comp_id = tarjan_scc(self.graph)
        self.comp_graph, self.comp_indeg = build_component_dag(self.graph, self.sccs, self.comp_id)
        self.comp_topo = topo_sort_components(self.comp_graph, self.comp_indeg)

        self.active_rules_sorted = sorted(self.active_rules, key=lambda r: r.rule_id)
        self.rule_index: Dict[int, int] = {r.rule_id: idx for idx, r in enumerate(self.active_rules_sorted)}
        self.rule_by_id: Dict[int, Rule] = {r.rule_id: r for r in self.active_rules_sorted}

        # fact -> set[int bitmask]
        self.support_sets: Dict[str, Set[int]] = {}

        self.rules_by_output_active: Dict[str, List[Rule]] = defaultdict(list)
        for r in self.active_rules_sorted:
            for out in r.outputs:
                if out in self.known_facts:
                    self.rules_by_output_active[out].append(r)

        if self.verbose:
            cyclic_components = [cid for cid, comp in enumerate(self.sccs) if self._is_cyclic_component(cid)]
            print(f"[INFO] 输入事实数: {len(self.input_facts)}")
            print(f"[INFO] 可达事实数: {len(self.known_facts)}")
            print(f"[INFO] 激活规则数: {len(self.active_rules)}")
            print(f"[INFO] SCC 数量: {len(self.sccs)}")
            print(f"[INFO] 循环 SCC 数量: {len(cyclic_components)}")
            if cyclic_components:
                print("[INFO] 循环 SCC 明细:")
                for cid in cyclic_components:
                    print(f"  SCC#{cid}: {self.sccs[cid]}")

        self._run_component_dp()

    def _rule_bit(self, rule_id: int) -> int:
        return 1 << self.rule_index[rule_id]

    def is_result_fact(self, fact: str) -> bool:
        return is_action_fact(fact)

    def get_final_results(self) -> List[str]:
        return sorted([f for f in self.known_facts if self.is_result_fact(f)])

    def _is_cyclic_component(self, cid: int) -> bool:
        comp = self.sccs[cid]
        if len(comp) > 1:
            return True
        f = comp[0]
        return f in self.graph.get(f, set())

    def _combine_masks(self, list_of_sets: List[Set[int]]) -> Set[int]:
        if not list_of_sets:
            return {0}
        parts = sorted(list_of_sets, key=lambda s: len(s))
        current = {0}
        for sset in parts:
            nxt = set()
            for base in current:
                for m in sset:
                    nxt.add(base | m)
            current = nxt
        return current

    def _run_component_dp(self) -> None:
        for pos, cid in enumerate(self.comp_topo, start=1):
            if self.verbose:
                print(f"[COMP] 处理组件 {pos}/{len(self.comp_topo)}: SCC#{cid} -> {self.sccs[cid]}")
            if self._is_cyclic_component(cid):
                self._solve_cyclic_component(cid)
            else:
                self._solve_acyclic_component(cid)

    def _solve_acyclic_component(self, cid: int) -> None:
        fact = self.sccs[cid][0]

        if fact in self.input_facts:
            self.support_sets[fact] = {0}
            return

        all_masks: Set[int] = set()

        for r in self.rules_by_output_active.get(fact, []):
            if fact in r.inputs:
                continue

            input_support_sets: List[Set[int]] = []
            ok = True
            for inp in r.inputs:
                if inp not in self.known_facts:
                    ok = False
                    break
                ss = self.support_sets.get(inp)
                if ss is None:
                    ok = False
                    break
                input_support_sets.append(ss)

            if not ok:
                continue

            combined = self._combine_masks(input_support_sets)
            rbit = self._rule_bit(r.rule_id)
            for m in combined:
                all_masks.add(m | rbit)

        self.support_sets[fact] = all_masks

    def _solve_cyclic_component(self, cid: int) -> None:
        comp_facts = self.sccs[cid]
        comp_fact_set = set(comp_facts)
        memo: Dict[Tuple[str, Tuple[str, ...]], Set[int]] = {}

        def solve_fact(target_fact: str, visiting: Tuple[str, ...]) -> Set[int]:
            key = (target_fact, visiting)
            if key in memo:
                return memo[key]

            if target_fact in self.input_facts:
                memo[key] = {0}
                return memo[key]

            visiting_set = set(visiting)
            if target_fact in visiting_set:
                memo[key] = set()
                return memo[key]

            next_visiting = tuple(sorted(visiting_set | {target_fact}))
            all_masks: Set[int] = set()

            for r in self.rules_by_output_active.get(target_fact, []):
                input_support_sets: List[Set[int]] = []
                ok = True

                for inp in r.inputs:
                    if inp not in self.known_facts:
                        ok = False
                        break

                    if inp in comp_fact_set:
                        ss = solve_fact(inp, next_visiting)
                    else:
                        ss = self.support_sets.get(inp)

                    if not ss:
                        ok = False
                        break

                    input_support_sets.append(ss)

                if not ok:
                    continue

                combined = self._combine_masks(input_support_sets)
                rbit = self._rule_bit(r.rule_id)
                for m in combined:
                    all_masks.add(m | rbit)

            memo[key] = all_masks
            return memo[key]

        for fact in comp_facts:
            self.support_sets[fact] = solve_fact(fact, tuple())

    def count_ways_for_fact(self, fact: str) -> int:
        fact = normalize_fact(fact)
        return len(self.support_sets.get(fact, set()))

    def decode_mask(self, mask: int) -> List[int]:
        rule_ids = []
        for rid, idx in self.rule_index.items():
            if (mask >> idx) & 1:
                rule_ids.append(rid)
        rule_ids.sort()
        return rule_ids

    def explain_mask(self, mask: int) -> Dict[str, object]:
        rule_ids = self.decode_mask(mask)
        rules = [self.rule_by_id[rid] for rid in rule_ids]
        return {
            "rule_count": len(rule_ids),
            "rule_ids": rule_ids,
            "rules": [
                {
                    "rule_id": r.rule_id,
                    "use_count": r.use_count,
                    "inputs": list(r.inputs),
                    "outputs": list(r.outputs),
                    "rule_text": r.raw_chain_text,
                }
                for r in rules
            ]
        }

    def summarize(self) -> Dict[str, object]:
        final_results = self.get_final_results()
        result_counts = {
            fact: self.count_ways_for_fact(fact) for fact in final_results
        }

        cyclic_sccs = []
        for cid, comp in enumerate(self.sccs):
            # 不把纯 action 终点单独当作“异常”，因为现在它们不应再形成环。
            if self._is_cyclic_component(cid):
                cyclic_sccs.append({
                    "scc_id": cid,
                    "facts": comp
                })

        return {
            "input_facts": sorted(self.input_facts),
            "reachable_facts_count": len(self.known_facts),
            "reachable_facts": sorted(self.known_facts),
            "activated_rules_count": len(self.activated_rule_ids),
            "final_results_count": len(final_results),
            "final_results": result_counts,
            "total_path_count": sum(result_counts.values()),
            "cyclic_sccs": cyclic_sccs,
        }

    def export_single_result_supports(self, target_fact: str, output_path: str) -> None:
        target_fact = normalize_fact(target_fact)
        masks = sorted(self.support_sets.get(target_fact, set()), key=lambda x: (x.bit_count(), x))

        payload = {
            "input_facts": sorted(self.input_facts),
            "target_fact": target_fact,
            "support_set_count": len(masks),
            "support_sets": [self.explain_mask(m) for m in masks],
        }

        with open(output_path, "w", encoding="utf-8") as f:
            json.dump(payload, f, ensure_ascii=False, indent=2)

    def export_all_results_supports(self, output_path: str) -> None:
        final_results = self.get_final_results()
        payload = {
            "input_facts": sorted(self.input_facts),
            "reachable_facts_count": len(self.known_facts),
            "activated_rules_count": len(self.activated_rule_ids),
            "final_results_count": len(final_results),
            "results": {}
        }

        for fact in final_results:
            masks = sorted(self.support_sets.get(fact, set()), key=lambda x: (x.bit_count(), x))
            payload["results"][fact] = {
                "support_set_count": len(masks),
                "support_sets": [self.explain_mask(m) for m in masks],
            }

        with open(output_path, "w", encoding="utf-8") as f:
            json.dump(payload, f, ensure_ascii=False, indent=2)


# ============================================================
# 输出
# ============================================================

def print_summary(summary: Dict[str, object], show_reachable_facts: bool = False) -> None:
    print("\n=== 推理摘要 ===")
    print("输入事实数:", len(summary["input_facts"]))
    print("可达事实总数:", summary["reachable_facts_count"])
    print("激活规则总数:", summary["activated_rules_count"])
    print("最终结果数:", summary["final_results_count"])
    print("最终结果对应不同规则集合总数:", summary["total_path_count"])

    print("\n输入事实:")
    for fact in summary["input_facts"]:
        print(" -", fact)

    cyclic_sccs = summary.get("cyclic_sccs", [])
    if cyclic_sccs:
        print("\n循环 SCC:")
        for item in cyclic_sccs:
            print(f" - SCC#{item['scc_id']}: {', '.join(item['facts'])}")

    print("\n最终结果:")
    final_results: Dict[str, int] = summary["final_results"]  # type: ignore
    if not final_results:
        print(" (无)")
    else:
        for fact, cnt in sorted(final_results.items(), key=lambda x: (-x[1], x[0])):
            print(f" - {fact}: {cnt} 条不同规则集合链路")

    if show_reachable_facts:
        print("\n可达事实:")
        for fact in summary["reachable_facts"]:
            print(" -", fact)


# ============================================================
# CLI
# ============================================================

def parse_facts_arg(facts_arg: str) -> Set[str]:
    facts = set()
    for x in facts_arg.split(","):
        x = normalize_fact(x)
        if x:
            facts.add(x)
    return facts


def main():
    parser = argparse.ArgumentParser(description="SQLite 精确推理链计数引擎（action终态 + SCC + bitset + DP）")
    parser.add_argument("--db", required=True, help="SQLite 数据库文件路径")
    parser.add_argument("--sync", action="store_true", help="将 atomic_chains 同步到规范化表")
    parser.add_argument("--facts", type=str, default="", help="输入事实，逗号分隔")
    parser.add_argument("--show-reachable-facts", action="store_true", help="打印所有可达事实")
    parser.add_argument("--json", action="store_true", help="输出 JSON 摘要")
    parser.add_argument("--target", type=str, default="", help="目标结果 fact")
    parser.add_argument("--export-target-supports", type=str, default="", help="导出指定目标结果的全部精确规则集合")
    parser.add_argument("--export-all-supports", type=str, default="", help="导出所有最终结果的全部精确规则集合")
    parser.add_argument("--verbose", action="store_true", help="打印进度日志")
    args = parser.parse_args()

    conn = sqlite3.connect(args.db)
    conn.row_factory = sqlite3.Row

    try:
        validate_atomic_chains_exists(conn)

        if args.sync:
            sync_from_atomic_chains(conn, verbose=True)
            if not args.facts.strip():
                print("[INFO] 同步完成。未提供 --facts，程序退出。")
                return

        ensure_normalized_tables(conn)
        cur = conn.cursor()
        cur.execute("SELECT COUNT(*) FROM rules")
        rule_count = cur.fetchone()[0]
        if rule_count == 0:
            print("[INFO] rules 表为空，自动从 atomic_chains 同步...")
            sync_from_atomic_chains(conn, verbose=True)

        rules = load_rules(conn)
        if not rules:
            print("没有可用规则。")
            return

        if not args.facts.strip():
            print("未提供 --facts，已完成规则同步/加载。")
            return

        input_facts = parse_facts_arg(args.facts)
        if not input_facts:
            print("输入 facts 为空。")
            return

        if args.verbose:
            print("[INFO] 开始构建精确推理引擎...")

        engine = ExactReasoningEngine(rules, input_facts, verbose=args.verbose)

        if args.verbose:
            print("[INFO] 开始汇总结果...")

        summary = engine.summarize()

        if args.json:
            print(json.dumps(summary, ensure_ascii=False, indent=2))
        else:
            print_summary(summary, show_reachable_facts=args.show_reachable_facts)

        if args.export_target_supports:
            if not args.target.strip():
                raise ValueError("使用 --export-target-supports 时必须同时提供 --target")
            engine.export_single_result_supports(args.target, args.export_target_supports)
            print(f"\n已导出目标 {normalize_fact(args.target)} 的全部精确规则集合到: {args.export_target_supports}")

        if args.export_all_supports:
            engine.export_all_results_supports(args.export_all_supports)
            print(f"\n已导出所有最终结果的全部精确规则集合到: {args.export_all_supports}")

    finally:
        conn.close()


if __name__ == "__main__":
    main()