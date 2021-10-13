#pragma once

// Code for parsing and dealing with S-expressions
namespace SExpression {
		
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
	
	
	std::vector<std::string> tokenize(std::string s) {
		assert_check_parens(s);
		std::vector<std::string> out;
		std::string x;
		for(char c : s) {
			if(c == '(' or c == ')') {
				
				// remove trailing spaces
				while(x[x.size()-1] == ' ') {
					x.erase(x.size()-1, 1);
				}
				
				if(x != "" and x != " ") {
					out.push_back(x);
				}
				x = "";
				out.push_back(std::string(1,c));
			}
			else { // just accumulate here
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
		// NOTE: We do NOT allow multiple nesting at the start ((NP bb) ..) because
		// we assume that the first token is the label
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