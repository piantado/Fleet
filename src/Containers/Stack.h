#pragma once

#include <vector> 

 
 /**
  * @class Stack
  * @author Steven Piantadosi
  * @date 06/09/20
  * @file Stack.h
  * @brief  Many things in Fleet are stacks and this is designed to allow for
  *         rapid changse to the stack type in order to improve speed. std::stack appears to be
  *         slow. Using vector with some wrappers is faster. 
  * 
  *         NOTE: We have tried reserving the intiial vector size and it doesn't seem to speed things up,
  *         nor does using a list
  */ 
template<typename T>
class Stack  {
		
	std::vector<T> value;
	
public:

	Stack() {
	}

	template<typename... Args>
	void emplace_back(Args... args) {
		value.emplace_back(args...);
	}
	
	void reserve(size_t t) {
		value.reserve(t);
	}

	/**
	* @brief Push val onto the stack
	* @param val
	*/
	void push(const T& val) {
		value.push_back(val);
	}
	void push(const T&& val) {
		value.push_back(std::move(val));
	}
	
	void pop() {
		/**
		 * @brief Remove the top element (returning void)
		 */
		
		value.pop_back();
	}
	
	/* There is a little sublety here -- for integral types, it's a pain to return a reference
	 * so we want to just return T. But some optimizations of the virtual machine will do better
	 * with references, leaving the top value on the stack. So by default we don't return a reference
	 * with top() but we do with topref
	 */
	
	[[nodiscard]] T top() {
		/**
		 * @brief Return the top element.
		 * @return 
		 */
		
		return value.back();
	}

	[[nodiscard]] T& topref() {
		/**
		 * @brief Get a reference to the top element (allowing in-place modification)
		 * @return 
		 */
		
		return std::forward<T&>(value.back());
	}
	
	size_t size() const { 
		return value.size();
	}
	
	bool empty() const {
		return value.empty();
	}
};
