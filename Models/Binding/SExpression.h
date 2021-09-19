#pragma once

// Code for parsing and dealing with S-expressions
namespace SExpression {
		
	std::vector<std::string> tokenize(std::string s) {
		std::vector<std::string> out;
		std::string x;
		for(char c : s) {
			if(c == '(' or c == ')') {
				if(x != "") out.push_back(x);
				x = "";
				out.push_back(std::string(1,c));
			}
			else if(c == ' ' or c == '\t' or c == '\n') {
				if(x != "") out.push_back(x);
				x = "";
			}
			else {
				x += c;
			}
		}
		return out;	
	}
	
	
	std::string pop_front(std::vector<std::string>& q) {
		// not great for vector, but this is just data pre-processing -- could switch to std::list
		auto x = q.at(0);
		q.erase(q.begin());
		return x; 
	}
	
	template<typename T>
	T parse(std::vector<std::string>& tok) {
		// recursive parsing of tokenized s-expression. Not high quality.
		// Requires that T can be constructed with a string argument
		
		assert(tok.size() > 1);
		
		// optionally allow us to start with "(" (makes the recursion simpler)
		auto o = pop_front(tok);
		if(o == "(") {
			o = pop_front(tok);
			assert(o != "(");
		}
		T out(o);

		while(true){
			auto x = pop_front(tok);
			if(x == "(")      out.push_back(parse<T>(tok)); 
			else if(x == ")") break; // when we get this, we must be a close (since lower-down stuff has been handled in the recursion)
			else              out.push_back(T(x)); // just a string
		}
		
		return out;	
	}

	template<typename T>
	T parse(std::string s) {
		auto tok = tokenize(s);
		return parse<T>(tok);
	}

	
	
}