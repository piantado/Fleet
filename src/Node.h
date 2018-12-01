#ifndef NODE_H
#define NODE_H

#include<functional>

#include "Program.h"
#include "Grammar.h"

// Todo: replace grammar references wiht pointers

class Node {
	
protected:
	static const size_t MAX_CHILDREN = 3;
	
public:
	static const std::string child_str; // what does format use to signify children's values?
	static const std::string nulldisplay;// "?"; // what to print in trees for null nodes?

	Node*        child[MAX_CHILDREN];
	const Rule*  rule; // which rule did I use?
	double       lp; 
	bool         can_resample;
	
	Node(const Rule* r, double _lp, bool cr=true) : rule(r), lp(_lp), can_resample(cr) {
		// NOTE: This constructor is used to make a copy of a node but not its children
		// so all class members should be here
		// to handle partial trees, we automatically fill in nullptrs, which counts as partial values
		zero();
	}
	
	Node(const Node& n){
		assert(false && "Do not use a copy construtor for Node"); // let's not use a copy constructor
	}
	
	virtual ~Node() {
		for(size_t i=0;i<rule->N;i++) {
			if(child[i] != nullptr)	
				delete child[i];
		}
	}
	
	virtual void takeover(Node* k) {
		// take over all the resouces of k, and zero out k (without deleting)
		// TODO: use a move operator here?
		
		for(size_t i=0;i<rule->N;i++) { // first, clear out my own children
			delete child[i];
		}

		rule = k->rule;
		lp = k-> lp;
		can_resample = k->can_resample;
		
		for(size_t i=0;i<rule->N;i++) {
			child[i] = k->child[i];
		}
		k->zero();
	}
	
	virtual void zero() {
		// set all my children to nullptr, but DO NOT delete them
		for(size_t i=0;i<rule->N;i++) {
			child[i] = nullptr;
		}
	}
	
	virtual Node* copy() const {
		auto ret = new Node(rule, lp, can_resample); // copy myself and not my children
		for(size_t i=0;i<rule->N;i++) {
			ret->child[i] = (child[i] == nullptr ? nullptr : child[i]->copy());
		}
		return ret;
	}
	
	
	// Returns the max of applying f to all of the non-null nodes 
	template<typename T>
	T maxof( T f(Node*) ) {
		T mx = f(this);
		for(size_t i=0;i<rule->N;i++) {
			if(child[i] != nullptr) {
				T m = child[i]->maxof(f);
				if(m > mx) mx = m;
			}
		}
		return mx;
	}
	
	
	bool all( bool f(Node*) ) {
		if(!f(this)) return false;
		for(size_t i=0;i<rule->N;i++) {
			if(child[i] != nullptr && (!child[i]->all(f)))
				return false;
		}
		return true;
	}
	
	template<typename T>
	T sum(std::function<T(const Node*)>& f ) const {
		T s = f(this);
		for(size_t i=0;i<rule->N;i++) {
			s += (child[i] == nullptr ? 0 : child[i]->sum<T>(f));
		}
		return s;
	}

	
	void map( void f(Node*) ) {
		// NOTE: Because map calls f first on this, it allows us to modify the tree if we want to. 
		f(this);
		for(size_t i=0;i<rule->N;i++) {
			if(child[i] != nullptr) 
				child[i]->map(f);
		}
	}
		
	virtual size_t count() const {
		std::function<size_t(const Node* n)> one = [](const Node* n){return (size_t)1;};
		return sum<size_t>( one );
	}

	virtual bool is_evaluable() const {
		// does this have any subnodes below that are null?
		for(size_t i=0;i<rule->N;i++) {
			if(child[i] == nullptr || !child[i]->is_evaluable() ) return false;
		}
		return true;
	}
		
