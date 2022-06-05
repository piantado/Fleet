#pragma once

#include <string>
#include <vector>
#include <assert.h>
#include <set>
#include <map>
#include <functional>

#include "BaseNode.h"

// Some parts of speech here as a class 
enum class POS { None, S, SBAR, NP, NPS, NPO, NPPOSS, VP, MD, PP, CP, CC, N, V, P, A};

std::map<std::string,POS> posmap = { // just for decoding strings to tags -- ugh
								    {"S",  POS::S}, 
									{"NP", POS::NP}, 
									{"NP-S", POS::NPS}, 
									{"NP-O", POS::NPO},
									{"VP", POS::VP},
									{"PP", POS::PP},
									{"CP", POS::CP},
									{"CC", POS::CC}, 
									{"SBAR", POS::SBAR},
									{"MD", POS::MD},
									{"N", POS::N},
									{"V", POS::V},
									{"P", POS::P},
									{"A", POS::A},
									{"NP-POSS", POS::NPPOSS},
								   };
	

							   
/**
 * @class BindingTree
 * @author piantado
 * @date 27/03/20
 * @file BindingTree.h
 * @brief A tree stores sentences that we use here. You provide S-expressions like 
 * 				"(S (NP he.1) (VP (V saw)) (NP >himself.1))"
 * 		  Here each string and part of speech (S, NP, he, saw, etc.) is stored as a "label"
 */

class BindingTree : public BaseNode<BindingTree> {
public:
	int referent; // parsed out of the string
	bool target; // and I the target? 
	int linear_order; // order of leaves
	POS pos;
	std::string label;
	std::string word; 
	
	/**
	 * @brief This constructor gets called by SExpression, and we need to call convert_from_SExpression() to 
	 * 		  really fill in referent, target, word, etc. 
	 * @param s
	 */
	BindingTree(std::string s="") :
		referent(-1), target(false), linear_order(0), pos(POS::None), word(""){
		
		// set up the referent if we can
		// NOTE: There referent is on the POS
		auto p = s.find(".");
		if(p != std::string::npos) {
			referent = stoi(s.substr(p+1)); // only single digits!!
			label = s.substr(0,p);
		}
		else {
			label = s;
		}
		
		// and then fix label if it has a star
		if(label.length() > 0 and label.at(0) == '>')  {
			target = true;
			label = label.substr(1,std::string::npos);
		}	
	
		// and set the POS accordingly
		if(posmap.count(label) != 0){
			pos = posmap[label];
		}		
	}
	
	/**
	 * @brief This is needed because when we make these with an S-expression, it doesn't correctly set the referent, POS, etc. 
	 */	
	void convert_from_SExpression() {
		
		// this is the main thing S-expression parsing does wrong -- it makes the 
		// "word" into the first child
		if(this->nchildren() > 0 and this->child(0).nchildren() == 0) {
			std::string s = this->child(0).label;
			word = s; 
			
			// erase the first child since it's going to be the word
			children.erase(children.begin());
		}	

		for(auto& c : children) {
			c.convert_from_SExpression();
		}
	}

	/////////////////////////////////
	// I need these because I need to call basenode, which overrides 
	// so that children are correctly set
	/////////////////////////////////
	
	BindingTree(const BindingTree& t) : BaseNode<BindingTree>(t) {
		label = t.label;
		target = t.target;
		referent = t.referent;
		word = t.word;
		linear_order = t.linear_order;
		pos = t.pos;
	}
	BindingTree(const BindingTree&& t) : BaseNode<BindingTree>(t) {
		label = t.label;
		target = t.target;
		referent = t.referent;
		word = t.word;
		linear_order = t.linear_order;
		pos = t.pos;
	}	
	void operator=(const BindingTree& t) {
		BaseNode<BindingTree>::operator=(t);
		label = t.label;
		target = t.target;
		referent = t.referent;
		word = t.word;
		linear_order = t.linear_order;
		pos = t.pos;
	}	
	void operator=(const BindingTree&& t) {
		BaseNode<BindingTree>::operator=(t);
		label = t.label;
		target = t.target;
		referent = t.referent;
		word = t.word;
		linear_order = t.linear_order;
		pos = t.pos;
	}	

	BindingTree* get_target() {
		std::function f = [&](BindingTree& t) { return t.target; };
		return get_via(f);
	}
	
	BindingTree* coreferent() {
		BindingTree* x = this; // local ref for f
		
		std::function f = [&](BindingTree& t) -> bool {
			return (t.referent != -1) and 
				   (t.referent == x->referent) and 
				   (&t != x);
		};
		
		return root()->get_via(f);
	}
	
	std::string string(bool usedot=true) const override {
		std::string out = "[" + (target ? std::string(">") : "") + 
		                        label + 
								(referent > -1 ? "."+str(referent) : "") + 
								" " + 
								word;
		for(auto& c : children) {
			out += " " + c.string();
		}
		out += "]";
		return out;
	}
	
	std::string my_string() const override {
		// string for just me but not my kids
		return (target ? ">" : "") + label + " " + std::to_string(referent);
	}
	
};


std::string str(BindingTree* t){
	if(t == nullptr) return "nullptr";
	else return t->string();
}