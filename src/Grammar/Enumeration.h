#pragma once

#include "FleetStatistics.h"
#include "IntegerizedStack.h"

/**
 * @class EnumerationInterface
 * @author piantado
 * @date 02/01/21
 * @file Enumeration.h
 * @brief This interface an easy enumeratino interface which provides an abstract class for various
 * 		  types of enumeration. Most need to know about the grammar type.  
 * 
 * 		  NOTE: This is NOT the infernece scheme -- for that see Inference/Enumeration.h
 */
template<typename Grammar_t>
class EnumerationInterface {
public:
	Grammar_t* grammar;

	EnumerationInterface(Grammar_t* g) : grammar(g) {

	}

	/**
	 * @brief Subclass must implement for IntegerizedStack
	 * @param nt
	 * @param is
	 * @return 
	 */
	virtual Node toNode(nonterminal_t nt, IntegerizedStack& is)  {
		throw NotImplementedError("*** Subclasses must implement toNode");
	}
	virtual Node toNode(nonterminal_t nt, const Node& frm, IntegerizedStack& is) {
		throw NotImplementedError("*** Subclasses must implement toNode");
	}
	
	// ensure these don't get called 
	//virtual Node toNode(nonterminal_t nt, IntegerizedStack is) = delete;
	//virtual Node toNode(nonterminal_t nt, const Node& frm, IntegerizedStack is) = delete; 
	
	/**
	 * @brief Expand an integer to a node using this nonterminal type. 
	 * @param nt
	 * @param z
	 * @return 
	 */
	virtual Node toNode(nonterminal_t nt, enumerationidx_t z) {
		++FleetStatistics::enumeration_steps;
	
		IntegerizedStack is(z);
		return toNode(nt, is);
	}
	
	virtual Node toNode(nonterminal_t nt, const Node& frm, enumerationidx_t z) {
		++FleetStatistics::enumeration_steps;
	
		IntegerizedStack is(z);
		return toNode(nt, frm, is);
	}
	
	/**
	 * @brief Convert a node to an integer (should be the inverse of toNode)
	 * @param n
	 * @return 
	 */
	virtual enumerationidx_t toInteger(const Node& n) {
		throw NotImplementedError("*** Subclasses must implement toInteger(Node&)");
	}
	
	/**
	 * @brief Convert a node to an integer where we come from something
	 * @param frm
	 * @param n
	 * @return 
	 */
	virtual enumerationidx_t toInteger(const Node& frm, const Node& n) {
		throw NotImplementedError("*** Subclasses must implement toInteger(Node&,Node&)");
	}
	
	
};


template<typename Grammar_t>
class FullCFGEnumeration : public EnumerationInterface<Grammar_t> {
public:
	FullCFGEnumeration(Grammar_t* g) : EnumerationInterface<Grammar_t>(g) {}
	
	/**
	 * @brief This is a handy function to enumerate trees produced by the grammar. 
	 *  This works by encoding each tree into an integer using some "pairing functions" 
	 *  (see https://arxiv.org/pdf/1706.04129.pdf)
	 * 	Specifically, here is how we encode a tree: if it's a terminal,
	 * 	encode the index of the terminal in the grammar. 
	 * 	If it's a nonterminal, encode the rule with a modular pairing function
	 * 	and then use Rosenberg-Strong encoding to encode each of the children. 
	 * 	In this way, we can make a 1-1 pairing between trees and integers,
	 * 	and therefore store trees as integers in a reversible way
	 * 	as well as enumerate by giving this integers. 
	 */
	virtual Node toNode(nonterminal_t nt, IntegerizedStack& is) override {
	
		// NOTE: for now this doesn't work when nt only has finitely many expansions/trees
		// below it. Otherwise we'll get an assertion error eventually.
		enumerationidx_t numterm = this->grammar->count_terminals(nt);
		if(is.get_value() < numterm) {
			return this->grammar->makeNode(this->grammar->get_rule(nt, is.get_value()));	// whatever terminal we wanted
		}
		else {
			// Otherwise it is encoding numterm+which_terminal
			is -= numterm; 
			
			auto ri = is.pop(this->grammar->count_nonterminals(nt)); // this already includes the shift from all the nonterminals
			
			Rule* r = this->grammar->get_rule(nt, ri+numterm); // shift index from terminals (because they are first!)
			Node out = this->grammar->makeNode(r);
			for(size_t i=0;i<r->N;i++) {
				// note that this recurses on the enumerationidx_t format -- so it makes a new IntegerizedStack for each child
				out.set_child(i, toNode(this->grammar, r->type(i), i==r->N-1 ? is.get_value() : is.pop()) ) ; 
			}
			
			return out;					
		}
	}
	
