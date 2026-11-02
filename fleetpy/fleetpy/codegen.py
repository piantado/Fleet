"""
Code generation: ModelSpec -> C++.

Only what is specific to a model is generated: its types, primitives, grammar and
hypothesis class, which are plain Fleet C++ (identical in a Python module and in an
ejected Main.cpp). Everything generic is real C++ in fleetpy/cpp/:

  fleetpy_pyops.h    Python semantics for transpiled code
  fleetpy_runtime.h  run, trace and the Python bindings, as templates over the hypothesis

A Python module is  model + "fp_rt::bind<H>(...)";  an ejected model is  model + main().
"""

from __future__ import annotations

from pathlib import Path

from . import ftypes as T
from .hypothesis import ModelSpec
from .rules import Primitive

GRAMMAR = "MyGrammar"
INNER = "FPInner"            # a Lexicon's factor class
CPP_DIR = Path(__file__).resolve().parent / "cpp"

# Transpiled code must match Python bit for bit, so it is compiled without FMA contraction
# (see fleetpy_pyops.h). Fleet's own code and C++ snippets keep Fleet's flags.
_NOCONTRACT = ('#pragma GCC push_options\n#pragma GCC optimize ("fp-contract=off")\n', '#pragma GCC pop_options\n')

# Fleet's ProgramLoader::was_called (src/VirtualMachine/Program/Program.h) had no initializer,
# so a fresh hypothesis carried an indeterminate bool that copies and moves then read: UB
# (UBSan: "load of value 16, which is not a valid value for type 'bool'"). This default member
# initializer runs in every constructor, inherited ones included. Harmless once Fleet has the
# upstream fix (`bool was_called = false;`).
_WAS_CALLED_INIT = "bool fp_was_called_init = (this->was_called = false, true);"

_STD_HEADERS = ["<cstdint>", "<limits>", "<string>", "<utility>", "<cmath>", "<optional>", "<span>", "<vector>"]
_FLEET_HEADERS = ["Grammar.h", "Singleton.h", "DeterministicLOTHypothesis.h", "StochasticLOTHypothesis.h",
                  "Lexicon.h", "TopN.h", "MCMCChain.h", "ChainPool.h", "ParallelTempering.h", "Fleet.h",
                  "Builtins.h"]


def indent(text: str, n: int) -> str:
    pad = "\t" * n
    return "\n".join(pad + ln if ln.strip() else "" for ln in text.strip("\n").splitlines())


def nocontract(code: str, transpiled: bool) -> str:
    return f"{_NOCONTRACT[0]}{code}{_NOCONTRACT[1]}" if transpiled else code


def uses_transpiled_code(spec: ModelSpec) -> bool:
    return bool(spec.primitives) or spec.likelihood.transpiled or bool(spec.prior and spec.prior.transpiled)


# ---- types ------------------------------------------------------------------------------------

def definition(t) -> str:
    """The C++ definition of a struct or newtype."""
    if isinstance(t, T.NewType):
        n, b = t.name, t.base.cpp
        return (f"struct {n} {{\n\t{b} v{{}};\n\t{n}() = default;\n\t{n}({b} x) : v(x) {{}}\n"
                f"\toperator {b}() const {{ return v; }}\n"
                f"\tauto operator<=>(const {n}&) const = default;\n"
                f"\tbool operator==(const {n}&) const = default;\n}};\n")
    n = t.name
    fields = "".join(f"\t{ft.cpp} {f}{{}};\n" for f, ft in t.fields)
    params = ", ".join(f"{ft.cpp} fp_{f}" for f, ft in t.fields)
    inits = ", ".join(f"{f}(fp_{f})" for f, _ in t.fields)
    return (f"struct {n} {{\n{fields}\t{n}() = default;\n\t{n}({params}) : {inits} {{}}\n"
            f"\tauto operator<=>(const {n}&) const = default;\n"
            f"\tbool operator==(const {n}&) const = default;\n}};\n")


