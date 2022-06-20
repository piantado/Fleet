#pragma once 

#include "Combinators.h"
#include "BaseNode.h"
#include "Node.h"

/**
 * @class CLNode
 * @author Steven Piantadosi
 * @date 29/05/22
 * @file Main.cpp
 * @brief A better implementation of combinators that just uses strings since we don't need
 *        all that other mess. 
 */

class CLNode : public BaseNode<CLNode> {	
public:

	using Super = BaseNode<CLNode>;
	
	std::string label; // these really just need labels
	
	CLNode() { }
	CLNode(std::string& l) : BaseNode<CLNode>(), label(l) {
	}
	CLNode(const Node& n) {
		// convert a node to a CL Node
//		if(s)
		if(n.rule->format != "(%s %s)") {
			label = n.rule->format;
		}
		
		for(size_t i=0;i<n.nchildren();i++){
			push_back(CLNode{n.child(i)});
		}
		
		
	}
	
	void assign(CLNode& n) {
		this->Super::operator=(n);
		label = n.label;
	}
	
	virtual std::string string(bool usedot=true) const override {
		
		if(this->nchildren() == 1) {
			return label;
		}
		else { 
		
			std::string out = "("+label;
			if(label != "") { out += " "; }
		
			for(const auto& c : this->children) {
				out += c.string() + " ";
			}	
			
			out.erase(out.length()-1);
			
			out += ")";
			return out; 
		}
	}	
	
	void substitute(const std::map<std::string, CLNode>& m) {
		if(m.contains(label)) {
			auto v = m.at(label); // copy
			this->assign(v);
		}
		
		for(auto& c : children) {
			c.substitute(m);
		}
	}
	
	void reduce() {
		size_t remaining_calls = 256;
		reduce(remaining_calls);
	}
	
	
	void reduce(size_t& remaining_calls) {
		// returns true if we change anything
		//PRINTN("REDUCE", string());
		

		
		bool modified = true; // did we change anything?
		while(modified) { // do this without recursion since its faster and we don't have the depth limit
			modified = false;






			// HMMMMM WHEN DO WE REDUCE CHILDREN?
				





			// first ensure that my children have all been reduced
			for(size_t c=0;c<this->nchildren();c++){
				this->child(c).reduce(remaining_calls);
			}
		
			if(remaining_calls-- == 0)
				throw Combinators::reduction_exception;

			///
///((K) ((S) (((K) ((K) ((S) (S)))) (I)))) (S)
			
			// try to evaluate n according to the rules
			if(this->nchildren() > 0 and 
			   this->child(0).label == "I"){ 				// (I x) = x
				auto x = std::move(this->child(1));
				assign(x);
				modified = true;
			}
			else if(this->nchildren() == 2 and // remember the root note in ((K x) y) has two children, but (K x) has one
			        this->child(0).nchildren() == 1 and
			        this->child(0).child(0).label == "K") {	// ((K x) y) = x
				auto x = std::move(this->child(0).child(0));
				assign(x);
				modified = true;
			} 
			else if(this->nchildren() == 2 and 
				    this->child(0).nchildren() == 2 and
				    this->child(0).child(0).nchildren() == 1 and
				    this->child(0).child(0).child(0).label == "S") { // (((S x) y) z) = ((x z) (y z))
//				Rule* rule = skgrammar.get_rule(skgrammar.nt<CL>(), Op::CL_Apply);
				
				// just make the notation handier here
				auto x = std::move(this->child(0).child(0).child(0));
				auto y = std::move(this->child(0).child(1));
				auto z = std::move(this->child(1));
				CLNode zz = z; // copy 
				
				CLNode q; // build our rule
				q.set_child(0, std::move(x)); 
				q.set_child(1, std::move(zz)); //copy
				
				CLNode r;
				r.set_child(0, std::move(y));
				r.set_child(1, std::move(z));
				
				this->set_child(0,std::move(q));
				this->set_child(1,std::move(r));
				modified = true;
			}
			else if(label == "" and this->nchildren() == 1) {
				assign(this->child(0)); // ((x)) = x
				modified = true; 
			}
			
		} // end while
	}
};