	virtual Node* get_nth(int& n, std::function<int(const Node*)>& f) const {
		// return a pointer to the nth  child satisfying f (f's output cast to bool)
		// this uses nullptr as a setinel that the nth is not below us
		if(f(this)) {
			if(n == 0) {
				return  (Node*)this;
			}
			else {
				--n;
			}
		}
		
		for(size_t i=0;i<rule->N;i++) {
			if(child[i] != nullptr){
				Node* x = child[i]->get_nth(n,f);
				if(x != nullptr) return x;				
			}
		}
		return nullptr; // signal to above that the nth si not here. 
	}
	
	
	virtual Node* copy_resample(const Grammar& g, bool f(const Node* n)) const {
		// this makes a copy of the current node where ALL nodes satifying f are resampled from the grammar
		// NOTE: this does NOT allow f to apply to nullptr children (so cannot be used to fill in)
		if(f(this)){
			return g.generate<Node>(rule->nonterminal_type);
		}
		
		// otherwise normal copy
		auto ret = new Node(rule, lp, can_resample);
		for(size_t i=0;i<rule->N;i++){
			ret->child[i] = (child[i] == nullptr ? nullptr : child[i]->copy_resample(g, f));
		}
		return ret;
	}
		

	virtual std::string string() const {
		// This converts my children to strings and then substitutes into rule->format using simple string substitution.
		// the format just searches and replaces %s for each successive child. NOTE: That this therefore does NOT use the
		// full power of printf formatting
		std::string output(rule->format);
		for(size_t i=0;i<rule->N;i++) {
			auto pos = output.find(child_str);
			assert(pos != std::string::npos && "Node format must contain the child_str (typically='%s')"); // must contain the child_str for all children all children
			output.replace(pos, child_str.length(), (child[i]==nullptr ? nulldisplay : child[i]->string()));
		}
		return output;
	}
	
//	virtual bool structural_lt(const Node* n) {
//		// test whether I am < n in an ordering determined by our rules. 
//		// This should not satisfy !(a<b) && !(b<a)  unless a==b
//		// this is used in LOTHypothesis to deterime < if the posteriors are equal
//		if(rule->id != n->rule->id) 
//			return rule->id < n->rule->id;
//		
//		if(rule->N != n->rule->N) 
//			return rule->N < n->rule->N;
//			
//		// else we must have equally many children
//		for(size_t i=0;i<rule->N;i++) {
//			bool b = child[i]->structural_lt(n->child[i]);
//			bool c = n->child[i]->structural_lt(child[i]);
//			
//			if((!b) && (!c)) { // e.g if !(b>=c && c>=b) or in other words !(b==c)
//				return b;
//			}	
//			else {
//				continue; // we have to be equal
//			}
//		}
//		return false; // we are totally equal so not lt
//	}
//	
	/********************************************************
	 * Operaitons for running programs
	 ********************************************************/

	virtual size_t program_size() const {
		// compute the size of the program -- just number of nodes plus special stuff for IF
		size_t n = 1; // I am one node
		if(rule->op == op_IF) { n += 3; } // I have to add this many more instructions for an if
		for(size_t i=0;i<rule->N;i++) {
			n += (child[i] == nullptr ? 0 : child[i]->program_size());
		}
		return n;
	}
	
	virtual void linearize(Opstack &ops) const { 
		// convert tree to a linear sequence of operations 
		// to do this, we first linearize the kids, leaving their values as the top on the stack
		// then we compute our value, remove our kids' values to clean up the stack, and push on our return
		// the only fanciness is for if: here we will use the following layout 
		// <TOP OF STACK> <bool> op_IF xsize [X-branch] JUMP ysize [Y-branch]
		
		
		// and just a little checking here
		for(size_t i=0;i<rule->N;i++) {
			assert(child[i] != nullptr && "Cannot linearize a Node with null children");
			assert(child[i]->rule->nonterminal_type == rule->child_types[i] && "Somehow the child has incorrect types"); // make sure my kids types are what they should be
		}
		
		// Main code
		if(rule->op == op_IF) {
			assert(rule->N == 3 && "op_IF require three arguments"); // must have 3 parts
			
			int xsize = child[1]->program_size()+2; // must be +2 in order to skip over the [JMP ysize] part too
			int ysize = child[2]->program_size();
			
			// TODO: should assert that these sizes fit in whatever an op_t is
			
			child[2]->linearize(ops);
			ops.push( op_t(ysize) );
			ops.push( op_JMP );
			child[1]->linearize(ops);
			
			ops.push((op_t) xsize);
			
			ops.push(op_IF); // encode jump
			
			// evaluate the bool first so its on the stack when we get to if
			child[0]->linearize(ops);
			
		}
		else {
			ops.push(rule->op);
			for(size_t i=0;i<rule->N;i++) {
				child[i]->linearize(ops);
			}
		}
		
	}
	
