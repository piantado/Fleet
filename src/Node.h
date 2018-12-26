#pragma once

#include <functional>

#include "Grammar.h"

// Todo: replace grammar references wiht pointers

class Node {
	
protected:
	static const size_t MAX_CHILDREN = 3;
	
public:
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
		// take over all the resouces of k, and zero out k and then delete it
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
		delete k;
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
	T maxof( std::function<T(const Node*)>& f) const {
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
		f(this); // call first, in case f modifies my children
		for(size_t i=0;i<rule->N;i++) {
			if(child[i] != nullptr) child[i]->map(f);
		}
	}
		
	void map( std::function<void(Node*)>& f ) {
		f(this);
		for(size_t i=0;i<rule->N;i++) {
			if(child[i] != nullptr) child[i]->map(f);
		}
	}
	
	void map_conditionalrecurse( std::function<bool(Node*)>& f ) {
		// f here returns a function and we only recurse if the function returns true
		// This is often useful if we are modifying the tree and don't want to keep going once we've found something
		bool b = f(this);
		if(b){
			for(size_t i=0;i<rule->N;i++) {
				if(child[i] != nullptr) child[i]->map_conditionalrecurse(f);
			}
		}
	}
		
	size_t count_equal(const Node* n) {
		// how many of my descendants are equal to n?
		size_t cnt = 1;
		if(*this == *n) cnt++;
		for(size_t i=0;i<rule->N;i++) { // TODO: probably don't have to recurse if we are equal
			if(child[i] != nullptr) 
				cnt += child[i]->count_equal(n);
		}
		return cnt;
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
	virtual Node* get_nth(int& n) const { // default true on every node
		std::function<int(const Node* n)> t = [](const Node* n) { return 1;};
		return get_nth(n, t); 
	}
	
	
	
	virtual Node* __sample_helper(std::function<double(const Node*)>& f, double& r) {
		
		r -= f(this);
		if(r < 0.0) { 
			return this; 
		}
		else {
			for(size_t i=0;i<rule->N && r >= 0.0;i++) {
				if(child[i] != nullptr){
					auto q = child[i]->__sample_helper(f,r);
					if(q != nullptr) return q;
				}
			}
			return nullptr;
		}		
	}
	
	virtual Node* sample(std::function<double(const Node*)>& f) {
		// sample a subnode satisfying f and return its probability
		// where f maps each node to a probability (possibly zero)
		// NOTE: this does NOT return a copy
		
		double z = sum<double>(f);
		double r = z * uniform(rng);
		return __sample_helper(f,r);
	}
	
	
	virtual Node* copy_resample(const Grammar& g, bool f(const Node* n)) const {
		// this makes a copy of the current node where ALL nodes satifying f are resampled from the grammar
		// NOTE: this does NOT allow f to apply to nullptr children (so cannot be used to fill in)
		if(f(this)){
			return g.generate<Node>(rule->nt);
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
			auto pos = output.find(ChildStr);
			assert(pos != std::string::npos && "Node format must contain the ChildStr (typically='%s')"); // must contain the ChildStr for all children all children
			output.replace(pos, ChildStr.length(), (child[i]==nullptr ? nulldisplay : child[i]->string()));
		}
		return output;
	}
	
	virtual std::string parseable(std::string delim=":") const {
		// get a string like one we could parse
		std::string out = rule->format;
		for(size_t i=0;i<rule->N;i++) {
			if(child[i] == nullptr) {
				out += delim + ChildStr;
			} 
			else {
				out += delim + child[i]->parseable(delim);
			}
		}
		return out;
	}
	

	/********************************************************
	 * Operaitons for running programs
	 ********************************************************/

	virtual size_t program_size() const {
		// compute the size of the program -- just number of nodes plus special stuff for IF
		size_t n = 1; // I am one node
		if( (!rule->instr.custom) && rule->instr.builtin == op_IF) { n += 1; } // I have to add this many more instructions for an if
		for(size_t i=0;i<rule->N;i++) {
			n += (child[i] == nullptr ? 0 : child[i]->program_size());
		}
		return n;
	}
	
	virtual void linearize(Program &ops) const { 
		// convert tree to a linear sequence of operations 
		// to do this, we first linearize the kids, leaving their values as the top on the stack
		// then we compute our value, remove our kids' values to clean up the stack, and push on our return
		// the only fanciness is for if: here we will use the following layout 
		// <TOP OF STACK> <bool> op_IF(xsize) X-branch JUMP(ysize) Y-branch
		
		
		// and just a little checking here
		for(size_t i=0;i<rule->N;i++) {
			assert(child[i] != nullptr && "Cannot linearize a Node with null children");
			assert(child[i]->rule->nt == rule->child_types[i] && "Somehow the child has incorrect types"); // make sure my kids types are what they should be
		}
		
		// Main code
		if( (!rule->instr.custom) && rule->instr.builtin == op_IF) {
			assert(rule->N == 3 && "op_IF require three arguments"); // must have 3 parts
			
			int xsize = child[1]->program_size()+1; // must be +1 in order to skip over the JMP too
			int ysize = child[2]->program_size();
			assert(xsize < (1<<12) && "If statement jump size too large to be encoded in Instruction arg"); // these sizes come from the arg bitfield 
			assert(ysize < (1<<12) && "If statement jump size too large to be encoded in Instruction arg");
			
			child[2]->linearize(ops);
			
			// make the right instruction 
			// TODO: Assert that ysize fits 
			ops.push(Instruction(op_JMP,ysize));
			
			child[1]->linearize(ops);
			
			// encode jump
			ops.push(Instruction(op_IF, xsize)); 
			
			// evaluate the bool first so its on the stack when we get to if
			child[0]->linearize(ops);
			
		}
		else {
			Instruction i = rule->instr; // use this as a template
			ops.push(i);
			for(size_t i=0;i<rule->N;i++) {
				child[i]->linearize(ops);
			}
		}
		
	}
	
	virtual bool operator==(const Node &n) const{
		// Check equality between notes. Note that this compares the rule *pointers* so we need to be careful with 
		// serialization and storing/recovering full node trees with equality comparison
		
		if(rule != n.rule) 
			return false;
			
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
const std::string Node::nulldisplay = "\u2b1c"; // this is shown for partial trees

