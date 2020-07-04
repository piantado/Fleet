#pragma once

#include <string>
#include <vector>
#include <assert.h>
#include <set>
#include <map>
#include <functional>

#include "BaseNode.h"


/**
 * @class BindingTree
 * @author piantado
 * @date 27/03/20
 * @file BindingTree.h
 * @brief A tree stores sentences that we use here. You provide S-expressions like 
 * 				"(S (NP he.1) (VP (V saw)) (NP *himself.1))"
 * 		  Here each string and part of speech (S, NP, he, saw, etc.) is stored as a "label"
 */

class BindingTree : public BaseNode<BindingTree> {
public:
	std::string label;
	int referent; // parsed out of the string
	bool starred; 
	
	BindingTree(std::string s="") : 
		BaseNode<BindingTree>(),
		referent(-1), starred(false) {
		
		// set up the referent if we can
		auto pos = s.find(".");
		if(pos != std::string::npos) {
			referent = stoi(s.substr(pos+1));
			label = s.substr(0,pos);
		}
		else {
			label = s;
		}
		
		// and then fix label if it has a star
		if(label.length() > 0 and label.at(0) == '*')  {
			starred = true;
			label = label.substr(1,std::string::npos);
		}
		
	}
	
	BindingTree(const BindingTree& t) : BaseNode<BindingTree>(t) {
		label = t.label;
		starred = t.starred;
		referent = t.referent;
	}
	BindingTree(const BindingTree&& t) : BaseNode<BindingTree>(t) {
		label = t.label;
		starred = t.starred;
		referent = t.referent;
	}	
	void operator=(const BindingTree& t) {
		BaseNode<BindingTree>::operator=(t);
		label = t.label;
		starred = t.starred;
		referent = t.referent;
	}	
	void operator=(const BindingTree&& t) {
		BaseNode<BindingTree>::operator=(t);
		label = t.label;
		starred = t.starred;
		referent = t.referent;
	}	

	BindingTree* get_starred() {
		std::function f = [&](BindingTree& t) { return t.starred; };
		return get_via(f);
	}
	
	std::string string() const override {
		std::string out = "[" + label;
		for(auto& c : children) {
			out += " " + c.string();
		}
		out += "]";
		return out;
	}
	
	std::string my_string() const override {
		// string for just me but not my kids
		return (starred ? "* " : "") + label + " " + std::to_string(referent);
	}
	
};




