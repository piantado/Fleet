#pragma once

#include<utility>
#include<string>
#include<optional>
#include<cctype>
#include<stdexcept>

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

	
	// Check that parentheses are balanced and never close before they open. Throws std::invalid_argument
	// on malformed input, rather than asserting: a library should report bad input in a way the caller can
	// catch, and asserts are compiled out under NDEBUG (where a bad string would otherwise parse silently).
	void check_parens(const std::string& s) {
		int nopen = 0;
		for(char c : s) {
			if     (c == '(') nopen++;
			else if(c == ')') nopen--;
			if(nopen < 0) throw std::invalid_argument("*** Unbalanced parentheses (')' before '(') in: " + s);
		}
		if(nopen != 0) throw std::invalid_argument("*** Unbalanced parentheses in: " + s);
	}

	// kept for backward compatibility (older callers used this name)
	void assert_check_parens(const std::string& s) { check_parens(s); }
	
	// Whitespace characters that separate tokens (space, tab, newline, carriage return, form feed, vtab).
	inline bool is_sexpr_space(const char c) { return std::isspace((unsigned char)c) != 0; }

	// Remove leading and trailing whitespace. Returns "" when x is empty or all whitespace.
	// (The previous version indexed x[x.size()-1] on an empty string, which is out of bounds: any
	//  leading/trailing/repeated whitespace -- or a token adjacent to a paren -- could trigger it.)
	std::string trim(const std::string& x) {
		const char* ws = " \t\r\n\f\v";
		const auto a = x.find_first_not_of(ws);
		if(a == std::string::npos) return ""; // empty or all whitespace
		const auto b = x.find_last_not_of(ws);
		return x.substr(a, b - a + 1);
	}


	std::vector<std::string> tokenize(std::string s) {
		assert_check_parens(s);
		std::vector<std::string> out; // list of tokens

		std::string x; // build up the token
		for(char c : s) {
			// A token ends at a parenthesis or ANY whitespace. Treating only ' ' as whitespace (as
			// before) meant tabs and newlines were swallowed into tokens, so s-expressions could not be
			// laid out across multiple lines or with indentation.
			if(c == '(' or c == ')' or is_sexpr_space(c)) {

				x = trim(x);
				if(x != "") {
					out.push_back(x);
				}
				x = "";

				// accumulate the paren itself as its own token (but not whitespace)
				if(c == '(' or c == ')') {
					out.push_back(std::string(1,c));
				}
			}
			else { // just accumulate here
				x += c;
			}
		}

		x = trim(x);
		if(x != "") {
			out.push_back(x);
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

		if(tok.empty()) throw std::invalid_argument("*** Unexpected end of s-expression");

		SExpNode out;
		while(not tok.empty()) {
			auto x = pop_front(tok);
			//print("x=",x);
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

		if(tok.empty()) throw std::invalid_argument("*** Empty s-expression");

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