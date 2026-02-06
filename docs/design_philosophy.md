# Design Philosophy: Explicit Semantics Over Convenience

## Problem Statement

Distributed simulation failures are often caused by implicit assumptions.
Hidden defaults and conflated responsibilities make behavior hard to audit and harder to reproduce.
When causality matters, ambiguity becomes a defect.

## Core Principle

Semantics must be explicitly declared, not inferred:

- **Data lifetime and overwrite rules** (cache)
- **Delivery guarantees and failure modes** (communication)
- **Meaning of bytes** (PDU definition)

Configuration is treated as part of the system specification, not a convenience layer.

## Why the Design Is Strict

This project chooses strictness by design:

- **Multiple configuration files** separate independent semantic decisions.
- **Cache / Communication / PDU Definition** are split to avoid hidden coupling.
- **Runtime configuration** enables switching behavior without recompilation, which is required in multi-asset simulations.

## Acknowledged Costs

These costs are accepted and intentional:

- Higher learning curve
- Longer path to “Hello World”
- Increased configuration surface area
- Requirement for tooling (generator / validator)

## Why These Costs Are Acceptable

The trade-off is justified when systems must remain explainable years later:

- Long-lived systems that outlive their original developers
- Multi-asset simulations with complex interdependencies
- Regulatory, safety, or reproducibility constraints
- Teams that require auditability and explicit causality

## What This Component Is NOT

- Not a general-purpose messaging library
- Not optimized for quick prototypes
- Not designed for implicit defaults

## Scope Boundary

Use this component when you need explicit, auditable semantics and long-term maintainability in distributed simulation.
Choose a simpler tool when you prioritize rapid setup, implicit defaults, or single-purpose messaging.
