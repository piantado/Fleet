"""
Code generation: spec -> one C++ translation unit.

The model part (structs, primitives, grammar, hypothesis) is identical in the Python module
and in an ejected Main.cpp; only the trailer differs (nanobind bindings vs. main()).
"""

from __future__ import annotations

import json
import math

from . import ftypes as T
from .grammar import Builtin, Primitive, Terminal

GRAMMAR_CLASS = "MyGrammar"


def _cpp_string_literal(s: str) -> str:
    out = ['"']
    for ch in s:
        o = ord(ch)
        if ch == '"':
            out.append('\\"')
        elif ch == "\\":
            out.append("\\\\")
        elif 32 <= o < 127:
            out.append(ch)
        else:
            # emit UTF-8 bytes as octal escapes (unambiguous, unlike \x which is greedy)
            for b in ch.encode("utf-8"):
                out.append("\\%03o" % b)
    out.append('"')
    return "".join(out)


def cpp_literal(t: T.FType, v) -> str:
    """Render a Python value of fleetpy type ``t`` as a C++ expression of type ``t.cpp``."""
    if isinstance(t, T._Bool):
        t.to_cpp(v, None)
        return "true" if v else "false"
    if isinstance(t, T._Int):
        t.to_cpp(v, None)
        if v == t.lo:  # the literal -2^(n-1) does not exist in C++ (it is -(2^(n-1)))
            return f"std::numeric_limits<{t.cpp}>::min()"
        return f"{t.cpp}({v}LL)"
    if isinstance(t, T._Float):
        v = t.to_cpp(v, None)
        if math.isnan(v):
            return "std::numeric_limits<double>::quiet_NaN()"
        if math.isinf(v):
            return ("" if v > 0 else "-") + "std::numeric_limits<double>::infinity()"
        return float.hex(v).join(["double(", ")"])  # hex float literal: exact
    if isinstance(t, T._Char):
        t.to_cpp(v, None)
        return f"char({ord(v)})"
    if isinstance(t, T._Str):
        t.to_cpp(v, None)
        return f"std::string({_cpp_string_literal(v)}, {len(v.encode('utf-8'))})"
    if isinstance(t, T.ListOf):
        return f"{t.cpp}{{" + ", ".join(cpp_literal(t.elem, x) for x in v) + "}"
    if isinstance(t, T.TupleOf):
        return f"{t.cpp}{{" + ", ".join(cpp_literal(e, x) for e, x in zip(t.elems, v)) + "}"
    if isinstance(t, T.SetOf):
        return f"{t.cpp}{{" + ", ".join(cpp_literal(t.elem, x) for x in sorted(v)) + "}"
    if isinstance(t, T.StructType):
        if not isinstance(v, t.pycls):
            raise TypeError(f"expected {t.name}, got {v!r}")
        return f"{t.name}(" + ", ".join(cpp_literal(ft, getattr(v, n)) for n, ft in t.fields) + ")"
    if isinstance(t, T.NewType):
        return f"{t.name}({cpp_literal(t.base, v)})"
    if isinstance(t, T.OpaqueType) and t.py is not None:
        t.to_cpp(v, None)
        return f"([]() {{ const {t.py.cpp} v = {cpp_literal(t.py, v)}; return {t.cpp}({t.from_py}); }}())"
    raise TypeError(f"cannot write a C++ literal for {t!r}")


def _struct_def(s) -> str:
    if isinstance(s, T.NewType):
        b = s.base.cpp
        return (f"struct {s.name} {{\n"
                f"\t{b} v{{}};\n"
                f"\t{s.name}() = default;\n"
                f"\t{s.name}({b} x) : v(x) {{}}\n"
                f"\toperator {b}() const {{ return v; }}\n"
                f"\tauto operator<=>(const {s.name}&) const = default;\n"
                f"\tbool operator==(const {s.name}&) const = default;\n"
                f"}};\n")
    lines = [f"struct {s.name} {{"]
    for n, ft in s.fields:
        lines.append(f"\t{ft.cpp} {n}{{}};")
    lines.append(f"\t{s.name}() = default;")
    params = ", ".join(f"{ft.cpp} fp_{n}" for n, ft in s.fields)
    inits = ", ".join(f"{n}(fp_{n})" for n, _ in s.fields)
    lines.append(f"\t{s.name}({params}) : {inits} {{}}")
    lines.append(f"\tauto operator<=>(const {s.name}&) const = default;")
    lines.append(f"\tbool operator==(const {s.name}&) const = default;")
    lines.append("};")
    return "\n".join(lines) + "\n"


# Python evaluates a*b + c as two roundings; with -march=native GCC fuses it into one FMA
# (even in ISO C++ mode). Transpiled (tier-2) code must match Python bit-for-bit, so it is
# compiled without contraction. Fleet's own code and tier-1 snippets keep Fleet's flags.
_NOCONTRACT_ON = '#pragma GCC push_options\n#pragma GCC optimize ("fp-contract=off")\n'
_NOCONTRACT_OFF = '#pragma GCC pop_options\n'


