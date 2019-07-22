#pragma once

#include <vector> 

/* Many things in Fleet are stacks and this is designed to allow for
 * rapid changse to the stack type in order to improve speed. std::stack appears to be
 * slow. Using vector with some wrappers is faster
 * */

template<typename T>
class Stack : public std::vector<T> {
	
//	static const size_t INITIAL_SIZE = 8; 
	
public:
//  Neither of these seem to speed stuff up
//	Stack() : std::vector<T>() { 
//		this->reserve(INITIAL_SIZE);		
//	}
//	Stack() : std::vector<T>(INITIAL_SIZE) { 
//	}

	void push(const T& val) {
		this->push_back(val);
	}
	
	void pop() {
		this->pop_back();
	}

	T top() {
		return this->back();
	}

	T& topref() {
		return this->back();
	}
};
