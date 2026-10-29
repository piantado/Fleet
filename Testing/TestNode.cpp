// TestNode: copying, moving and assignment of Nodes (and the tree invariants they must preserve).
//
// The invariants that every operation must leave intact, checked by check_tree():
//   (I1) every child c of n has c.parent == &n and c.pi == its index         (parent links)
//   (I2) n.children.size() == n.rule->N for non-null nodes                     (arity)
//   (I3) each child's nonterminal is the type the rule expects (or null)       (typing)
//   (I4) iteration visits exactly count() nodes, in postfix order, and stops at the subtree root
//
// Values are compared through an independent serialization (shape(), below) as well as through
// Node::operator==, so that a bug in operator== cannot hide a bug in copying.
//
// Covered: copy/move construction, copy/move assignment (incl. self-assignment, promoting a
// descendant to its ancestor's place, and assigning an ancestor into a descendant), Node::assign,
// set_child (copy and move, incl. moving a sibling), push_back, std::swap, containers of Nodes
// (vector reallocation), deep-copy independence, hash/==/<=> consistency, and the
// string()/parseable() round trips.

#include <map>
#include <set>
#include <string>
#include <vector>

#include "Fleet.h"
#include "Grammar.h"
#include "Builtins.h"

#include "TestHarness.h"

using namespace TestHarness;

class G : public Grammar<int,int, int,bool> {
public:
	G() {
		add("add(%s,%s)",     +[](int a, int b) -> int { return a+b; }, 1.0);
		add("neg(%s)",        +[](int a) -> int { return -a; }, 0.6);
		add("x",              Builtins::X<G>, 1.5);
		add("one",            +[]() -> int { return 1; }, 1.0);
		add("if(%s,%s,%s)",   Builtins::If<G,int>, 0.4);
		add("and(%s,%s)",     Builtins::And<G>, 0.5);
		add("not(%s)",        Builtins::Not<G>, 0.5);
		add("lt(%s,%s)",      +[](int a, int b) -> bool { return a<b; }, 0.4);
		add("T",              +[]() -> bool { return true; }, 1.0);
		add("F",              +[]() -> bool { return false; }, 1.0);
	}
} grammar;

// ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// Independent views of a tree
// ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

// a serialization that only reads rule pointers, lp, can_resample and children by recursion
std::string shape(const Node& n) {
	std::string s = "(" + n.rule->format + "|" + str(n.lp) + "|" + str(n.can_resample);
	for(size_t i=0;i<n.nchildren();i++) s += " " + shape(n.child(i));
	return s + ")";
}

void postfix(const Node& n, std::vector<const Node*>& out) {
	for(size_t i=0;i<n.nchildren();i++) postfix(n.child(i), out);
	out.push_back(&n);
}

size_t rec_count(const Node& n) { size_t c = 1; for(size_t i=0;i<n.nchildren();i++) c += rec_count(n.child(i)); return c; }
size_t rec_depth(const Node& n) { size_t d = 0; for(size_t i=0;i<n.nchildren();i++) d = std::max(d, 1+rec_depth(n.child(i))); return d; }

// all invariants; returns false (and records) on the first problem
bool check_tree(const Node& root, const std::string& what) {
	bool ok = true;
	std::function<void(const Node&)> rec = [&](const Node& n) {
		if(not n.is_null()) {
			if(n.nchildren() != n.rule->N) { ok = false; CHECK(false, what << ": arity " << n.nchildren() << " != " << n.rule->N << " at " << n.rule->format); }
		}
		for(size_t i=0;i<n.nchildren();i++) {
			const Node& c = n.child(i);
			if(c.parent != &n or c.pi != i) { ok = false; CHECK(false, what << ": bad parent link at child " << i << " of " << n.rule->format); }
			if(not c.is_null() and c.nt() != n.rule->type(i)) { ok = false; CHECK(false, what << ": bad child type"); }
			rec(c);
		}
	};
	rec(root);
	// iteration: postfix order, exactly the subtree, starting from any node
	std::vector<const Node*> ref; postfix(root, ref);
	std::vector<const Node*> it;
	for(const auto& n : root) { it.push_back(&n); if(it.size() > ref.size() + 5) break; }
	if(it != ref) { ok = false; CHECK(false, what << ": iteration visited " << it.size() << " nodes (expected " << ref.size() << ") or wrong order"); }
	if(root.count() != ref.size()) { ok = false; CHECK(false, what << ": count()"); }
	return ok;
}