def all_structs(H):
    """Grammar structs plus structs used only inside tier-2 bodies; dependencies first."""
    g = H.grammar
    out, seen = [], set()

    def push(t):
        for s in t.structs():
            if s.key not in seen:
                seen.add(s.key)
                out.append(s)
    for s in g.structs():
        push(s)
    for r in g.rules:
        for s in getattr(r, "extra_structs", []) or []:
            push(s)
    for s in getattr(H.likelihood, "extra_structs", []) or []:
        push(s)
    return out


def _prim_fn(r: Primitive, idx: int) -> str:
    params = ", ".join(f"{t.cpp} {n}" for n, t in zip(r.argnames, r.argtypes))
    head = f"static {r.rettype.cpp} {r.name}({params})"
    if r.tier == 1:
        body = r.cpp.strip()
        if "return" in body or ";" in body:
            return f"{head} {{\n{body}\n}}\n"
        return f"{head} {{ return ({body}); }}\n"
    if r.tier == 2:
        return f"{_NOCONTRACT_ON}{head} {{\n{r.transpiled}\n}}\n{_NOCONTRACT_OFF}"
    # tier 3: call back into Python, holding the GIL
    args = ", ".join(r.argnames)
    return (f"static nb::object* fp_cb_{r.name} = nullptr; // set from Python; deliberately leaked\n"
            f"{head} {{\n"
            f"\tnb::gil_scoped_acquire fp_gil;\n"
            f"\ttry {{\n"
            f"\t\tnb::object fp_r = (*fp_cb_{r.name})({args});\n"
            f"\t\treturn nb::cast<{r.rettype.cpp}>(fp_r);\n"
            f"\t}} catch (nb::python_error& fp_e) {{\n"
            f"\t\tif(fp_undefined != nullptr and fp_e.matches(*fp_undefined)) throw VMSRuntimeError();\n"
            f"\t\tthrow;\n"
            f"\t}}\n"
            f"}}\n")


def _opaque_casters(H) -> str:
    """nanobind type casters for opaque types with a Python representation. Casters are
    compile-time and local to this module, unlike nanobind's (process-wide) class registry."""
    seen, out = set(), []

    def walk(t):
        if isinstance(t, T.OpaqueType) and t.py is not None and t.cpp not in seen:
            seen.add(t.cpp)
            P = t.py.cpp
            out.append(f"""namespace nanobind {{ namespace detail {{
template <> struct type_caster<{t.cpp}> {{
	NB_TYPE_CASTER({t.cpp}, const_name("{t.cpp}"))
	bool from_python(handle src, uint8_t flags, cleanup_list* cleanup) noexcept {{
		make_caster<{P}> c;
		if(!c.from_python(src, flags_for_local_caster<{P}>(flags), cleanup) or !c.template can_cast<{P}>()) return false;
		const {P} v = c.operator cast_t<{P}>();
		try {{ value = ({t.from_py}); }} catch(...) {{ return false; }}
		return true;
	}}
	static handle from_cpp(const {t.cpp}& v, rv_policy policy, cleanup_list* cleanup) noexcept {{
		return make_caster<{P}>::from_cpp(({P})({t.to_py}), policy, cleanup);
	}}
}};
}} }}
""")
        for x in getattr(t, "elems", []) or []:
            walk(x)
        for nm in ("elem", "base"):
            if hasattr(t, nm):
                walk(getattr(t, nm))
        for _, ft in getattr(t, "fields", []) or []:
            walk(ft)
    for t in H.grammar.all_types():
        walk(t)
    return "".join(out)


# Fleet's ProgramLoader::was_called (src/VirtualMachine/Program/Program.h) has no initializer,
# so a freshly constructed hypothesis carries an indeterminate bool that copies and moves then
# read -- undefined behavior (UBSan: "load of value 16, which is not a valid value for type
# 'bool'"). This default member initializer runs in every constructor of the generated class,
# including the inherited ones, and gives it a defined value. (Upstream fix: `bool was_called = false;`.)
_WAS_CALLED_INIT = "\tbool fp_was_called_init = (this->was_called = false, true);\n"


def _kind(H):
    return H._kind  # "deterministic" | "stochastic" | "lexicon"


def _lot_base(H, cls, stochastic):
    g = H.grammar
    IN, OUT = g.input.cpp, g.output.cpp
    base = "StochasticLOTHypothesis" if stochastic else "DeterministicLOTHypothesis"
    return (f"{base}<{cls},{IN},{OUT},{GRAMMAR_CLASS},&grammar,"
            f"defaultdatum_t<{IN},{OUT}>,std::span<defaultdatum_t<{IN},{OUT}>>>")