	virtual bool operator==(const Node &n) const{
		// Check equality between notes. Note that this compares the rule *pointers* so we need to be careful with 
		// serialization and storing/recovering full node trees with equality comparison
		if(rule == n.rule) return false;
		for(size_t i=0;i<rule->N;i++){
			if(!(child[i]->operator==(*n.child[i]))) return false;
		}
		return true;
	}

	virtual size_t count_equal_child(const Node* n) const {
		// how many of my IMMEDIATE children are equal to n?
		size_t cnt = 0;
		for(size_t i=0;i<rule->N;i++) {
			cnt += (child[i] == n);
		}
		return cnt;
	}


	virtual size_t hash() const {
		// Like equality checking, hashing also uses the rule pointers' numerical values, so beware!
		size_t output = (size_t) rule; // tunrs out, this is actually important to prevent hash collisions when rule_id and i are small
		for(size_t i=0;i<rule->N;i++) {
			output = hash_combine(output, hash_combine(i, (child[i] == nullptr ? i : child[i]->hash())));
		}
		return output;
	}

	
	/********************************************************
	 * Enumeration
	 ********************************************************/

	
	virtual size_t neighbors(const Grammar& g) const {
		// How many neighbors do I have? We have to find every gap (nullptr child) and count the ways to expand each
		size_t n=0;
		for(size_t i=0;i<rule->N;i++){
			n += (child[i] == nullptr ? g.count_expansions(rule->child_types[i]) : child[i]->neighbors(g) );
		}
		return n;
	}
	
	virtual size_t first_neighbors(const Grammar& g) const {
		// How many neighbors does my first gap have?
		for(size_t i=0;i<rule->N;i++){
			if(child[i] == nullptr)
				return g.count_expansions(rule->child_types[i]);
			
			size_t n = child[i]->first_neighbors(g);			
			if(n > 0) return n;
		}
		return 0;
	}
	
	virtual void expand_to_neighbor(const Grammar& g, int& which) {
		// here we find the neighbor indicated by which and expand it into the which'th neighbor
		// to do this, we loop through until which is less than the number of neighbors,
		// and then it must specify which expansion we want to take. This means that when we
		// skip a nullptr, we have to subtract from it the number of neighbors (expansions)
		// we could have taken. 
		for(size_t i=0;i<rule->N;i++){
			if(child[i] == nullptr) {
				int c = g.count_expansions(rule->child_types[i]);
				if(which >= 0 && which < c) {
					auto r = g.get_expansion(rule->child_types[i], which);
					child[i] = g.make<Node>(r);
				}
				which -= c;
			}
			else { // otherwise we have to process that which
				child[i]->expand_to_neighbor(g,which);
			}
		}
	}
	
	virtual void complete(const Grammar& g) {
		// go through and fill in the tree at random
		for(size_t i=0;i<rule->N;i++){
			if(child[i] == nullptr) {
				child[i] = g.generate<Node>(rule->child_types[i]);
			}
			else {
				child[i]->complete(g);
			}
		}
	}
	
	
};
const std::string Node::child_str = "%s";// we search for this in format strings in order to display a node
const std::string Node::nulldisplay = "\u2b1c"; // this is shown for partial trees


#endif