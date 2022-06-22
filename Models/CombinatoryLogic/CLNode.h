#pragma once 

#include "Combinators.h"
#include "BaseNode.h"
#include "Node.h"

/**
 * @class CLNode
 * @author Steven Piantadosi
 * @date 29/05/22
 * @file Main.cpp
 * @brief An improved implementation of combinators. Here, we storea label on terminals
 * 		  and only terminals!
 */
const std::string APPLY = ".";  // internal label used to signify apply
	
class CLNode : public BaseNode<CLNode> {	
public:

	
	using Super = BaseNode<CLNode>;
	
	std::string label; // Some nodes have labels (S and K) with their arg
	
	CLNode() { }
	CLNode(std::string& l) : BaseNode<CLNode>(), label(l) {
	}
	CLNode(const Node& n) {
		
		// convert a node to a CL Node
		if(n.rule->format == "(%s %s)") {
			label = APPLY;
			
			// now go through and process the children 		
			for(size_t i=0;i<n.nchildren();i++){
				push_back(CLNode{n.child(i)});
			}
		}
		else {
			assert(n.nchildren() == 0);
			label = n.rule->format;
		}
	}
	
	void assign(CLNode& n) {
		label = n.label; // must assign label first?
		this->Super::operator=(n);		
	}
	
	void assign(CLNode&& n) {
		label = n.label;
		this->Super::operator=(n);
	}
	
	virtual std::string string(bool usedot=true) const override {
		
		if(this->nchildren() == 0) {
			return label;
		}
		else { 
		
			std::string out = "(" ;
		
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
		
		bool modified; // did we change anything?
		do { // do this without recursion since its faster and we don't have the depth limit
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
			
			if(NC == 2 and label == APPLY) { // we are an application node
				if(this->child(0).label == "I"){ 				// (I x) = x
					assert(this->child(0).nchildren() == 0);
					auto x = std::move(this->child(1));
					assign(x); 
					modified = true;
//					::print("HERE I", string());
				}
				else if(this->child(0).nchildren() == 2 and
						this->child(0).child(0).label == "K") {	// ((K x) y) = x
					assert(this->child(0).child(0).nchildren() == 0);
					auto x = std::move(this->child(0).child(1));
					assign(x); 
//					::print("HERE K", string());
					modified = true;
				} 
				else if(this->child(0).nchildren() == 2 and
						this->child(0).child(0).nchildren() == 2 and
						this->child(0).child(0).child(0).label == "S") { // (((S x) y) z) = ((x z) (y z))
					assert(this->child(0).child(0).child(0).nchildren() == 0);
					
					// just make the notation handier here
					auto x = std::move(this->child(0).child(0).child(1));
					auto y = std::move(this->child(0).child(1));
					auto z = std::move(this->child(1));
					CLNode zz = z; // copy 
					
					CLNode q; // build our rule
					q.set_child(0, std::move(x)); 
					q.set_child(1, std::move(zz)); //copy
					
					CLNode r;
					r.set_child(0, std::move(y));
					r.set_child(1, std::move(z));
					
					this->label = APPLY; // remove our label 
					this->set_child(0,std::move(q));
					this->set_child(1,std::move(r));
					modified = true;
//					::print("HERE S", string());
				}
			}
			else if(label == APPLY and this->nchildren() == 1) {
				auto x = std::move(this->child(0));
				assign(x); // ((x)) = x
				modified = true; 
//				::print("HERE APP", string());
			}
			
//			::print("GOT", this, string());
			
			
		} while(modified); // end while
	}
};