	virtual enumerationidx_t toInteger(const Node& n) override {
		// inverse of the above function -- what order would we be enumerated in?
		if(n.nchildren() == 0) {
			return this->grammar->get_index_of(n.rule);
		}
		else {
			// here we work backwards to encode 
			nonterminal_t nt = n.rule->nt;
			enumerationidx_t numterm = this->grammar->count_terminals(nt);
			
			IntegerizedStack is(toInteger(this->grammar, n.child(n.rule->N-1)));
			for(int i=n.rule->N-2;i>=0;i--) {
				is.push(toInteger(this->grammar, n.child(i)));
			}
			is.push(this->grammar->get_index_of(n.rule)-numterm, this->grammar->count_nonterminals(nt));
			is += numterm;
			return is.get_value();
		}
	}
};


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
	
*/	
// ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// Subtrees
// ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~


/**
 * @class SubtreeEnumeration
 * @author piantado
 * @date 02/01/21
 * @file Enumeration.h
 * @brief Enumerate subtrees of a given tree
 */
template<typename Grammar_t>
class SubtreeEnumeration : public EnumerationInterface<Grammar_t> {
	using Super = EnumerationInterface<Grammar_t>;
public:
	SubtreeEnumeration(Grammar_t* g) : Super(g) {}
		
	/**
	 * @brief How many partial subtrees are there?
	 * @param n
	 * @return 
	 */
	[[nodiscard]] enumerationidx_t count(const Node& n) {
		// how many possible connected, partial subtrees do I have
		// (e.g. including a path back to my root)
				
		if(n.is_null()) 
			return 1; // only one tree there!

		enumerationidx_t k=1; 
		for(const auto& c : n.get_children() ){
			// for each child independently, I can either make them null, or I can include them
			// and choose one of their own subtrees
			k *= count(c); 
		}
		return k+1; // +1 because I could make the root null
	}

	/**
	 * @brief Convert to the is'th partial subtree of frm
	 * @param nt
	 * @param is
	 * @return 
	 */
	[[nodiscard]] virtual Node toNode(nonterminal_t nt, const Node& frm, IntegerizedStack& is) override {
		if(is.get_value() == 0) {
			return Node();
		}
		else {
			is -= 1; // is encodes tree+1
			Node out = this->grammar->makeNode(frm.rule); // copy the first level
			for(size_t i=0;i<out.nchildren();i++) {
				auto cz = count(frm.child(i));
				out.set_child(i, toNode(frm.type(i), frm.child(i), is.pop(cz) ));		
			}
			return out;
		}
	}

	// Required here or else we get z converted implicity to IntegerizedStack
	[[nodiscard]] virtual Node toNode(nonterminal_t nt, const Node& frm, enumerationidx_t z) override {
		return Super::toNode(nt,frm,z);
	}
	

	[[nodiscard]] virtual enumerationidx_t toInteger(const Node& frm, const Node& n) override {
		if(n.is_null()) {
			return 0;
		}
		else {
			// else it's a real construction
			
			const int N = n.nchildren();
			assert( (int)frm.nchildren() == N);
		
			IntegerizedStack is;
			
			// We have to go in reverse order here, remember, since that's hwo things are unpacked
			for(int i=N-1;i>=0;i--) {
			
				// the most we can pop from a child is the number of partial trees they have
				auto cx = count(frm.child(i));
				auto k = toInteger(frm.child(i), n.child(i));
				
				is.push(k, cx);
				
				//COUT ">>" TAB k TAB cx TAB is.get_value() TAB "[" << i << "] " << n.child(i) << " in " << n.string() ENDL;
			}
			
			// 1+ becuase it wasn't null
			return 1+is.get_value();		
		}
		
	}

};


 