def _fp_call_method(H):
    g = H.grammar
    IN, OUT = g.input.cpp, g.output.cpp
    return f"""
	// Like DeterministicLOTHypothesis::call, but also reports whether the program completed
	// (false on VMSRuntimeError, MAX_RECURSE or MAX_RUN_PROGRAM), so errors are never confused
	// with a legitimate output equal to the error value.
	std::pair<bool, {OUT}> fp_call(const {IN}& x) {{
		// VirtualMachineState keeps `err` by const reference, so it must outlive vms.
		const {OUT} fp_err{{}};
		VirtualMachineState_t vms(x, fp_err, nullptr);
		vms.program = this->program;   // carries program.loader (this, or the owning Lexicon)
		this->was_called = true;
		auto out = vms.run();
		this->total_instruction_count_last_call = vms.runtime_counter.total;
		this->total_vms_steps = 1;
		return std::make_pair(vms.status == vmstatus_t::COMPLETE, out);
	}}
"""


def _fp_dist_methods(H, target):
    """fp_dist (Fleet's DiscreteDistribution) and fp_logprob (normalized log P(y|x))."""
    g = H.grammar
    IN, OUT = g.input.cpp, g.output.cpp
    return f"""
	// Fleet's stochastic call: every execution path, as an unnormalized DiscreteDistribution.
	DiscreteDistribution<{OUT}> fp_dist(const {IN}& x) {{
		const {OUT} fp_err{{}};   // VMS states hold err by reference; it must outlive the call
		{target}
	}}

	// log P(output = y | x), normalized by the completed mass Z; -inf if y never occurs.
	double fp_logprob(const {IN}& x, const {OUT}& y) {{
		auto fp_d = fp_dist(x);
		if(fp_d.size() == 0 or not fp_d.contains(y)) return -infinity;
		return fp_d.get(y, -infinity) - fp_d.Z();
	}}
"""


def _propose_method(H, cls):
    prop = getattr(H, "proposal", None)
    if prop is None:
        return ""
    return f"""
	[[nodiscard]] virtual std::optional<std::pair<{cls},double>> propose() const override {{
		std::optional<std::pair<Node,double>> p;
		{{
{_indent(prop.body, 3)}
		}}
		if(not p) return {{}};
		return std::make_pair({cls}(std::move(p->first)), p->second);
	}}
"""


def _likelihood_method(H):
    tr = H.likelihood.name.startswith("transpiled:")
    if getattr(H.likelihood, "total", False):
        return (f"{_NOCONTRACT_ON if tr else ''}"
                f"\tdouble compute_likelihood(const data_t& data, const double breakout=-infinity) override {{\n"
                f"\t\tauto fp_body = [&]() -> double {{\n{_indent(H.likelihood.body, 3)}\n\t\t}};\n"
                f"\t\treturn this->likelihood = fp_body();\n"
                f"\t}}\n"
                f"{_NOCONTRACT_OFF if tr else ''}")
    return (f"{_NOCONTRACT_ON if tr else ''}"
            f"\tdouble compute_single_likelihood(const datum_t& di) override {{\n"
            f"{_indent(H.likelihood.body, 2)}\n"
            f"\t}}\n"
            f"{_NOCONTRACT_OFF if tr else ''}")


def _keystr_lit(H, k):
    return cpp_literal(H.key_type, k)