def opaque_caster(t: T.OpaqueType) -> str:
    """A nanobind type caster for an opaque type with a Python representation. Casters are
    compile-time and local to the module, unlike nanobind's process-wide class registry."""
    P = t.py.cpp
    return f"""namespace nanobind {{ namespace detail {{
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
"""


# ---- primitives -------------------------------------------------------------------------------

def _signature(r: Primitive) -> str:
    return f"static {r.ret.cpp} {r.name}(" + ", ".join(f"{t.cpp} {n}" for n, t in r.params) + ")"


def primitive_function(r: Primitive, spec: ModelSpec) -> str:
    head = _signature(r)
    if r.tier == 1:
        body = r.cpp.strip()
        if "return" in body or ";" in body:
            return f"{head} {{\n{body}\n}}\n"
        return f"{head} {{ return ({body}); }}\n"
    if r.tier == 2:
        return nocontract(f"{head} {{\n{spec.primitives[r.name].body}\n}}\n", True)
    # tier 3: call back into Python, holding the GIL
    args = ", ".join(n for n, _ in r.params)
    return f"""static nb::object* fp_cb_{r.name} = nullptr; // set from Python; deliberately leaked
{head} {{
	nb::gil_scoped_acquire fp_gil;
	try {{
		nb::object fp_r = (*fp_cb_{r.name})({args});
		return nb::cast<{r.ret.cpp}>(fp_r);
	}} catch (nb::python_error& fp_e) {{
		if(fp_undefined != nullptr and fp_e.matches(*fp_undefined)) throw VMSRuntimeError();
		throw;
	}}
}}
"""


# ---- the grammar ------------------------------------------------------------------------------

def grammar_class(spec: ModelSpec) -> str:
    g = spec.grammar
    tlist = ", ".join([g.input.cpp, g.output.cpp] + [t.cpp for t in g.types()])
    adds = "".join(f"\t\t{r.cpp_registration(GRAMMAR)};\n" for r in g.rules)
    return (f"class {GRAMMAR} : public Grammar<{tlist}>,\n\t\t\t\t  public Singleton<{GRAMMAR}> {{\n"
            f"public:\n\t{GRAMMAR}() {{\n{adds}\t}}\n}} grammar;\n")


# ---- the hypothesis ---------------------------------------------------------------------------

def _lot_base(spec, cls):
    g = spec.grammar
    IN, OUT = g.input.cpp, g.output.cpp
    base = "StochasticLOTHypothesis" if spec.stochastic else "DeterministicLOTHypothesis"
    return f"{base}<{cls},{IN},{OUT},{GRAMMAR},&grammar,defaultdatum_t<{IN},{OUT}>,std::span<defaultdatum_t<{IN},{OUT}>>>"


def _class(name, base, members, traits=None):
    head = f"class {name} final : public {base} {{\npublic:\n\tusing Super = {base};\n\tusing Super::Super;\n"
    if traits is not None:
        head += "".join(f"\tstatic constexpr bool fp_{k} = {'true' if v else 'false'};\n" for k, v in traits.items())
    head += f"\t{_WAS_CALLED_INIT}\n"
    return head + "".join(m for m in members if m) + "};\n"


def _traits(spec):
    return {"lexicon": spec.is_lexicon, "stochastic": spec.stochastic,
            "total_likelihood": spec.likelihood.scope == "total"}


def _fp_call(spec, target=None):
    """Deterministic call that also reports whether the program completed (false on
    VMSRuntimeError, MAX_RECURSE or MAX_RUN_PROGRAM), so an error is never confused with a
    legitimate output equal to the error value."""
    g = spec.grammar
    IN, OUT = g.input.cpp, g.output.cpp
    if target is not None:   # a Lexicon delegates to its main factor
        return f"""
	std::pair<bool, {OUT}> fp_call(const {IN}& x) {{
		fp_prepare();
		return this->factors.at({target}).fp_call(x);
	}}
"""
    return f"""
	std::pair<bool, {OUT}> fp_call(const {IN}& x) {{
		const {OUT} fp_err{{}};   // VirtualMachineState holds err by reference: it must outlive vms
		VirtualMachineState_t vms(x, fp_err, nullptr);
		vms.program = this->program;   // carries program.loader (this, or the owning Lexicon)
		this->was_called = true;
		auto out = vms.run();
		this->total_instruction_count_last_call = vms.runtime_counter.total;
		this->total_vms_steps = 1;
		return std::make_pair(vms.status == vmstatus_t::COMPLETE, out);
	}}
"""


