from __future__ import annotations

import json
import re
from pathlib import Path
from typing import Optional

from schema import Scenario, Decision
from prompts import SYSTEM_PROMPT, build_user_prompt


class Qwen3GGUFRunner:
    def __init__(
        self,
        model_path: str,
        ctx_size: int = 2048,
        threads: int = 8,
        temperature: float = 0.0,
        max_tokens: int = 80,
        n_gpu_layers: int = 0,
    ):
        self.model_path = model_path
        self.ctx_size = ctx_size
        self.threads = threads
        self.temperature = temperature
        self.max_tokens = max_tokens
        self.n_gpu_layers = n_gpu_layers
        self.llm = None

    def load(self):
        if self.llm is not None:
            return
        from llama_cpp import Llama

        mp = Path(self.model_path)
        if not mp.exists():
            raise FileNotFoundError(
                f"GGUF model not found: {mp}. Please pass a valid --model_path."
            )

        self.llm = Llama(
            model_path=str(mp),
            n_ctx=self.ctx_size,
            n_threads=self.threads,
            n_gpu_layers=self.n_gpu_layers,
            verbose=False,
        )

    def generate_text(self, case: Scenario) -> str:
        self.load()
        prompt = (
            "<|im_start|>system\n" + SYSTEM_PROMPT + "\n<|im_end|>\n"
            "<|im_start|>user\n" + build_user_prompt(case) + "\n<|im_end|>\n"
            "<|im_start|>assistant\n"
        )

        out = self.llm(
            prompt,
            max_tokens=self.max_tokens,
            temperature=self.temperature,
            stop=["<|im_end|>", "\nInput:", "\nOutput:", "\n\nExample"],
        )
        return out["choices"][0]["text"].strip()


def _extract_json_block(text: str) -> Optional[dict]:
    # 1. direct parse
    try:
        return json.loads(text)
    except Exception:
        pass

    # 2. first json object block
    match = re.search(r"\{.*\}", text, flags=re.DOTALL)
    if match:
        block = match.group(0)
        try:
            return json.loads(block)
        except Exception:
            pass

    # 3. salvage
    selected = None
    action = None
    reason_code = None

    m1 = re.search(r'"selected_target"\s*:\s*(null|"[^"]+")', text)
    if m1:
        val = m1.group(1)
        selected = None if val == "null" else val.strip('"')

    m2 = re.search(r'"action"\s*:\s*"([^"]+)"', text)
    if m2:
        action = m2.group(1)

    m3 = re.search(r'"reason_code"\s*:\s*"([^"]+)"', text)
    if m3:
        reason_code = m3.group(1)

    if action is not None:
        return {
            "selected_target": selected,
            "action": action,
            "reason_code": reason_code,
        }

    return None


def _sanitize_decision(case: Scenario, parsed: Optional[dict], raw_text: str) -> Decision:
    """
    Important design:
    - qwen3_only should preserve model errors as much as possible
    - only fix gross format failures
    - do NOT enforce executability or priority logic here
    """
    if not parsed:
        return Decision(
            selected_target=None,
            action="wait",
            reason_code="PARSE_FAIL_WAIT",
            raw_text=raw_text,
        )

    selected = parsed.get("selected_target")
    action = parsed.get("action", "wait")
    reason_code = parsed.get("reason_code", "MODEL_OUTPUT")

    if action not in {"observe", "wait"}:
        return Decision(
            selected_target=None,
            action="wait",
            reason_code="INVALID_ACTION_WAIT",
            raw_text=raw_text,
        )

    if action == "wait":
        return Decision(
            selected_target=None,
            action="wait",
            reason_code=reason_code,
            raw_text=raw_text,
        )

    # observe branch
    return Decision(
        selected_target=selected,
        action="observe",
        reason_code=reason_code,
        raw_text=raw_text,
    )


def run_qwen3_only(case: Scenario, runner: Qwen3GGUFRunner) -> Decision:
    raw = runner.generate_text(case)
    parsed = _extract_json_block(raw)
    return _sanitize_decision(case, parsed, raw)