def model_source(H, *, for_python: bool) -> str:
    """The model: includes, structs, primitives, grammar, hypothesis (plain Fleet C++)."""
    g = H.grammar
    kind = _kind(H)
    types = g.all_types()
    hdrs = list(dict.fromkeys(h for t in types for h in t.headers()))
    tier3 = [r for r in g.rules if isinstance(r, Primitive) and r.tier == 3]
    if tier3 and not for_python:
        raise ValueError("cannot eject a model with Python-callback (tier 3) primitives")

    o = []
    o.append("// Generated by fleetpy. The model section below is plain Fleet C++.\n")
    o.append("#include <cstdint>\n#include <limits>\n#include <string>\n#include <utility>\n"
             "#include <cmath>\n#include <optional>\n#include <span>\n#include <vector>\n")
    for h in hdrs:
        if "nanobind" not in h:
            o.append(f"#include {h}\n")
    if for_python:
        o.append("#include <nanobind/nanobind.h>\n#include <nanobind/stl/vector.h>\n"
                 "#include <nanobind/stl/tuple.h>\n#include <nanobind/stl/pair.h>\n"
                 "#include <nanobind/stl/string.h>\n")
        for h in hdrs:
            if "nanobind" in h:
                o.append(f"#include <{h}>\n")
        o.append("namespace nb = nanobind;\n")
    o.append('#include "Grammar.h"\n#include "Singleton.h"\n#include "DeterministicLOTHypothesis.h"\n'
             '#include "StochasticLOTHypothesis.h"\n#include "Lexicon.h"\n'
             '#include "TopN.h"\n#include "MCMCChain.h"\n#include "ChainPool.h"\n'
             '#include "ParallelTempering.h"\n#include "Fleet.h"\n#include "Builtins.h"\n\n')
    if for_python:
        o.append(_opaque_casters(H))
        o.append("static nb::object* fp_undefined = nullptr; // fleetpy.Undefined; deliberately leaked\n\n")
        # A per-module namespace: nanobind's type registry is process-wide and keyed by C++
        # type name, so two models that both declare `struct Obj` or `class MyHypothesis`
        # (with different layouts) must not have the same C++ names.
        o.append("namespace FLEETPY_NAMESPACE {\n")

    o.append("// ---- types ----\n")
    for s in all_structs(H):
        o.append(_struct_def(s))
    for name, (ty, val) in (getattr(H.likelihood, "constants", None) or {}).items():
        o.append(f"{ty} {name} = {val};\n")

    uses_tier2 = any(isinstance(r, Primitive) and r.tier == 2 for r in g.rules) or \
        H.likelihood.name.startswith("transpiled:")
    if uses_tier2:
        from .transpile import PRELUDE
        o.append("#include <type_traits>\n" + _NOCONTRACT_ON + PRELUDE + _NOCONTRACT_OFF)

    # User primitives live in their own namespace so their names cannot collide with Fleet's
    # (e.g. a primitive called `pmod` or `flip`).
    o.append("\n// ---- primitives ----\nnamespace fp_user {\n")
    for r in g.rules:  # forward declarations, so tier-2 primitives may call each other
        if isinstance(r, Primitive):
            params = ", ".join(f"{t.cpp} {n}" for n, t in zip(r.argnames, r.argtypes))
            o.append(f"static {r.rettype.cpp} {r.name}({params});\n")
    for i, r in enumerate(g.rules):
        if isinstance(r, Primitive):
            o.append(_prim_fn(r, i))
    o.append("} // namespace fp_user\n")

    tlist = ", ".join([g.input.cpp, g.output.cpp] + [t.cpp for t in types])
    o.append(f"\n// ---- grammar ----\nclass {GRAMMAR_CLASS} : public Grammar<{tlist}>,\n"
             f"\t\t\t\t  public Singleton<{GRAMMAR_CLASS}> {{\npublic:\n\t{GRAMMAR_CLASS}() {{\n")
    for r in g.rules:
        fmt = _cpp_string_literal(r.fmt)
        if isinstance(r, Primitive):
            o.append(f"\t\tadd({fmt}, &fp_user::{r.name}, {r.p!r});\n")
        elif isinstance(r, Terminal):
            o.append(f"\t\tadd_terminal({fmt}, {cpp_literal(r.type, r.value)}, {r.p!r});\n")
        else:
            tmpl = r.cpp_template.replace("{G}", GRAMMAR_CLASS)
            o.append(f"\t\tadd({fmt}, {tmpl}, {r.p!r});\n")
    o.append("\t}\n} grammar;\n")

    hn = H.__name__
    IN, OUT = g.input.cpp, g.output.cpp
    prior = getattr(H, "prior_cpp", None)
    ptr = getattr(H, "_prior_transpiled", False)
    prior_method = "" if not prior else f"""{_NOCONTRACT_ON if ptr else ""}
	double compute_prior() override {{
		if(this->value.count() > FleetArgs::MAX_NODES) return this->prior = -infinity;
		auto fp_body = [&]() -> double {{
{_indent(prior, 3)}
		}};
		return this->prior = fp_body();
	}}
{_NOCONTRACT_OFF if ptr else ""}"""
    if kind in ("deterministic", "stochastic"):
        st = kind == "stochastic"
        o.append(f"\n// ---- hypothesis ----\nclass {hn} final : public {_lot_base(H, hn, st)} {{\npublic:\n"
                 f"\tusing Super = {_lot_base(H, hn, st)};\n\tusing Super::Super;\n\tusing output_t = {OUT};\n"
                 + _WAS_CALLED_INIT)
        o.append(_fp_dist_methods(H, "return this->call(x, fp_err);") if st else _fp_call_method(H))
        o.append(_propose_method(H, hn))
        o.append(_likelihood_method(H))
        o.append(prior_method)
        o.append(f"\tstatic {hn} fp_sample() {{ return {hn}::sample(); }}\n")
        o.append("};\n")
    else:
        if prior:
            raise ValueError("prior_cpp is not supported for a Lexicon (its prior is Fleet's Lexicon prior)")
        if getattr(H.likelihood, "total", False):
            raise ValueError("cpp_total likelihoods are not supported for a Lexicon yet")
        st = H.stochastic
        KEY = H.key_type.cpp
        inner = "FPInner"
        o.append(f"\n// ---- lexicon factor ----\nclass {inner} final : public {_lot_base(H, inner, st)} {{\npublic:\n"
                 f"\tusing Super = {_lot_base(H, inner, st)};\n\tusing Super::Super;\n" + _WAS_CALLED_INIT)
        if not st:
            o.append(_fp_call_method(H))
        o.append(_propose_method(H, inner))
        o.append("};\n")
        LEX = f"Lexicon<{hn},{KEY},{inner},{IN},{OUT},defaultdatum_t<{IN},{OUT}>,std::span<defaultdatum_t<{IN},{OUT}>>>"
        keys = ", ".join(_keystr_lit(H, k) for k in H.keys)
        main = _keystr_lit(H, H.main)
        o.append(f"""
// ---- lexicon ----
class {hn} final : public {LEX} {{
public:
	using Super = {LEX};
	using Super::Super;
	using output_t = {OUT};
{_WAS_CALLED_INIT}
	// Factors find each other through program.loader. Copying a Lexicon copies the factors with
	// their loader still pointing at the source, so every call re-points them at this lexicon
	// (as Models/FormalLanguageTheory-Complex does).
	void fp_prepare() {{ for(auto& [fp_k, fp_f] : this->factors) fp_f.program.loader = this; }}
""")
        if st:
            o.append(_fp_dist_methods(H, f"fp_prepare();\n\t\treturn this->factors.at({main}).call(x, fp_err);"))
        else:
            o.append(f"""
	std::pair<bool, {OUT}> fp_call(const {IN}& x) {{
		fp_prepare();
		return this->factors.at({main}).fp_call(x);
	}}
""")
        o.append(_likelihood_method(H))
        req = "true" if H.require_all_called else "false"
        o.append(f"""
	double compute_likelihood(const data_t& data, const double breakout=-infinity) override {{
		for(auto& [fp_k, fp_f] : this->factors) fp_f.was_called = false;
		Super::compute_likelihood(data, breakout);
		if({req}) {{ // require_all_called: every factor must have been used
			for(auto& [fp_k, fp_f] : this->factors)
				if(not fp_f.was_called) return this->likelihood = -infinity;
		}}
		return this->likelihood;
	}}

	static {hn} fp_sample() {{ return {hn}::sample(std::vector<{KEY}>{{ {keys} }}); }}
}};
""")
    if for_python:
        o.append("} // namespace FLEETPY_NAMESPACE\nusing namespace FLEETPY_NAMESPACE;\n")
    return "".join(o)