def _fp_dist(spec, call):
    """Fleet's stochastic call (every execution path, unnormalized) and the normalized
    log P(y | x) that fleetpy's likelihoods use."""
    g = spec.grammar
    IN, OUT = g.input.cpp, g.output.cpp
    return f"""
	DiscreteDistribution<{OUT}> fp_dist(const {IN}& x) {{
		const {OUT} fp_err{{}};   // VMS states hold err by reference: it must outlive the call
		{call}
	}}

	double fp_logprob(const {IN}& x, const {OUT}& y) {{
		auto fp_d = fp_dist(x);
		if(fp_d.size() == 0 or not fp_d.contains(y)) return -infinity;
		return fp_d.get(y, -infinity) - fp_d.Z();
	}}
"""


def _propose(spec, cls):
    if spec.proposal is None:
        return ""
    return f"""
	[[nodiscard]] virtual std::optional<std::pair<{cls},double>> propose() const override {{
		std::optional<std::pair<Node,double>> p;
		{{
{indent(spec.proposal.body, 3)}
		}}
		if(not p) return {{}};
		return std::make_pair({cls}(std::move(p->first)), p->second);
	}}
"""


def _likelihood(spec):
    lk = spec.likelihood
    if lk.scope == "total":
        code = (f"\tdouble compute_likelihood(const data_t& data, const double breakout=-infinity) override {{\n"
                f"\t\tauto fp_body = [&]() -> double {{\n{indent(lk.body, 3)}\n\t\t}};\n"
                f"\t\treturn this->likelihood = fp_body();\n\t}}\n")
    else:
        code = f"\tdouble compute_single_likelihood(const datum_t& di) override {{\n{indent(lk.body, 2)}\n\t}}\n"
    return nocontract(code, lk.transpiled)


def _prior(spec):
    if spec.prior is None:
        return ""
    code = (f"\tdouble compute_prior() override {{\n"
            f"\t\tif(this->value.count() > FleetArgs::MAX_NODES) return this->prior = -infinity;\n"
            f"\t\tauto fp_body = [&]() -> double {{\n{indent(spec.prior.body, 3)}\n\t\t}};\n"
            f"\t\treturn this->prior = fp_body();\n\t}}\n")
    return nocontract(code, spec.prior.transpiled)


def hypothesis_classes(spec: ModelSpec) -> str:
    H = spec.name
    if not spec.is_lexicon:
        call = _fp_dist(spec, "return this->call(x, fp_err);") if spec.stochastic else _fp_call(spec)
        return _class(H, _lot_base(spec, H), [
            f"\tusing output_t = {spec.grammar.output.cpp};\n", call, _propose(spec, H), _likelihood(spec),
            _prior(spec), f"\tstatic {H} fp_sample() {{ return {H}::sample(); }}\n"], _traits(spec))

    g, lex = spec.grammar, spec.lexicon
    IN, OUT, KEY = g.input.cpp, g.output.cpp, lex.key_type.cpp
    factor = _class(INNER, _lot_base(spec, INNER), [None if spec.stochastic else _fp_call(spec),
                                                     _propose(spec, INNER)])
    LEX = f"Lexicon<{H},{KEY},{INNER},{IN},{OUT},defaultdatum_t<{IN},{OUT}>,std::span<defaultdatum_t<{IN},{OUT}>>>"
    main = lex.key_type.literal(lex.main)
    keys = ", ".join(lex.key_type.literal(k) for k in lex.keys)
    prepare = """
	// Factors find each other through program.loader. Copying a Lexicon copies the factors with
	// their loader still pointing at the source, so every call re-points them at this lexicon
	// (as Models/FormalLanguageTheory-Complex does).
	void fp_prepare() { for(auto& [fp_k, fp_f] : this->factors) fp_f.program.loader = this; }
"""
    call = (_fp_dist(spec, f"fp_prepare();\n\t\treturn this->factors.at({main}).call(x, fp_err);")
            if spec.stochastic else _fp_call(spec, target=main))
    total = f"""
	double compute_likelihood(const data_t& data, const double breakout=-infinity) override {{
		for(auto& [fp_k, fp_f] : this->factors) fp_f.was_called = false;
		Super::compute_likelihood(data, breakout);
		if({'true' if lex.require_all_called else 'false'}) {{ // require_all_called: every factor must have been used
			for(auto& [fp_k, fp_f] : this->factors)
				if(not fp_f.was_called) return this->likelihood = -infinity;
		}}
		return this->likelihood;
	}}

	static {H} fp_sample() {{ return {H}::sample(std::vector<{KEY}>{{ {keys} }}); }}
"""
    return factor + "\n" + _class(H, LEX, [f"\tusing output_t = {OUT};\n", prepare, call, _likelihood(spec), total],
                                  _traits(spec))


