# ZTF-driven telescope scheduling experiments

This repository contains the implementation of the telescope scheduling experiments based on real ZTF alert data. The code is designed to support the experimental setup described in the manuscript, including scenario construction, baseline methods, and AI-based scheduling approaches.

The main objectives of this implementation are:

- To use real ZTF `.avro` alerts as the source of candidate targets  
- To preserve the scenario definitions (S1–S6) described in the manuscript  
- To include multiple non-AI baseline methods for comparison  
- To evaluate both `qwen3_only` and `qwen3_framework` settings  
- To support local inference using GGUF models via `llama-cpp-python`  

---

## Methods

### Non-AI baselines

- `baseline_rule_priority`
- `baseline_rule_deadline`
- `baseline_validated_heuristic`
- `baseline_weighted_heuristic`

### AI-based methods

- `qwen3_only`
- `qwen3_framework`

---

## Repository structure

- `schema.py` — unified data structures  
- `ztf_loader.py` — loading and filtering ZTF alert data  
- `generate_cases.py` — generation of S1–S6 scheduling scenarios  
- `methods_baseline.py` — implementation of baseline methods  
- `prompts.py` — prompt templates for Qwen3  
- `methods_qwen3.py` — model loading and inference (GGUF)  
- `methods_framework.py` — validation framework and correction logic  
- `evaluate.py` — evaluation metrics and error analysis  
- `run_all.py` — main entry point for experiments  

---

## Installation

```bash
pip install -r requirements.txt