def _indent(body: str, n: int) -> str:
    pad = "\t" * n
    return "\n".join(pad + ln for ln in body.strip().splitlines())


def _vm_settings(H) -> str:
    v = H.vm_limits()
    return (f"\tVirtualMachineControl::MAX_RECURSE = {v.max_recurse};\n"
            f"\tVirtualMachineControl::MAX_RUN_PROGRAM = {v.max_run_program};\n"
            f"\tVirtualMachineControl::MAX_STEPS = {v.max_steps};\n"
            f"\tVirtualMachineControl::MAX_OUTPUTS = {v.max_outputs};\n"
            f"\tVirtualMachineControl::MIN_LP = {cpp_literal(T.f64, v.min_lp)};\n")


def python_module_source(H, modname: str) -> str:
    g = H.grammar
    hn = H.__name__
    kind = _kind(H)
    lex = kind == "lexicon"
    st = H.stochastic
    IN, OUT = g.input.cpp, g.output.cpp
    o = [model_source(H, for_python=True)]
    tier3 = [r for r in g.rules if isinstance(r, Primitive) and r.tier == 3]

    if lex:
        KEY = H.key_type.cpp
        state_fns = f"""
static std::string fp_keystr(const std::string& k) {{ return k; }}
static std::string fp_keystr(int k) {{ return std::to_string(k); }}
static std::string fp_keystr(short k) {{ return std::to_string(k); }}

// One string per lexicon state: key \\x1f parseable, joined by \\x1e, in key order.
static std::string fp_state(const {hn}& h) {{
	std::string out;
	for(auto& [k, f] : h.factors) {{
		if(not out.empty()) out += '\\x1e';
		out += fp_keystr(k) + '\\x1f' + f.get_value().parseable();
	}}
	return out;
}}
static size_t fp_nodes(const {hn}& h) {{ size_t n = 0; for(auto& [k, f] : h.factors) n += f.get_value().count(); return n; }}
static double fp_logprob_tree(const {hn}& h) {{
	double lp = 0.0;
	for(auto& [k, f] : h.factors) lp += grammar.log_probability(f.get_value());
	return lp;
}}
static {hn} fp_from_factors(const std::vector<std::pair<{KEY}, std::string>>& fs) {{
	{hn} h;
	for(auto& [k, p] : fs) h.factors[k] = FPInner(grammar.from_parseable(p));
	return h;
}}
"""
    else:
        state_fns = f"""
static std::string fp_state(const {hn}& h) {{ return h.get_value().parseable(); }}
static size_t fp_nodes(const {hn}& h) {{ return h.get_value().count(); }}
static double fp_logprob_tree(const {hn}& h) {{ return grammar.log_probability(h.get_value()); }}
"""
    o.append(f"""
// ---- runtime (nanobind) ----
#include <csignal>
#include <mutex>
#include <unordered_map>

using fp_datum_t = {hn}::datum_t;
using fp_row_t = std::tuple<{IN}, {OUT}, double, double>;

static std::vector<fp_datum_t> fp_data(const std::vector<fp_row_t>& rows) {{
	std::vector<fp_datum_t> out;
	out.reserve(rows.size());
	for(auto& [i, o, r, c] : rows) out.emplace_back(i, o, r, c);
	return out;
}}
{state_fns}
static std::mutex fp_run_mutex;

// Ctrl-C during a run sets Fleet's CTRL_C; the previous handler is restored afterwards.
struct FPSigGuard {{
	struct sigaction old_sa;
	FPSigGuard() {{
		struct sigaction sa{{}};
		sa.sa_handler = [](int) {{ CTRL_C = true; }};
		sigemptyset(&sa.sa_mask);
		sa.sa_flags = 0;
		sigaction(SIGINT, &sa, &old_sa);
	}}
	~FPSigGuard() {{ sigaction(SIGINT, &old_sa, nullptr); }}
}};

static auto fp_run(std::vector<fp_row_t> rows, int algo, size_t chains, double max_temp,
                   unsigned long steps, unsigned long runtime_ms, size_t threads, unsigned long restart,
                   unsigned long seed, size_t ntop, size_t max_nodes, bool breakout) {{

	std::unique_lock<std::mutex> lk(fp_run_mutex, std::try_to_lock);
	if(not lk.owns_lock()) throw std::runtime_error("fleetpy: another run is in progress on this model");
	std::vector<fp_datum_t> data = fp_data(rows);

	FleetArgs::nthreads = threads;
	FleetArgs::nchains  = chains;
	FleetArgs::steps    = steps;
	FleetArgs::runtime  = runtime_ms;
	FleetArgs::restart  = restart;
	FleetArgs::ntop     = ntop;
	FleetArgs::MAX_NODES = max_nodes;
	FleetArgs::LIKELIHOOD_BREAKOUT = breakout;
	FleetArgs::print = 0;
	FleetArgs::top_print_best = false;
	FleetArgs::MCMCYieldOnlyChanges = false;
	CTRL_C = false;

	// Same seeding as Fleet::initialize, plus resetting the per-thread arrival counter so
	// that repeated runs in one process get the same worker-thread streams.
	Rng::thread_counter = 0;
	DefaultRNG.seed(seed);

	const auto samples0 = FleetStatistics::global_sample_count.load();
	const auto pcalls0  = FleetStatistics::posterior_calls.load();
	const auto t0 = now();

	TopN<{hn}> top(ntop);
	std::vector<double> acceptance;
	bool interrupted = false;
	{{
		nb::gil_scoped_release fp_release;
		FPSigGuard fp_sig;
		std::span<fp_datum_t> d(data);
		auto h0 = {hn}::fp_sample();
		Control ctl(steps, runtime_ms, threads, restart);
		if(algo == 0) {{
			MCMCChain samp(h0, d);
			for(auto& h : samp.run(ctl)) top << h;
			acceptance.push_back(samp.acceptance_ratio());
		}}
		else if(algo == 1) {{
			ChainPool samp(h0, d, chains);
			for(auto& h : samp.run(ctl)) top << h;
			for(auto& c : samp.pool) acceptance.push_back(c.acceptance_ratio());
		}}
		else if(algo == 2) {{
			ParallelTempering samp(h0, d, chains, max_temp);
			for(auto& h : samp.run(ctl)) top << h;
			for(auto& c : samp.pool) acceptance.push_back(c.acceptance_ratio());
		}}
		else throw std::invalid_argument("fleetpy: unknown algorithm");
		interrupted = CTRL_C.load();
		CTRL_C = false;
	}}
	const double secs = time_since(t0) / 1000.0;
	return std::make_tuple(top.sorted(false),
	                       (unsigned long)(FleetStatistics::global_sample_count.load() - samples0),
	                       (unsigned long)(FleetStatistics::posterior_calls.load() - pcalls0),
	                       secs, interrupted, acceptance);
}}

NB_MODULE({modname}, m) {{
{_vm_settings(H)}
""")
    for s in all_structs(H):
        if isinstance(s, T.NewType):
            o.append(f'\tnb::class_<{s.name}>(m, "{s.name}")\n'
                     f'\t\t.def(nb::init<{s.base.cpp}>())\n'
                     f'\t\t.def_rw("v", &{s.name}::v);\n')
        else:
            ftys = ", ".join(ft.cpp for _, ft in s.fields)
            o.append(f'\tnb::class_<{s.name}>(m, "{s.name}")\n\t\t.def(nb::init<{ftys}>())')
            for n, _ in s.fields:
                o.append(f'\n\t\t.def_rw("{n}", &{s.name}::{n})')
            o.append(";\n")
    nts = ", ".join(f"(int)grammar.template nt<{t.cpp}>()" for t in g.all_types())
    if st:
        call_def = f"""
		.def("call", [](const {hn}& h, const {IN}& x) {{
			{hn} hc = h;
			auto d = hc.fp_dist(x);
			std::vector<std::pair<{OUT}, double>> out;
			for(auto& [k, v] : d.values()) out.emplace_back(k, v);
			return out;
		}})
		.def("logprob", [](const {hn}& h, const {IN}& x, const {OUT}& y) {{ {hn} hc = h; return hc.fp_logprob(x, y); }})"""
    else:
        call_def = f"""
		.def("call", [](const {hn}& h, const {IN}& x) {{
			{hn} hc = h; // fp_call mutates bookkeeping fields; keep the Python object unchanged
			return hc.fp_call(x);
		}})"""
    lex_defs = ""
    if lex:
        lex_defs = f"""
		.def("factors", [](const {hn}& h) {{
			std::vector<std::pair<{H.key_type.cpp}, std::string>> out;
			for(auto& [k, f] : h.factors) out.emplace_back(k, f.get_value().parseable());
			return out;
		}})
		.def("factor_strings", [](const {hn}& h) {{
			std::vector<std::pair<{H.key_type.cpp}, std::string>> out;
			for(auto& [k, f] : h.factors) out.emplace_back(k, f.string());
			return out;
		}})"""
    # the per-datum likelihood itself, independent of the prior (compute_posterior skips the
    # likelihood and reports NaN when the prior is -inf, e.g. beyond MAX_NODES)
    single = (f"""
		.def("total_likelihood", [](const {hn}& h, std::vector<fp_row_t> rows) {{
			auto data = fp_data(rows);
			{hn} hc = h; std::span<fp_datum_t> d(data);
			return hc.compute_likelihood(d);
		}})""" if getattr(H.likelihood, "total", False) else f"""
		.def("single_likelihood", [](const {hn}& h, fp_row_t row) {{
			auto data = fp_data(std::vector<fp_row_t>{{row}});
			{hn} hc = h;
			return hc.compute_single_likelihood(data[0]);
		}})""")
    if not lex:   # the pieces a tier-2 prior can use, for its differential test
        single += f"""
		.def("grammar_prior", [](const {hn}& h) {{ {hn} hc = h; return hc.{hn}::Super::compute_prior(); }})
		.def("recursion_count", [](const {hn}& h) {{ {hn} hc = h; return (int64_t)hc.recursion_count(); }})"""
    o.append(f"""
	nb::class_<{hn}>(m, "Hypothesis")
		.def("string", [](const {hn}& h) {{ return h.string(); }})
		.def("parseable", [](const {hn}& h) {{ return fp_state(h); }})
		.def_prop_ro("prior", [](const {hn}& h) {{ return h.prior; }})
		.def_prop_ro("likelihood", [](const {hn}& h) {{ return h.likelihood; }})
		.def_prop_ro("posterior", [](const {hn}& h) {{ return h.posterior; }})
		.def_prop_ro("nodes", [](const {hn}& h) {{ return fp_nodes(h); }}){call_def}{lex_defs}
		.def("compute_prior", [](const {hn}& h) {{ {hn} hc = h; return hc.compute_prior(); }}){single}
		.def("compute_posterior", [](const {hn}& h, std::vector<fp_row_t> rows) {{
			auto data = fp_data(rows);
			{hn} hc = h; std::span<fp_datum_t> d(data);
			hc.compute_posterior(d);
			return std::make_tuple(hc.prior, hc.likelihood, hc.posterior);
		}})
		.def("__eq__", [](const {hn}& a, const {hn}& b) {{ return a == b; }})
		.def("__hash__", [](const {hn}& h) {{ return h.hash(); }});
""")
    if lex:
        o.append(f'\tm.def("from_factors", &fp_from_factors);\n')
    else:
        o.append(f'\tm.def("from_parseable", [](const std::string& s) {{ return {hn}(grammar.from_parseable(s)); }});\n')
    o.append(f"""
	m.def("sample", [](unsigned long seed) {{
		if(seed != 0) DefaultRNG.seed(seed);
		return {hn}::fp_sample();
	}});
	m.def("nonterminals", []() {{ return std::vector<int>{{ {nts} }}; }});
	m.def("rule_formats", []() {{
		std::vector<std::pair<int, std::string>> out;
		for(auto& r : grammar) out.emplace_back((int)r.nt, r.format);
		return out;
	}});
	m.def("log_probability", [](const {hn}& h) {{ return fp_logprob_tree(h); }});
	m.def("set_max_nodes", [](size_t n) {{ FleetArgs::MAX_NODES = n; }});
	m.def("run", &fp_run);
	// Every state of one MCMC chain: (distinct states, id sequence).
	// raw=false (the exact-posterior check): start from a prior sample with finite posterior,
	// discard `burn` steps, and record only finite-posterior states (a chain at -inf is
	// restarting, not sampling the target).
	// raw=true (equivalence with native Fleet): exactly what a plain MCMCChain run does --
	// the first prior sample as the start, and every yielded state recorded.
	m.def("trace", [](std::vector<fp_row_t> rows, unsigned long steps, unsigned long burn,
	                  unsigned long seed, size_t max_nodes, bool breakout, bool raw) {{
		auto data = fp_data(rows);
		std::unique_lock<std::mutex> lk(fp_run_mutex, std::try_to_lock);
		if(not lk.owns_lock()) throw std::runtime_error("fleetpy: another run is in progress on this model");
		FleetArgs::MAX_NODES = max_nodes;
		FleetArgs::LIKELIHOOD_BREAKOUT = breakout;
		FleetArgs::nthreads = 1;
		FleetArgs::MCMCYieldOnlyChanges = false;
		CTRL_C = false;
		Rng::thread_counter = 0;
		DefaultRNG.seed(seed);
		std::unordered_map<std::string, uint32_t> ids;
		std::vector<std::string> names;
		std::vector<uint32_t> seq;
		seq.reserve(steps);
		{{
			nb::gil_scoped_release fp_release;
			FPSigGuard fp_sig;
			std::span<fp_datum_t> d(data);
			auto h0 = {hn}::fp_sample();
			for(int tries = 0; not raw; tries++) {{
				h0.compute_posterior(d);
				if(std::isfinite(h0.posterior)) break;
				if(tries > 100000) throw std::runtime_error("fleetpy.trace: no finite-posterior start found");
				h0 = {hn}::fp_sample();
			}}
			MCMCChain samp(h0, d);
			Control ctl(steps + burn, 0, 1, 0);
			unsigned long k = 0;
			for(auto& h : samp.run(ctl)) {{
				if(not raw and (k++ < burn or not std::isfinite(h.posterior))) continue;
				auto [it, inserted] = ids.try_emplace(fp_state(h), (uint32_t)names.size());
				if(inserted) names.push_back(it->first);
				seq.push_back(it->second);
			}}
			CTRL_C = false;
		}}
		return std::make_pair(names, seq);
	}});
	m.def("fp_set_undefined", [](nb::object o) {{ fp_undefined = new nb::object(o); }});
""")
    for r in tier3:
        o.append(f'\tm.def("fp_set_callback_{r.name}", [](nb::object o) {{ fp_user::fp_cb_{r.name} = new nb::object(o); }});\n')
    # Direct access to each compiled primitive, for differential testing: (ok, value).
    for r in g.rules:
        if isinstance(r, Primitive) and r.tier in (1, 2):
            params = ", ".join(f"{t.cpp} {n}" for n, t in zip(r.argnames, r.argtypes))
            args = ", ".join(r.argnames)
            o.append(f'\tm.def("fp_prim_{r.name}", []({params}) {{\n'
                     f'\t\ttry {{ return std::make_pair(true, fp_user::{r.name}({args})); }}\n'
                     f'\t\tcatch (VMSRuntimeError&) {{ return std::make_pair(false, {r.rettype.cpp}{{}}); }}\n'
                     f'\t}});\n')
    o.append(f'\tm.attr("fp_n_python_callbacks") = {len(tier3)};\n')
    o.append("}\n")
    return "".join(o)


