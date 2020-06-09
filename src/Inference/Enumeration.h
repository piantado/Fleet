#pragma once

/* Functions for enumerating trees */

typedef size_t enumerationidx_t; // this is the type we use to store enuemration indices


class IntegerizedStack {
	// An IntegerizedStack is just a wrapper around unsigned longs that allow
	// them to behave like a stack, supporting push and pop (maybe with mod)
	// via a standard pairing function
	// (The name is because its not the same as an integer stack)
	
	typedef unsigned long value_t;

protected:
	value_t value;
	
public:
	IntegerizedStack(value_t v=0) : value(v) {
		
	}
	
	// A bunch of static operations
	//std::pair<enumerationidx_t, enumerationidx_t> cantor_decode(const enumerationidx_t z) {
	//	enumerationidx_t w = (enumerationidx_t)std::floor((std::sqrt(8*z+1)-1.0)/2);
	//	enumerationidx_t t = w*(w+1)/2;
	//	return std::make_pair(z-t, w-(z-t));
	//}

	static std::pair<enumerationidx_t, enumerationidx_t> rosenberg_strong_decode(const enumerationidx_t z) {
		// https:arxiv.org/pdf/1706.04129.pdf
		enumerationidx_t m = (enumerationidx_t)std::floor(std::sqrt(z));
		if(z-m*m < m) {
			return std::make_pair(z-m*m,m);
		}
		else {
			return std::make_pair(m, m*(m+2)-z);
		}
	}

	static enumerationidx_t rosenberg_strong_encode(const enumerationidx_t x, const enumerationidx_t y){
		auto m = std::max(x,y);
		return m*(m+1)+x-y;
	}

	static std::pair<enumerationidx_t,enumerationidx_t> mod_decode(const enumerationidx_t z, const enumerationidx_t k) {
		
		if(z == 0) 
			return std::make_pair(0,0); // I think?
		
		auto x = z%k;
		return std::make_pair(x, (z-x)/k);
	}

	static enumerationidx_t mod_encode(const enumerationidx_t x, const enumerationidx_t y, const enumerationidx_t k) {
		assert(x < k);
		return x + y*k;
	}

	// Stack-like operations on z
	static enumerationidx_t rosenberg_strong_pop(enumerationidx_t &z) {
		// https:arxiv.org/pdf/1706.04129.pdf
		enumerationidx_t m = (enumerationidx_t)std::floor(std::sqrt(z));
		if(z-m*m < m) {
			auto ret = z-m*m;
			z = m;
			return ret;
		}
		else {
			auto ret = m;
			z = m*(m+2)-z;
			return ret;
		}
	}
	static enumerationidx_t mod_pop(enumerationidx_t& z, const enumerationidx_t k) {			
		auto x = z%k;
		z = (z-x)/k;
		return x;
	}
	
	
	value_t pop() {
		auto u = rosenberg_strong_decode(value);
		value = u.second;
		return u.first;
	}

	value_t pop(value_t modulus) { // for mod decoding
		auto u = mod_decode(value, modulus);
		value = u.second;
		return u.first;
	}
	
	void push(value_t x) {
		auto before = value;
		value = rosenberg_strong_encode(x, value);
		assert(before <= value && "*** Overflow in encoding IntegerizedStack::push");
	}

	void push(value_t x, value_t modulus) {
		auto before = value;
		value = mod_encode(x, value, modulus);
		assert(before <= value && "*** Overflow in encoding IntegerizedStack::push");
	}
	
	value_t get_value() const {
		return value; 
	}
	
	bool empty() const {
		// not necessarily empty, just will forever return 0
		return value == 0;
	}
	
