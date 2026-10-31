"""
How to run inference: the sampler and Fleet's Control (budgets, threads, seed).
"""

from __future__ import annotations

import dataclasses
import re

_TIME = re.compile(r"^\s*([0-9]*\.?[0-9]+)\s*([smhd]?)\s*$")
_UNIT_MS = {"s": 1000, "m": 60_000, "h": 3_600_000, "d": 86_400_000}


def parse_time_ms(t) -> int:
    """'30s', '2m', '1.5h', 10 (seconds) -> milliseconds, with Fleet's smhd units."""
    if t is None or t == 0:
        return 0
    if isinstance(t, (int, float)) and not isinstance(t, bool):
        if t < 0:
            raise ValueError("time must be >= 0")
        return int(round(t * 1000))
    m = _TIME.match(str(t))
    if not m:
        raise ValueError(f"cannot parse time {t!r}; use e.g. '30s', '2m', '1h', '1d'")
    return int(round(float(m.group(1)) * _UNIT_MS[m.group(2) or "s"]))


class Control:
    """Fleet's Control, plus the FleetArgs that matter for a run.

    steps=0 and time=0 together mean "run until Ctrl-C", as in Fleet; fleetpy refuses that
    unless forever=True, because it is almost always a mistake in a script.
    """

    def __init__(self, steps: int = 0, time=0, threads: int = 1, restart: int = 0, seed: int = 0,
                 max_nodes: int = 64, forever: bool = False):
        if not isinstance(steps, int) or steps < 0:
            raise ValueError("steps must be an int >= 0")
        if not isinstance(threads, int) or threads < 1:
            raise ValueError("threads must be an int >= 1")
        if not isinstance(seed, int) or not 0 <= seed < 2**64:
            raise ValueError("seed must be an int in [0, 2^64); 0 means unseeded")
        self.steps, self.time_ms, self.threads = steps, parse_time_ms(time), threads
        self.restart, self.seed, self.max_nodes = int(restart), seed, int(max_nodes)
        if steps == 0 and self.time_ms == 0 and not forever:
            raise ValueError("Control: give steps= or time= (or forever=True to run until Ctrl-C)")

    def __repr__(self):
        return (f"Control(steps={self.steps}, time_ms={self.time_ms}, threads={self.threads}, "
                f"restart={self.restart}, seed={self.seed}, max_nodes={self.max_nodes})")


@dataclasses.dataclass(frozen=True)
class Sampler:
    """One of Fleet's samplers. ``algo`` is the runtime's code for it."""
    algo: int
    chains: int = 1
    max_temp: float = 1.0

    def cpp(self, h0: str, data: str) -> str:
        """The C++ that constructs this sampler as ``samp`` (for ejected models)."""
        return [f"MCMCChain samp({h0}, {data});",
                f"ChainPool samp({h0}, {data}, {self.chains});",
                f"ParallelTempering samp({h0}, {data}, {self.chains}, {self.max_temp!r});"][self.algo]


def MCMCChain() -> Sampler:
    """A single chain (Fleet asserts it runs with threads=1)."""
    return Sampler(0)


def ChainPool(chains: int) -> Sampler:
    if not (isinstance(chains, int) and chains >= 1):
        raise ValueError("chains must be an int >= 1")
    return Sampler(1, chains)


def ParallelTempering(chains: int, max_temp: float) -> Sampler:
    if not (isinstance(chains, int) and chains >= 1) or not max_temp >= 1.0:
        raise ValueError("ParallelTempering needs chains >= 1 and max_temp >= 1")
    return Sampler(2, chains, float(max_temp))