def eject_main_source(H, data, algo, control) -> str:
    """A standalone Main.cpp with the data embedded, mirroring Fleet's Models/*/Main.cpp."""
    g = H.grammar
    hn = H.__name__
    src = [model_source(H, for_python=False)]
    rows = []
    for d in data:
        rows.append(f"\tfp_datum_t({cpp_literal(g.input, d.input)}, {cpp_literal(g.output, d.output)}, "
                    f"{_dbl(d.reliability)}, {_dbl(d.count)})")
    if algo.algo == 0:
        sampler = "MCMCChain samp(h0, mydata);"
    elif algo.algo == 1:
        sampler = f"ChainPool samp(h0, mydata, {algo.chains});"
    else:
        sampler = f"ParallelTempering samp(h0, mydata, {algo.chains}, {algo.max_temp!r});"
    src.append(f"""
#ifndef NO_INCLUDE_MAIN
using fp_datum_t = {hn}::datum_t;

int main(int argc, char** argv) {{
	Fleet fleet("{hn} (ejected from fleetpy)");
	fleet.initialize(argc, argv);
	FleetArgs::LIKELIHOOD_BREAKOUT = {"true" if H.likelihood.nonpositive else "false"};
{_vm_settings(H)}
	std::vector<fp_datum_t> mydata = {{
{(","+chr(10)).join(rows)}
	}};

	TopN<{hn}> top;
	auto h0 = {hn}::fp_sample();
	{sampler}
	for(auto& h : samp.run(Control()) | printer(FleetArgs::print) | top) {{
		UNUSED(h);
	}}
	top.print();
}}
#endif
""")
    return "".join(src)


def _dbl(x) -> str:
    if x is None:
        return "NaN"
    return cpp_literal(T.f64, float(x))


def spec_json(H) -> str:
    return json.dumps(H.spec_dict(), sort_keys=True)