# ---- whole translation units ------------------------------------------------------------------

def model_source(spec: ModelSpec, *, python: bool) -> str:
    """The model: includes, types, primitives, grammar, hypothesis (plain Fleet C++)."""
    g = spec.grammar
    types = spec.types()
    prims = g.primitives()
    if not python and any(r.tier == 3 for r in prims):
        raise ValueError("cannot eject a model with Python-callback (tier 3) primitives")
    hdrs = T.headers(*types)
    nb_hdrs = [h for h in hdrs if "nanobind" in h]
    o = ["// Generated by fleetpy. The model section below is plain Fleet C++.\n"]
    o += [f"#include {h}\n" for h in _STD_HEADERS + [h for h in hdrs if "nanobind" not in h]]
    if python:
        o += [f"#include {h}\n" for h in ["<nanobind/nanobind.h>"] + nb_hdrs]
    o += [f'#include "{h}"\n' for h in _FLEET_HEADERS]
    if uses_transpiled_code(spec):
        o.append('#include "fleetpy_pyops.h"\n' if python else
                 "\n// ---- fleetpy_pyops.h ----\n" + (CPP_DIR / "fleetpy_pyops.h").read_text().replace("#pragma once\n", ""))
    if python:
        o.append('#include "fleetpy_runtime.h"\n\n')
        o += [opaque_caster(t) for t in T.walk(*types) if isinstance(t, T.OpaqueType) and t.py is not None]
        # A per-module namespace: nanobind's type registry is process-wide and keyed by C++
        # type, so two models that both declare `struct Obj` or `class MyHypothesis` (with
        # different layouts) must not have the same C++ names.
        o.append("namespace FLEETPY_NAMESPACE {\n")
    o.append("\n// ---- types ----\n")
    o += [definition(t) for t in T.definitions(*types)]
    o += [f"{ty} {name} = {val};\n" for ty, name, val in spec.likelihood.constants]
    # User primitives get their own namespace so that their names cannot collide with Fleet's
    # (a primitive called `pmod` or `flip`). Declarations first: primitives may call each other.
    o.append("\n// ---- primitives ----\nnamespace fp_user {\n")
    o += [_signature(r) + ";\n" for r in prims]
    o += [primitive_function(r, spec) for r in prims]
    o.append("} // namespace fp_user\n\n// ---- grammar ----\n")
    o.append(grammar_class(spec))
    o.append("\n// ---- hypothesis ----\n")
    o.append(hypothesis_classes(spec))
    if python:
        o.append("} // namespace FLEETPY_NAMESPACE\nusing namespace FLEETPY_NAMESPACE;\n")
    return "".join(o)


