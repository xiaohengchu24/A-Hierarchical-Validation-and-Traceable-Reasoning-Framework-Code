# A-Hierarchical-Validation-and-Traceable-Reasoning-Framework-Code
A Hierarchical Validation and Traceable Reasoning Framework for Improving the Executability of AI-driven Telescope Scheduling Code
## 📄 Overview

This repository contains the implementation code accompanying the paper:

> **"A Hierarchical Validation and Traceable Reasoning Framework for Improving the Executability of AI-driven Telescope Scheduling"**

This work addresses a critical bottleneck in applying Large Language Models (LLMs) to astronomical telescope scheduling: the **reliability and auditability of AI-generated decisions**. While LLMs excel at handling complex, multi-constraint scenarios, their tendency toward "hallucination" and opaque reasoning processes limits their deployment in high-stakes observational environments.

We propose a framework that sits between LLM output generation and telescope command execution, providing:

1.  **Multi-level Hierarchical Validation:** Systematic verification of data consistency, physical constraints, equipment states, and computational correctness.
2.  **Traceable Reasoning via Atomic Units:** Decomposition of AI inference into reusable, structured **Atomic Reasoning Units (ARUs)** and visualization through **Directed Acyclic Graphs (DAGs)** .
3.  **Closed-loop Error Feedback:** Automatic blocking of unexecutable suggestions with structured feedback for model correction.

The code in this repository was used to generate the experimental results (Figures 4–8) presented in the paper.

## 📊 Key Findings (Reproduced from Paper)

- **Executable Pass Rate:** Average rates of **84.5%** (Qwen 30B) and **79.3%** (Phi-4 14B) across 2,000+ simulated ToO scenarios.
- **Inference Reusability:** Average **80%** hit rate for high-frequency atomic reasoning items; only **~104** unique atomic terms activated across >7,000 inference paths.
- **Validation Efficacy:** Multi-level checks significantly reduce unexecutable scheduling attempts, particularly filtering out violations of physical constraints and equipment anomalies.
