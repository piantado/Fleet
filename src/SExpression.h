#pragma once

#include<utility>
#include<string>
#include<optional>

#include "BaseNode.h"

// Code for parsing and dealing with S-expressions
namespace SExpression {
	
	/**
	 * @class SExpNode
	 * @author Steven Piantadosi
	 * @date 26/06/22
	 * @file SExpression.h
	 * @brief This is the return type of parsing S-expressions. It contains an optional label and
	 * 		  typically we be converted into a different type. 
	 */
	struct SExpNode : public BaseNode<SExpNode> {
		std::optional<std::string> label;
		
		SExpNode() : label() { }
		SExpNode(const std::string& s) : label(s) {}
		SExpNode(const SExpNode& s) : BaseNode<SExpNode>(s), label(s.label) {}
		
		SExpNode& operator=(const SExpNode& n) {
			label = n.label;
			children = n.children;
			return *this;
		}
		SExpNode& operator=(const SExpNode&& n) {
			label = std::move(n.label);
			children = std::move(n.children);
			return *this;
		}
		
		virtual bool operator==(const SExpNode& n) const override {
			return label == n.label and children == n.children;
		}
		
		std::string get_label() {
			if(label.has_value()) {
				return label.value();
			}
			else {
				return "<NA>";
			}
		}
		
		virtual std::string string() const {
			if(label) {
				assert(nchildren() == 0);
				return label.value();
			}
			else { 
			
				std::string out = "["; // we use square braces so we can see how it's different than paren inputs
			
				for(const auto& c : this->children) {
					out += c.string() + " ";
				}	
				
				if(nchildren() > 0)
					out.erase(out.length()-1);
				
				out += "]";
				return out; 
			}
	}	
	};

	
	void assert_check_parens(const std::string& s) {
		int nopen = 0;
		for(char c : s) {
			if     (c == '(') nopen++;
			else if(c == ')') nopen--;
			assert(nopen >= 0);
		}
		if(nopen != 0) {
			CERR "*** Bad parentheses in " << s ENDL;
			assert(false);
		}
	}
	
	std::string trim(std::string x) {
		// remove leading spaces
		while(x[0] == ' ') {
			x.erase(0, 1);
		}
		
		// remove trailing spaces
		while(x[x.size()-1] == ' ') {
			x.erase(x.size()-1, 1);
		}
		return x; 
	}
	
	
	std::vector<std::string> tokenize(std::string s) {
		assert_check_parens(s);
		std::vector<std::string> out; // list of tokens
		
		std::string x; // build up the token 
		for(char c : s) {
			if(c == '(' or c == ')' or c == ' ') {
				
				x = trim(x);
				
				if(x != "" and x != " ") {
					out.push_back(x);
				}
				x = "";
				
				// and accumulate the paren if its parens
				if(c != ' ') {
					out.push_back(std::string(1,c));
				}
			}
			else { // just accumulate here
				x += c;
			}
		}
		
		if(x != "") {
			out.push_back(trim(x));
		}
		
		//PRINTN(str(out));
		return out;	
	}	
	
	std::string pop_front(std::vector<std::string>& q) {
		// not great for vector, but this is just data pre-processing -- could switch to std::list
		auto x = q.at(0);
		q.erase(q.begin());
		return x; 
	}
	

	/**
	 * @brief Recursive parsing of S-expressions. Not high qualtiy. 
	 * 		Basically, if we get ((A...) B...) then we call T's constructor with no arguments and make (A...) the first
	 * 		child. If we get (A ....) then we call with "A" as the first argument to the constructor because in some
	 *      cases (like Binding) we want to handle this as a "label" on the node; in others like CL we want it to be an Node itself
	 * @param tok
	 * @return 
	 */
	SExpNode __parse(std::vector<std::string>& tok) {
		
		assert(tok.size() > 0);
		
		SExpNode out;		
		while(not tok.empty()) {
			auto x = pop_front(tok);
			//::print("x=",x);
			if(x == "(")      out.push_back(__parse(tok)); 
			else if(x == ")") break; // when we get this, we must be a close (since lower-down stuff has been handled in the recursion)
			else              out.push_back(SExpNode(x)); // just a string
		}
		
		return out;	
	}
	
	
	/**
	 * @brief Wrapper to parse to remove outer (...)
	 * @param tok
	 * @return 
	 */	
	SExpNode parse(std::vector<std::string>& tok) {
		
		if(tok.size() == 1) {
			return SExpNode(pop_front(tok));
		}
		
		if(tok.front() == "(") { 
			pop_front(tok);
		}
		
		return __parse(tok); 
	}	

	SExpNode parse(std::string s) {
		auto tok = tokenize(s);
		return parse(tok);
	}

	
	
}