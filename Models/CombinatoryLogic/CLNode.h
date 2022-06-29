#pragma once 

#include "SExpression.h"
#include "Combinators.h"
#include "BaseNode.h"
#include "Node.h"

/**
 * @class CLNode
 * @author Steven Piantadosi
 * @date 29/05/22
 * @file Main.cpp
 * @brief An improved implementation of combinators. This kind of node is necessary because it stores arbitrary
 * 		  objects as labels, without having to interface with the grammar. 
 */
const std::string APPLY = ".";  // internal label used to signify apply
	
class CLNode : public BaseNode<CLNode> {	
public:
	
	using Super = BaseNode<CLNode>;
	
	std::string label; // Some nodes have labels (S and K) with their arg
	
	CLNode() : label(APPLY) { }
	CLNode(const SExpression::SENode& n) {
		
		// here we need to see if the first child is a label, we use that 
		// for our label 
		if(n.nchildren() >= 1 and n.child(0).label.has_value()) {
			label = n.child(0).label.value();
			// copy AFTER the first since the first was my symbol
			for(size_t i=1;i<n.nchildren();i++){
				set_child(i-1, CLNode{n.child(i)});
			} 
		}
		else if(nchildren() == 0 and n.label.has_value()) {
			//assert(n.label.has_value());
			
			label = n.label.value();
		}
		else {
			// otherwise we're an apply node
			assert(n.nchildren() <= 2);
			
			label = APPLY;
			
			// now go through and process the children 		
			for(size_t i=0;i<n.nchildren();i++){
				set_child(i, CLNode{n.child(i)});
			}
		}
	}
	CLNode(const Node& n) {
		
		// how to convert a Node (returned by S-expression parsing) into a CLNode
		
		// TODO: Add some fanciness to make this *binary* trees please
		//::print("::CL=", n.string());
		assert(n.nchildren() <= 2);
		
		if(n.rule->format != "(%s %s)") { // if its a terminal 
			assert(n.nchildren() == 0); 
			label = n.rule->format;
		}
		else {
			
			label = APPLY;
			
			// now go through and process the children 		
			for(size_t i=0;i<n.nchildren();i++){
				set_child(i, CLNode{n.child(i)});
			}
		}
	}
	
	Node toNode() {		
		if(label == APPLY) {
			Rule* r = Combinators::skgrammar.get_rule(Combinators::skgrammar.nt<CL>(), "(%s %s)");
			assert(r != nullptr);
			Node out(r);
			for(size_t i=0;i<nchildren();i++){
				out.set_child(i, child(i).toNode());
			}
			return out; 
		}
		else {
			assert(nchildren() == 0);
			Rule* r = Combinators::skgrammar.get_rule(Combinators::skgrammar.nt<CL>(), label);
			assert(r != nullptr);
			return Node(r);			
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
		
			std::string out = "("+(label == APPLY ? "" : label+" ");
		
			for(const auto& c : this->children) {
				out += c.string() + " ";
			}	
			
			if(nchildren() > 0)
				out.erase(out.length()-1);
			
			out += ")";
			return out; 
		}
	}	
	
	/**
	 * @brief Returns true if we only use CL terms
	 * @return 
	 */	
	bool is_only_CL() {
		for(auto& n : get_children()) {
			if(not n.is_only_CL()) {
				return false;
			}
			else if(n.label != APPLY and n.label != "S" and n.label != "K" and n.label != "I") {
				return false;
			}
		}
		return true;
	}
	
	template<typename L>
	void substitute(const L& m) {
		//::print("LABEL=",label, m.factors.contains(label));
		
		if(m.factors.contains(label)) {
			auto v = m.at(label).get_value(); // copy
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
		
//			std::string original = string();
//			::print("REDUCE", this, label, string());
			
			if(remaining_calls-- == 0)
				throw Combinators::reduction_exception;

			// try to evaluate n according to the rules
			const auto NC = this->nchildren();
			assert(NC <= 2); // we don't handle this -- 3+ children should have been caught in the constructor 
			
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
			
//			::print("GOT", this, label, original, string());
			
			
		} while(modified); // end while
	}
	
	virtual bool operator==(const CLNode& n) const override {
		return label == n.label and children == n.children;
	}
};