	void operator=(value_t z) {
		// just set my value
		value = z;
	}
	void operator-=(value_t x) {
		// subtract off my value
		value -= x;
	}
	void operator+=(value_t x) {
		// add to value
		value += x;
	}
};
std::ostream& operator<<(std::ostream& o, const IntegerizedStack& n) {
	o << n.get_value();
	return o;
}	

	
template<typename Grammar_t>
Node expand_from_integer(Grammar_t* g, nonterminal_t nt, IntegerizedStack& is) {
	// this is a handy function to enumerate trees produced by the grammar. 
	// This works by encoding each tree into an integer using some "pairing functions"
	// (see https://arxiv.org/pdf/1706.04129.pdf)
	// Specifically, here is how we encode a tree: if it's a terminal,
	// encode the index of the terminal in the grammar. 
	// If it's a nonterminal, encode the rule with a modular pairing function
	// and then use Rosenberg-Strong encoding to encode each of the children. 
	// In this way, we can make a 1-1 pairing between trees and integers,
	// and therefore store trees as integers in a reversible way
	// as well as enumerate by giving this integers. 
	
	// NOTE: for now this doesn't work when nt only has finitely many expansions/trees
	// below it. Otherwise we'll get an assertion error eventually.
	
	enumerationidx_t numterm = g->count_terminals(nt);
	if(is.get_value() < numterm) {
		return g->makeNode(g->get_rule(nt, is.get_value()));	// whatever terminal we wanted
	}
	else {
		
		is -= numterm; // remove terminals from is
		auto ri =  is.pop(g->count_nonterminals(nt));	
//		COUT nt TAB g->count_nonterminals(nt) TAB ri TAB numterm ENDL;
		Rule* r = g->get_rule(nt, ri+numterm); // shift index from terminals (because they are first!)
		Node out = g->makeNode(r);
		for(size_t i=0;i<r->N;i++) {
			// note that this recurses on the z format -- so it makes a new IntegerizedStack for each child
			out.set_child(i, expand_from_integer(g, r->type(i), i==r->N-1 ? is.get_value() : is.pop()) ) ; 
		}
		return out;					
	}

}

template<typename Grammar_t>
Node expand_from_integer(Grammar_t* g, nonterminal_t nt, enumerationidx_t z) {
	IntegerizedStack is(z);
	return expand_from_integer(g, nt, is);
}
	
template<typename Grammar_t>
enumerationidx_t compute_enumeration_order(Grammar_t* g, const Node& n) {
	// inverse of the above function -- what order would we be enumerated in?
	if(n.nchildren() == 0) {
		return g->get_index_of(n.rule);
	}
	else {
		// here we work backwards to encode 
		nonterminal_t nt = n.rule->nt;
		enumerationidx_t numterm = g->count_terminals(nt);
		
		IntegerizedStack is(compute_enumeration_order(g, n.child(n.rule->N-1)));
		for(int i=n.rule->N-2;i>=0;i--) {
			is.push(compute_enumeration_order(g, n.child(i)));
		}
		is.push(g->get_index_of(n.rule)-numterm, g->count_nonterminals(nt));
		is += numterm;
		return is.get_value();
	}
}

