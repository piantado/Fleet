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
		std::vector<std::string> out; // list of tokens
		std::string x; // build up the token 
		for(char c : s) {
			if(c == '(' or c == ')' or c == ' ') {
				
				// remove leading spaces
				while(x[0] == ' ') {
					x.erase(0, 1);
				}
				
				// remove trailing spaces
				while(x[x.size()-1] == ' ') {
					x.erase(x.size()-1, 1);
				}
				
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
		//PRINTN(str(out));
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
		// Requires that T can be constructable with a string argument
		assert(tok.size() > 1);
		
		std::string lab = "";
		if(tok.front() != "(") { 
			lab = pop_front(tok);				
		}
		
		T out(lab);
		
		while(not tok.empty()){
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