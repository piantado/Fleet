#pragma once 

#include "Combinators.h"
#include "BaseNode.h"
#include "Node.h"

/**
 * @class CLNode
 * @author Steven Piantadosi
 * @date 29/05/22
 * @file Main.cpp
 * @brief An improved implementation of combinators. Here, we store S,K,I as a "label" 
 * 		  on a node, and if it has a label then the arguments are childen (e.g. (S x y)  has 
 *        two children x,y, with a label=S)
 */

class CLNode : public BaseNode<CLNode> {	
public:

	using Super = BaseNode<CLNode>;
	
	std::string label; // Some nodes have labels (S and K) with their arg
	
	CLNode() { }
	CLNode(std::string& l) : BaseNode<CLNode>(), label(l) {
	}
	CLNode(const Node& n) {
		
		size_t start = 0; // where do we start from? 
		
		// convert a node to a CL Node
		if(n.rule->format != "(%s %s)") {
			label = n.rule->format;
			assert(n.nchildren() == 0);
		}
		else if (n.nchildren() > 0 and n.child(0).nchildren() == 0) { // matches something like (S x...) and promotes S to the label
			label = n.child(0).rule->format;
			start = 1; // we don't copy the first kid
		}
		
		// now go through and process the children 		
		for(size_t i=start;i<n.nchildren();i++){
			push_back(CLNode{n.child(i)});
		}
		
		
	}
	
	void assign(CLNode& n) {
		label = n.label; // must assign label first
		this->Super::operator=(n);		
	}
	
	void assign(CLNode&& n) {
		label = n.label;
		this->Super::operator=(n);
	}
	
	virtual std::string string(bool usedot=true) const override {
		
		if(this->nchildren() == 0) {
			return "["+label+"]";
		}
		else { 
		
			std::string out = "(" " ["+label+"]";
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
		
		bool modified = true; // did we change anything?
		while(modified) { // do this without recursion since its faster and we don't have the depth limit
			modified = false;
		
			// first ensure that my children have all been reduced
			for(size_t c=0;c<this->nchildren();c++){
				this->child(c).reduce(remaining_calls);
			}
		
//			::print("REDUCE", this, string());
			
			if(remaining_calls-- == 0)
				throw Combinators::reduction_exception;

			// try to evaluate n according to the rules
			const auto NC = this->nchildren();
			
			if(NC == 1 and label == "I"){ 				// (I x) = x
				assign(std::move(this->child(0))); // 0 b/c I is a label
				modified = true;
//				::print("HERE I", string());
			}
			else if(NC == 2 and // remember the root note in ((K x) y) has two children, but (K x) has one
			        this->child(0).nchildren() == 1 and
			        this->child(0).label == "K") {	// ((K x) y) = x
					assert(label == ""); // no label for toplevel node, which is apply 
				
				assign(std::move(this->child(0).child(0))); // 0 b/c K is a label
//				::print("HERE K", string());
				modified = true;
			} 
			else if(NC == 2 and 
				    this->child(0).nchildren() == 2 and
				    this->child(0).child(0).nchildren() == 1 and
				    this->child(0).child(0).label == "S") { // (((S x) y) z) = ((x z) (y z))
				assert(label == "" and this->child(0).label=="");
				
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
				
				this->label = ""; // remove our label 
				this->set_child(0,std::move(q));
				this->set_child(1,std::move(r));
				modified = true;
//				::print("HERE S", string());
			}
			else if(label == "" and this->nchildren() == 1) {
				assign(this->child(0)); // ((x)) = x
				modified = true; 
				//::print("HERE APP", string());
			}
//			
//			::print("GOT", this, string());
			
			
		} // end while
	}
};