/*
	Node lempel_ziv_full_expand(nonterminal_t nt, enumerationidx_t z, Node* root=nullptr) const {
		IntegerizedStack is(z);
		return lempel_ziv_full_expand(nt, is, root);
	}
		
	Node lempel_ziv_full_expand(nonterminal_t nt, IntegerizedStack& is, Node* root=nullptr) const {
		// This implements "lempel-ziv" decoding on trees, where each integer
		// can be a reference to prior *full* subtree
//#ifdef BLAHBLAH		
		// First, lowest numbers will reference prior complete subtrees
		// that are not terminals
//		std::function is_full_tree = [&](const Node& ni) -> int {
//			return 1*(ni.rule->nt == nt and (not ni.is_terminal()) and ni.is_complete() );
//		};
//		
//		// might be a reference to a prior full tree
//		int t = (root == nullptr ? 0 : root->sum(is_full_tree));
//		if(is.get_value() < (size_t)t) {
//			return *root->get_nth(is.get_value(), is_full_tree); 
//		}
//		is -= t;
//
//		// next numbers are terminals first integers encode terminals
//		enumerationidx_t numterm = count_terminals(nt);
//		if(is.get_value() < numterm) {
//			return makeNode(this->get_rule(nt, is.get_value() ));	// whatever terminal we wanted
//		}
//		is -= numterm;
//
//		// now we choose whether we have encoded children directly
//		// or a reference to prior *subtree* 
//		if(root != nullptr and is.pop(2) == 0) {
//			
//			// count up the number
//			size_t T=0;
//			for(auto& n : *root) {
//				if(n.nt() == nt) {
//					T += count_connected_partial_subtrees(n)-1; // I could code any of these subtrees, but not their roots (so -1)
//				}
//			}
//			
//			// which subtree did I get?
//			int t = is.pop(T);
//
//			// ugh go back and find it
//			//Node out;
//			for(auto& n : *root) {
//				if(n.nt() == nt) {
//					T += count_connected_partial_subtrees(n)-1; // I could code any of these subtrees, but not their roots (so -1)
//				}
//			}
//			
//			auto out = copy_connected_partial_subtree(*root, t); // get this partial tree
//			
//			// TODO: Make it so that a subtree we reference cannot be ONE node (so therefore
//			//       the node is not null)
//			
//			// and now fill it in
//			if(out.is_null()) {
//				out = lempel_ziv_full_expand(out.nt(), is.get_value(), root);
//			}
//			else {
//				
//				std::function is_null = +[](const Node& ni) -> size_t { return 1*ni.is_null(); };
//				auto b = out.sum(is_null);
//
//				for(auto& ni : out) {
//					if(ni.is_null()) {
//						auto p = ni.parent; // need to set the parent to the new value
//						auto pi = ni.pi;
//						--b;
//						// TODO: NOt right because the parent links are now broken
//						p->set_child(pi, lempel_ziv_full_expand(p->rule->type(pi), (b==0 ? is.get_value() : is.pop()), root));
//					}
//				}
//			}
//			
//			return out;
//		}
//		else { 
//			// we must have encoded a normal tree expansion
//
//			// otherwise just a direct coding
//			Rule* r = this->get_rule(nt, is.pop(count_nonterminals(nt))+numterm); // shift index from terminals (because they are first!)
//			
//			// we need to fill in out's children with null for it to work
//			Node out = makeNode(r);
//			out.fill(); // retuired below because we'll be iterating		
//			if(root == nullptr) 
//				root = &out; // if not defined, out is our root
//			
//			for(size_t i=0;i<r->N;i++) {
//				out.set_child(i, lempel_ziv_full_expand(r->type(i), i==r->N-1 ? is.get_value() : is.pop(), root));
//			}
//			return out;		
//		}
////#endif
		
		
		return Node(); // just to avoid warning
		
		
	}


//	Node lempel_ziv_full_expand(nonterminal_t nt, enumerationidx_t z, Node* root=nullptr) const {
//		IntegerizedStack is(z);
//		return lempel_ziv_full_expand(nt, is, root);
//	}
//		
//	Node lempel_ziv_full_expand(nonterminal_t nt, IntegerizedStack& is, Node* root=nullptr) const {
//		// This implements "lempel-ziv" decoding on trees, where each integer
//		// can be a reference to prior *full* subtree
//		
//		// First, lowest numbers will reference prior complete subtrees
//		// that are not terminals
//		std::function is_full_tree = [&](const Node& ni) -> int {
//			return 1*(ni.rule->nt == nt and (not ni.is_terminal()) and ni.is_complete() );
//		};
//		
//		std::function is_partial_tree = [&](const Node& ni) -> int {
//			return 1*(ni.rule->nt == nt and (not ni.is_terminal()) and (not ni.is_complete()) );
//		};
//			
//		int t = 0;
//		if(root != nullptr) 
//			t = root->sum(is_full_tree);
//		
//		if(is.get_value() < (size_t)t) {
//			return *root->get_nth(is.get_value(), is_full_tree); 
//		}
//		is -= t;
//		
//		// next numbers are terminals first integers encode terminals
//		enumerationidx_t numterm = count_terminals(nt);
//		if(is.get_value() < numterm) {
//			return makeNode(this->get_rule(nt, is.get_value() ));	// whatever terminal we wanted
//		}
//		is -= numterm;
//					
//		
//		// otherwise just a direct coding
//		Rule* r = this->get_rule(nt, is.pop(count_nonterminals(nt))+numterm); // shift index from terminals (because they are first!)
//		
//		// we need to fill in out's children with null for it to work
//		Node out = makeNode(r);
//		out.fill(); // retuired below because we'll be iterating		
//		if(root == nullptr) 
//			root = &out; // if not defined, out is our root
//		
//		for(size_t i=0;i<r->N;i++) {
//			out.set_child(i, lempel_ziv_full_expand(r->type(i), i==r->N-1 ? is.get_value() : is.pop(), root));
//		}
//		return out;		
//		
//	}


//	Node lempel_ziv_partial_expand(nonterminal_t nt, enumerationidx_t z, Node* root=nullptr) const {
//		IntegerizedStack is(z);
//		return lempel_ziv_partial_expand(nt, is, root);
//	}
//	Node lempel_ziv_partial_expand(nonterminal_t nt, IntegerizedStack& is, Node* root=nullptr) const {
//		// coding where we expand to a partial tree -- this is easier because we can more easily reference prior
//		// partial trees
//		
//		// our first integers encode terminals
//		enumerationidx_t numterm = count_terminals(nt);
//		if(is.get_value() < numterm) {
//			return makeNode(this->get_rule(nt, is.get_value() ));	// whatever terminal we wanted
//		}
//		
//		is -= numterm; // we weren't any of those 
//		
//		// then the next encode some subtrees
//		if(root != nullptr) {
//			
//			auto t = count_partial_subtrees(*root);
//			if(is.get_value() < t) {
//				return copy_partial_subtree(*root, is.get_value() ); // just get the z'th tree
//			}
//			
//			is -= t;
//		}
//		
//		// now just decode		
//		auto ri = is.pop(count_nonterminals(nt)); // mod pop		
//		Rule* r = this->get_rule(nt, ri+numterm); // shift index from terminals (because they are first!)
//		
//		// we need to fill in out's children with null for it to work
//		Node out = makeNode(r);
//		out.fill(); // returned below because we'll be iterating		
//		
//		if(root == nullptr) 
//			root = &out; // if not defined, out is our root
//		
//		for(size_t i=0;i<r->N;i++) {
//			out.set_child(i, lempel_ziv_partial_expand(r->type(i), is.get_value() == r->N-1 ? is.get_value() : is.pop(), root)); // since we are by reference, this should work right
//		}
//		return out;		
//		
//	}
		
	// ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
	// Subtrees
	// ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

	virtual enumerationidx_t count_connected_partial_subtrees(const Node& n) const {
		// how many possible connected, partial subtrees do I have
		// (e.g. including a path back to my root)
				
		assert(not n.is_null()); // can't really be connected if I'm null
	
		enumerationidx_t k=1; 
		for(enumerationidx_t i=0;i<n.nchildren();i++){
			if(not n.child(i).is_null())  {
				// for each child independently, I can either make them null, or I can include them
				// and choose one of their own subtrees
				k = k * count_connected_partial_subtrees(n.child(i)); 
			}
		}
		return k+1; // +1 because I could make the root null
	}


//	virtual Node copy_connected_partial_subtree(const Node& n, IntegerizedStack is) const {
//		// make a copy of the z'th partial subtree 
// 
//		if(is.get_value() == 0) 
//			return Node(); // return null for me
//		
//		Node out = makeNode(n.rule); // copy the first level
//		for(enumerationidx_t i=0;i<out.nchildren();i++) {
//			out.set_child(i, copy_connected_partial_subtree(n.child(i), (i==out.nchildren() ? is.get_value() : is.pop() )));
//		}
//		return out;
//	}
//	


*/