def vm_settings(spec: ModelSpec) -> str:
    v = spec.vm
    return (f"\tVirtualMachineControl::MAX_RECURSE = {v.max_recurse};\n"
            f"\tVirtualMachineControl::MAX_RUN_PROGRAM = {v.max_run_program};\n"
            f"\tVirtualMachineControl::MAX_STEPS = {v.max_steps};\n"
            f"\tVirtualMachineControl::MAX_OUTPUTS = {v.max_outputs};\n"
            f"\tVirtualMachineControl::MIN_LP = {T.cpp_double(v.min_lp)};\n")


def module_source(spec: ModelSpec) -> str:
    """A nanobind module: the model plus its bindings. Its name and namespace are the
    placeholders FLEETPY_MODULE_NAME / FLEETPY_NAMESPACE (filled in from the build hash)."""
    g = spec.grammar
    o = [model_source(spec, python=True), "\nNB_MODULE(FLEETPY_MODULE_NAME, m) {\n", vm_settings(spec)]
    for t in T.definitions(*spec.types()):
        if isinstance(t, T.NewType):
            o.append(f'\tnb::class_<{t.name}>(m, "{t.name}")\n\t\t.def(nb::init<{t.base.cpp}>())\n'
                     f'\t\t.def_rw("v", &{t.name}::v);\n')
        else:
            init = ", ".join(ft.cpp for _, ft in t.fields)
            rw = "".join(f'\n\t\t.def_rw("{f}", &{t.name}::{f})' for f, _ in t.fields)
            o.append(f'\tnb::class_<{t.name}>(m, "{t.name}")\n\t\t.def(nb::init<{init}>()){rw};\n')
    nts = ", ".join(f"(int)grammar.template nt<{t.cpp}>()" for t in g.types())
    o.append(f"\tfp_rt::bind<{spec.name}>(m, grammar, {{ {nts} }});\n")
    prims = g.primitives()
    for r in prims:
        if r.tier == 3:
            o.append(f'\tm.def("fp_set_callback_{r.name}", [](nb::object o) {{ fp_user::fp_cb_{r.name} = new nb::object(o); }});\n')
        else:   # direct access to each compiled primitive, for differential testing: (ok, value)
            params = ", ".join(f"{t.cpp} {n}" for n, t in r.params)
            args = ", ".join(n for n, _ in r.params)
            o.append(f'\tm.def("fp_prim_{r.name}", []({params}) {{\n'
                     f'\t\ttry {{ return std::make_pair(true, fp_user::{r.name}({args})); }}\n'
                     f'\t\tcatch (VMSRuntimeError&) {{ return std::make_pair(false, {r.ret.cpp}{{}}); }}\n'
                     f'\t}});\n')
    o.append(f'\tm.attr("fp_n_python_callbacks") = {sum(r.tier == 3 for r in prims)};\n}}\n')
    return "".join(o)


def eject_source(spec: ModelSpec, data, sampler) -> str:
    """A standalone Main.cpp with the data embedded, mirroring Fleet's Models/*/Main.cpp."""
    g, H = spec.grammar, spec.name

    def dbl(x):
        return "NaN" if x is None else T.cpp_double(float(x))
    rows = ",\n".join(f"\tfp_datum_t({g.input.literal(d.input)}, {g.output.literal(d.output)}, "
                      f"{dbl(d.reliability)}, {dbl(d.count)})" for d in data)
    return model_source(spec, python=False) + f"""
#ifndef NO_INCLUDE_MAIN
using fp_datum_t = {H}::datum_t;

int main(int argc, char** argv) {{
	Fleet fleet("{H} (ejected from fleetpy)");
	fleet.initialize(argc, argv);
	FleetArgs::LIKELIHOOD_BREAKOUT = {"true" if spec.likelihood.nonpositive else "false"};
{vm_settings(spec)}
	std::vector<fp_datum_t> mydata = {{
{rows}
	}};

	TopN<{H}> top;
	auto h0 = {H}::fp_sample();
	{sampler.cpp("h0", "mydata")}
	for(auto& h : samp.run(Control()) | printer(FleetArgs::print) | top) {{
		UNUSED(h);
	}}
	top.print();
}}
#endif
"""