// every node of a and b lives at a different address (deep copy)
bool disjoint(const Node& a, const Node& b) {
	std::vector<const Node*> pa, pb; postfix(a, pa); postfix(b, pb);
	std::set<const Node*> sa(pa.begin(), pa.end());
	for(auto p : pb) if(sa.count(p)) return false;
	return true;
}

Node random_tree(size_t min_nodes=1, size_t max_nodes=40) {
	while(true) {
		Node t = grammar.generate();
		if(t.count() >= min_nodes and t.count() <= max_nodes) {
			// make can_resample vary so that copies must carry it
			for(auto& n : t) if(flip(0.2)) n.can_resample = false;
			return t;
		}
	}
}

// a random node strictly inside t (not the root), or nullptr
Node* random_inner(Node& t) {
	if(t.count() < 2) return nullptr;
	return t.get_nth(myrandom((int)t.count()-1)); // postfix: the root is last, so indices < count-1 are inner
}

int main() {
	test_name = "Node";
	FleetArgs::print_header = 0;
	DefaultRNG.seed(seed());
	const int R = 3000;

	// ==================================================================================
	section("construction");
	{
		Node nul;
		CHECK(nul.is_null() and nul.nchildren() == 0 and nul.parent == nullptr and nul.count() == 1, "default Node is a null root");
		Node a = grammar.makeNode(grammar.get_rule((nonterminal_t)0, "add(%s,%s)"));
		CHECK(a.nchildren() == 2 and a.child(0).is_null() and a.child(1).is_null(), "makeNode creates null children");
		CHECK(check_tree(a, "fresh node with null children"), "fresh node invariants");
		CHECK(not a.is_complete() and a.count_nonnull() == 1, "partial tree is incomplete");
		CHECK_CLOSE(a.lp, std::log(1.0) - std::log(1.0+0.6+1.5+1.0+0.4), 1e-15, 0, "makeNode lp");
		CHECK_CLOSE(grammar.log_probability(a), a.lp, 1e-15, 0, "log_probability of a partial tree ignores null nodes but includes the root");
	}

	// ==================================================================================
	section("copy construction");
	size_t nsubcopies = 0, nsubcopies_with_parent = 0;
	for(int r=0;r<R;r++) {
		Node t = random_tree();
		const std::string before = shape(t);
		Node c(t);
		CHECK(shape(c) == before and shape(t) == before, "copy has the same value, original unchanged");
		CHECK(c == t and not (c != t) and c.hash() == t.hash() and (c <=> t) == 0, "copy compares equal");
		CHECK(c.string() == t.string() and c.parseable() == t.parseable(), "copy prints the same");
		CHECK(disjoint(c, t), "copy is deep");
		CHECK(check_tree(c, "copy") and check_tree(t, "original after copy"), "copy invariants");
		CHECK(c.is_root(), "copy of a root is a root");
		// independence: mutate the copy, the original must not change
		if(Node* s = random_inner(c)) {
			s->assign(grammar.generate(s->nt()));
			CHECK(shape(t) == before, "mutating a copy changed the original");
		}
		// copy of an inner subtree
		if(Node* s = random_inner(t)) {
			const std::string sub = shape(*s);
			Node sc(*s);
			CHECK(shape(sc) == sub and sc == *s, "copy of a subtree has the subtree's value");
			CHECK(check_tree(sc, "subtree copy"), "subtree copy invariants");
			CHECK(disjoint(sc, *s), "subtree copy is deep");
			nsubcopies++;
			if(not (sc.is_root() and sc.parent == nullptr)) nsubcopies_with_parent++;
		}
	}
	CHECK(nsubcopies_with_parent == 0,
	      nsubcopies_with_parent << " of " << nsubcopies << " copy-constructed subtrees kept the ORIGINAL's parent pointer "
	      "(BaseNode's copy ctor copies parent/pi): the copy is not a root, root() walks into the source tree, and the "
	      "pointer dangles once the source is destroyed. (Node::operator= deliberately does NOT copy parent/pi.)");

	// ==================================================================================
	section("move construction");
	for(int r=0;r<R;r++) {
		Node t = random_tree();
		const std::string before = shape(t);
		Node m(std::move(t));
		CHECK(shape(m) == before, "moved-into node has the source's value");
		CHECK(check_tree(m, "move-constructed"), "move-constructed invariants");
		// the moved-from object must still be destructible and assignable
		t = grammar.generate();
		CHECK(check_tree(t, "moved-from then reassigned"), "moved-from node is reusable");
		std::vector<Node> v;
		for(int k=0;k<20;k++) v.push_back(random_tree()); // forces reallocations (moves via noexcept move ctor)
		std::vector<std::string> shapes; for(auto& x : v) shapes.push_back(shape(x));
		for(int k=0;k<200;k++) v.push_back(Node());
		bool ok = true;
		for(int k=0;k<20;k++) ok = ok and shape(v[k]) == shapes[k] and check_tree(v[k], "vector element after reallocation");
		CHECK(ok, "nodes stored in a reallocating std::vector keep value and parent links");
		if(r > 50) break; // the vector part is slow-ish; 50 reps is plenty
	}

	// ==================================================================================
	section("copy assignment");
	for(int r=0;r<R;r++) {
		// (a) whole tree into whole tree
		Node a = random_tree(), b = random_tree();
		const std::string sb = shape(b);
		a = b;
		CHECK(shape(a) == sb and shape(b) == sb and a == b, "a = b copies the value");
		CHECK(disjoint(a, b) and check_tree(a, "a=b") and check_tree(b, "b after a=b"), "a = b is deep with valid links");
		CHECK(a.is_root(), "a stays a root after a = b");

		// (b) assignment INTO a node inside a tree keeps that node's place in the tree
		Node t = random_tree(3);
		if(Node* s = random_inner(t)) {
			Node* par = s->parent; const size_t pi = s->pi;
			Node rep = grammar.generate(s->nt());
			const std::string srep = shape(rep);
			*s = rep;
			CHECK(s->parent == par and s->pi == pi, "assigning into a subtree must keep its parent and index");
			CHECK(shape(*s) == srep and &par->child(pi) == s, "subtree now holds the assigned value");
			CHECK(check_tree(t, "assign into subtree"), "tree invariants after assigning into a subtree");
		}

		// (c) self assignment
		Node u = random_tree(); const std::string su = shape(u);
		Node& uref = u;
		u = uref;
		CHECK(shape(u) == su and check_tree(u, "self assignment"), "self copy-assignment is a no-op");

		// (d) promote a descendant: t = t.child(...)  (the source lives inside the destination)
		Node v = random_tree(3);
		if(Node* s = random_inner(v)) {
			const std::string ss = shape(*s);
			v = *s;
			CHECK(shape(v) == ss, "t = <descendant of t> gives the descendant's value");
			CHECK(v.is_root() and check_tree(v, "promote descendant (copy)"), "promote descendant invariants");
		}

		// (e) assign an ancestor INTO one of its own descendants
		Node w = random_tree(3);
		if(Node* s = random_inner(w)) {
			if(s->nt() == w.nt()) {
				const std::string sw = shape(w);
				*s = w; // s now holds a copy of the *old* w; w grows
				CHECK(shape(*s) == sw, "descendant = ancestor copies the ancestor's old value");
				CHECK(check_tree(w, "ancestor into descendant"), "ancestor-into-descendant invariants");
			}
		}
	}

	// ==================================================================================
	section("move assignment");
	for(int r=0;r<R;r++) {
		Node a = random_tree(), b = random_tree();
		const std::string sb = shape(b);
		a = std::move(b);
		CHECK(shape(a) == sb and check_tree(a, "a = move(b)") and a.is_root(), "a = std::move(b)");

		Node t = random_tree(3);
		if(Node* s = random_inner(t)) {
			Node* par = s->parent; const size_t pi = s->pi;
			Node rep = grammar.generate(s->nt());
			const std::string srep = shape(rep);
			*s = std::move(rep);
			CHECK(s->parent == par and s->pi == pi and shape(*s) == srep, "move-assigning into a subtree keeps its place");
			CHECK(check_tree(t, "move into subtree"), "tree invariants after move-assigning into a subtree");
		}

		Node u = random_tree(); const std::string su = shape(u);
		Node& uref = u;
		u = std::move(uref);
		CHECK(shape(u) == su and check_tree(u, "self move"), "self move-assignment is a no-op");

		Node v = random_tree(3);
		if(Node* s = random_inner(v)) {
			const std::string ss = shape(*s);
			v = std::move(*s);
			CHECK(shape(v) == ss and v.is_root() and check_tree(v, "promote descendant (move)"), "t = std::move(<descendant>)");
		}
	}

	// ==================================================================================
	section("Node::assign (keeps position, copies value)");
	for(int r=0;r<R;r++) {
		Node t = random_tree(3);
		if(Node* s = random_inner(t)) {
			Node* par = s->parent; const size_t pi = s->pi;
			Node rep = grammar.generate(s->nt()); rep.can_resample = flip();
			const std::string srep = shape(rep);
			s->assign(rep);
			CHECK(s->parent == par and s->pi == pi and shape(*s) == srep and shape(rep) == srep, "assign(Node&)");
			CHECK(check_tree(t, "assign(Node&)"), "assign(Node&) invariants");
			Node rep2 = grammar.generate(s->nt());
			const std::string srep2 = shape(rep2);
			s->assign(std::move(rep2));
			CHECK(s->parent == par and s->pi == pi and shape(*s) == srep2, "assign(Node&&)");
			CHECK(check_tree(t, "assign(Node&&)"), "assign(Node&&) invariants");
		}
	}

	// ==================================================================================
	section("set_child / push_back / swapping children");
	for(int r=0;r<R;r++) {
		Node t = random_tree(3);
		// pick a node with >= 2 children of the same type and swap two of them the way
		// Proposals::swap_args does: tmp = child(x); set_child(x, move(child(y))); set_child(y, move(tmp))
		Node* target = nullptr;
		for(auto& n : t) if(n.nchildren() >= 2 and n.child(0).nt() == n.child(1).nt()) { target = &n; break; }
		if(target) {
			const std::string s0 = shape(target->child(0)), s1 = shape(target->child(1));
			Node tmp = target->child(0);
			target->set_child(0, std::move(target->child(1)));
			target->set_child(1, std::move(tmp));
			CHECK(shape(target->child(0)) == s1 and shape(target->child(1)) == s0, "swapping two children via set_child(move)");
			CHECK(check_tree(t, "after swapping children"), "invariants after swapping children");
			// copy version
			Node c0 = target->child(0);
			target->set_child(1, c0);
			CHECK(shape(target->child(1)) == shape(c0) and disjoint(target->child(1), c0) and check_tree(t, "set_child copy"), "set_child(copy)");
			// a node set as its own sibling's value via lvalue
			target->set_child(0, target->child(1));
			CHECK(shape(target->child(0)) == shape(target->child(1)) and check_tree(t, "set_child from sibling"), "set_child from a sibling lvalue");
		}
		// building a tree with set_child from scratch
		Node a = grammar.makeNode(grammar.get_rule((nonterminal_t)0, "add(%s,%s)"));
		Node x = random_tree(), y = random_tree();
		const std::string sx = shape(x), sy = shape(y);
		a.set_child(0, x); a.set_child(1, std::move(y));
		CHECK(shape(a.child(0)) == sx and shape(a.child(1)) == sy and check_tree(a, "built with set_child") and a.is_complete(), "set_child builds trees");
		CHECK(dies([&]{ Node q = grammar.makeNode(grammar.get_rule((nonterminal_t)0, "add(%s,%s)")); Node b = grammar.makeNode(grammar.get_rule((nonterminal_t)1, "T")); q.set_child(0, b); }),
		      "set_child with the wrong type asserts");
	}
	{
		// std::swap of two roots
		Node a = random_tree(), b = random_tree();
		const std::string sa = shape(a), sb = shape(b);
		std::swap(a, b);
		CHECK(shape(a) == sb and shape(b) == sa and check_tree(a, "swap a") and check_tree(b, "swap b"), "std::swap");
		// swap of two subtrees in different trees (keeps each in its own tree)
		Node t1 = random_tree(3), t2 = random_tree(3);
		Node *s1 = nullptr, *s2 = nullptr;
		for(auto& n : t1) if(not n.is_root()) { s1 = &n; break; }
		for(auto& n : t2) if(not n.is_root() and s1 and n.nt() == s1->nt()) { s2 = &n; break; }
		if(s1 and s2) {
			const std::string a1 = shape(*s1), a2 = shape(*s2);
			std::swap(*s1, *s2);
			CHECK(shape(*s1) == a2 and shape(*s2) == a1, "std::swap of subtrees");
			CHECK(check_tree(t1, "swap subtree t1") and check_tree(t2, "swap subtree t2"), "trees valid after swapping subtrees");
		}
	}

	// ==================================================================================
	section("counts, depth, iteration from inner nodes");
	for(int r=0;r<R;r++) {
		Node t = random_tree();
		CHECK(t.count() == rec_count(t) and t.depth() == rec_depth(t), "count/depth");
		size_t term = 0; for(auto& n : t) term += n.is_terminal();
		CHECK(t.count_terminals() == term, "count_terminals");
		for(auto& n : t) {
			// iterating a subtree visits exactly that subtree
			std::vector<const Node*> ref; postfix(n, ref);
			std::vector<const Node*> it; for(const auto& m : n) it.push_back(&m);
			if(it != ref) { CHECK(false, "iterating an inner subtree"); break; }
			if(n.root() != &t) { CHECK(false, "root() from an inner node"); break; }
		}
		// count(n): number of subtrees equal to n
		Node* s = t.get_nth(myrandom((int)t.count()));
		size_t eq = 0; for(auto& n : t) eq += (n == *s);
		CHECK(t.count(*s) == eq, "count(n)");
	}

	// ==================================================================================
	section("==, <=> and hash are consistent");
	{
		std::vector<Node> v;
		for(int i=0;i<400;i++) v.push_back(random_tree(1, 8)); // small trees so that duplicates occur
		int dup = 0;
		for(size_t i=0;i<v.size();i++) for(size_t j=0;j<v.size();j++) {
			const bool eq = v[i] == v[j];
			const bool shape_eq = shape(v[i]) == shape(v[j]);
			const auto c = v[i] <=> v[j];
			const auto d = v[j] <=> v[i];
			// == ignores lp/can_resample (it compares rules), so compare against a rules-only view
			const bool same_rules = v[i].parseable() == v[j].parseable();
			if(i != j and eq) dup++;
			if(eq != same_rules) { CHECK(false, "== disagrees with structural equality: " << v[i] << " vs " << v[j]); }
			if(eq and v[i].hash() != v[j].hash()) { CHECK(false, "equal trees with different hashes"); }
			if((c == 0) != eq) { CHECK(false, "<=> == 0 must agree with ==: " << v[i] << " vs " << v[j]); }
			if((c < 0) != (d > 0)) { CHECK(false, "<=> must be antisymmetric"); }
			(void)shape_eq;
		}
		CHECK(dup > 0, "test had duplicate trees to compare");
		// transitivity via sorting: a sorted vector must be sorted under every pair
		auto w = v; std::sort(w.begin(), w.end(), [](const Node& a, const Node& b){ return (a <=> b) < 0; });
		bool sorted = true;
		for(size_t i=0;i<w.size();i++) for(size_t j=i+1;j<w.size();j++) sorted = sorted and not ((w[j] <=> w[i]) < 0);
		CHECK(sorted, "<=> is a consistent total order (sort is globally consistent)");
		std::set<Node> st(v.begin(), v.end());
		std::set<std::string> ss; for(auto& n : v) ss.insert(n.parseable());
		CHECK(st.size() == ss.size(), "std::set<Node> deduplicates exactly the structurally equal trees");
	}

	// ==================================================================================
	section("string() and parseable() round trips");
	size_t nusedot = 0, nusedot_bad = 0;
	for(int r=0;r<R;r++) {
		Node t = random_tree();
		Node p = grammar.from_parseable(t.parseable());
		CHECK(p == t and p.parseable() == t.parseable() and check_tree(p, "from_parseable"), "from_parseable(parseable()) round trip: " << t.parseable());
		// string() marks can_resample=false nodes with a dot; string(false) must be the plain program text
		if(t.string(false).find("\u2022") != std::string::npos) nusedot_bad++;
		nusedot++;
		bool lp_ok = true; for(auto& n : p) lp_ok = lp_ok and close(n.lp, grammar.log_probability(grammar.makeNode(n.rule)), 1e-15, 1e-15);
		CHECK(lp_ok, "parsed nodes get their rule's lp");
		Node plain = t; for(auto& n : plain) n.can_resample = true; // so string() has no markers
		Node s = grammar.simple_parse(plain.string());
		CHECK(s == t and check_tree(s, "simple_parse"), "simple_parse(string()) round trip: " << plain.string());
	}
	CHECK(nusedot_bad == 0, nusedot_bad << " of " << nusedot << " trees still print the can_resample dot with string(false): "
	      "Node::string(usedot) passes usedot only to the root -- children are printed with string() (default usedot=true)");
	{
		// partial trees with null nodes round trip through parseable too
		Node a = grammar.makeNode(grammar.get_rule((nonterminal_t)0, "add(%s,%s)"));
		a.set_child(1, grammar.generate());
		Node p = grammar.from_parseable(a.parseable());
		CHECK(p == a and p.child(0).is_null() and check_tree(p, "partial parse"), "partial tree parseable round trip");
		// a null gap whose expected type is NOT nonterminal 0 (the NullRule's own nt is always 0)
		Node f = grammar.makeNode(grammar.get_rule((nonterminal_t)0, "if(%s,%s,%s)"));
		f.set_child(1, grammar.generate()); f.set_child(2, grammar.generate()); // child 0 (a bool) stays null
		const std::string fp = f.parseable();
		CHECK(not dies([&]{ Node q = grammar.from_parseable(fp); }),
		      "from_parseable asserts on a partial tree whose null gap has a non-zero type (it compares the expected type to "
		      "the NullRule's nt, which is always 0): " << fp);
	}

	return finish();
}
