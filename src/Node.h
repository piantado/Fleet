#ifndef NODE_H
#define NODE_H

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
		assert(false); // let's not use a copy constructor
	}
	
	virtual ~Node() {
		for(size_t i=0;i<rule->N;i++) {
			if(child[i] != nullptr)	
				delete child[i];
		}
	}
	
	virtual void zero() {
		// set all my children to nullptr
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
	T sum( T f(const Node*) ) const {
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
	
		
	virtual double log_probability(const Grammar& g) const {
		// compute the log probability under the grammar g
		// NOTE: this ignores empty nodes
		double s = log(rule->p) - log(g.rule_normalizer(rule->nonterminal_type));
		for(size_t i=0;i<rule->N;i++){
			s += (child[i] == nullptr ? 0.0 : child[i]->log_probability(g));
		}
		return s;
	}
	
	virtual size_t count() const {
		return sum<size_t>( [](const Node* n){return (size_t)1;} );
	}
	virtual size_t count_can_propose() const {
		return sum<size_t>( [](const Node* n){ return (size_t)(n->can_resample?1:0); } );
	}
	
	virtual bool is_complete() const {
		// does this have any subnodes below that are null?
		for(size_t i=0;i<rule->N;i++) {
			if(child[i] == nullptr || !child[i]->is_complete() ) return false;
		}
		return true;
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
		
	virtual Node* copy_resample(const Grammar& g, int& which) const {
		// this makes a copy of the current node where the which'th node has been resampled from the grammar
		if(can_resample){
			if(which == 0) {
				--which; // must remember to decrement so we don't keep resampling
				return g.generate<Node>(rule->nonterminal_type);
			}
			--which;
		}
		
		// otherwise normal copy
		auto ret = new Node(rule, lp, can_resample);
		for(size_t i=0;i<rule->N;i++){
			ret->child[i] = (child[i] == nullptr ? nullptr : child[i]->copy_resample(g, which));
		}
		return ret;
	}
	virtual Node* copy_resample(const Grammar& g) const {
		int N = (int)count_can_propose(); // TODO: assuming we can fit size_t into int
		if(N == 0) return copy(); // well I can't do anything
		std::uniform_int_distribution<int> r(0, N-1);
		int w = r(rng);
		return copy_resample(g,w);
	}
	
	
	
	
	
	
//	virtual Node* copy_increment(const Grammar& g, bool& carry) const {
//		// counting in base grammar g
//		auto ret = new Node(rule, lp, can_resample); 
//		for(size_t i=0;i<rule->N;i++){
//			
//		}
//	}
	

	virtual std::string string() const {
		// This converts my children to strings and then substitutes into rule->format using simple string substitution.
		// the format just searches and replaces %s for each successive child. NOTE: That this therefore does NOT use the
		// full power of printf formatting
		std::string output = rule->format;
		for(size_t i=0;i<rule->N;i++) {
			auto pos = output.find(child_str);
			assert(pos != std::string::npos); // must contain the child_str for all children all children
			output.replace(pos, child_str.length(), (child[i]==nullptr ? nulldisplay : child[i]->string()));
		}
		return output;
	}
	
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
			assert(child[i] != nullptr);
			assert(child[i]->rule->nonterminal_type == rule->child_types[i]); // make sure my kids types are what they should be
		}
		
		// Main code
		if(rule->op == op_IF) {
			assert(rule->N == 3); // must have 3 parts
			
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
		if(rule->id != n.rule->id) return false;
		for(size_t i=0;i<rule->N;i++){
			if(!(child[i]->operator==(*n.child[i]))) return false;
		}
		return true;
	}


	virtual size_t hash() const {
		// Like equality checking, hashing also uses the rule pointers' numerical values, so beware!
		size_t output = rule->id;
		for(size_t i=0;i<rule->N;i++) {
			output = hash_combine(output, (child[i] == nullptr ? i : child[i]->hash()));
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