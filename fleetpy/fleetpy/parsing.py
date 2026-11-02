"""
Programs as text.

Two forms exist:
  printed    what Fleet prints: ``and(blue(x),not(circle(x)))``. Unambiguous when every rule
             format is canonical (``rules.is_canonical_format``).
  parseable  Fleet's serialization: preorder ``nt:format`` tokens joined by ``;``. Exact for
             any formats.

Everything here works on a Grammar alone and is checked in Python, because Fleet's own
from_parseable reports malformed input with asserts that are compiled out under -DNDEBUG.
"""

from __future__ import annotations

import re

from .rules import default_format

_TOKEN = re.compile(r"\s*([^(),\s]+|\(|\)|,)")


def parse_tree(s: str):
    """A printed program -> (name, None | [subtrees]). Strict: rejects anything else."""
    s = s.strip()
    for prefix in ("λx.", "lambda x."):
        if s.startswith(prefix):
            s = s[len(prefix):]
    toks, pos = [], 0
    while pos < len(s):
        m = _TOKEN.match(s, pos)
        if not m or m.end() == pos:
            raise ValueError(f"cannot parse {s!r} at position {pos}: {s[pos:pos + 20]!r}")
        toks.append(m.group(1))
        pos = m.end()
        while pos < len(s) and s[pos].isspace():
            pos += 1
    i = 0

    def node():
        nonlocal i
        if i >= len(toks) or toks[i] in ("(", ")", ","):
            raise ValueError(f"expected a name in {s!r}")
        name = toks[i]
        i += 1
        if i >= len(toks) or toks[i] != "(":
            return (name, None)
        i += 1
        if i < len(toks) and toks[i] == ")":   # f()
            i += 1
            return (name, [])
        args = [node()]
        while i < len(toks) and toks[i] == ",":
            i += 1
            args.append(node())
        if i >= len(toks) or toks[i] != ")":
            raise ValueError(f"expected ')' in {s!r}")
        i += 1
        return (name, args)
    t = node()
    if i != len(toks):
        raise ValueError(f"trailing input in {s!r}: {toks[i:]}")
    return t


def printed_to_parseable(grammar, s: str) -> str:
    """A printed program of the grammar's output type -> Fleet's parseable form, matching
    every node to a rule of exactly the type needed there."""
    if not grammar.canonical:
        raise ValueError(f"parse() needs canonical formats; these are not: {grammar.noncanonical}. "
                         f"Use serialize()/deserialize() instead.")

    def convert(tree, want):
        name, args = tree
        fmt = name if args is None else (name + "()" if not args else default_format(name, len(args)))
        r = grammar.rule(fmt, want)
        if r.arity != len(args or []):
            raise ValueError(f"{fmt}: wrong number of arguments")
        return ";".join([f"{grammar.nt(r.ret)}:{fmt}"] + [convert(a, t) for a, t in zip(args or [], r.args)])
    return convert(parse_tree(s), grammar.output)


def check_parseable(grammar, p: str):
    """Raise unless ``p`` is a well-typed parseable program of the grammar's output type."""
    toks, pos = p.split(";"), 0

    def node(want):
        nonlocal pos
        if pos >= len(toks):
            raise ValueError(f"truncated program {p!r}")
        nt, _, fmt = toks[pos].partition(":")
        pos += 1
        r = grammar.rule(fmt, want)
        if str(grammar.nt(want)) != nt:
            raise ValueError(f"{fmt!r}: nonterminal {nt} does not match type {want.key}")
        for a in r.args:
            node(a)
    node(grammar.output)
    if pos != len(toks):
        raise ValueError(f"trailing input in program {p!r}")


def parseable_rules(p: str):
    """A parseable program -> its rules in preorder, as (nonterminal, format)."""
    return [(int(nt), fmt) for nt, _, fmt in (t.partition(":") for t in p.split(";"))]
