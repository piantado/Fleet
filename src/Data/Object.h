
#pragma once 

#include<iostream>
#include<tuple>

#include "Random.h"
#include "Strings.h"

/**
 * @class Object
 * @author piantado
 * @date 02/08/20
 * @file Main.cpp
 * @brief This stores an object with feature FARGS which is meant to be something like enums.
 * 			 These are often used to store features that LOT expressions operate on, for instance
 * 			 the objects that boolean logic operates on. 
 */
template<typename... feature_t> // template of the features
struct Object { 
	std::tuple<feature_t...> feature;
	
	// Well.. this is the craziest hack. Down below we need to define operator<<
	// and that complains when given feature_t={} because it redefines this constructor
	// however, there is no problem if we define this with a *default* parameter
	// (which allows it ot be called as Object(), but doesn't seem to clash with the 
	// empty list anymore, which makes no sense)
	Object(int __nothing=0) { 	}
	
	Object(feature_t... types) : feature(types...) { }
	
	/**
	 * @brief Get one of the feature values
	 * @return 
	 */	
	template<typename t> 
	t get() const {
		return std::get<t>(feature);
	}

	/**
	 * @brief Set a feature value
	 * @param val
	 */
	template<typename t>
	void set(t val) {
		std::get<t>(feature) = val;
	}

	/**
	 * @brief Check if this feature dimension is a given value
	 * @param v
	 * @return 
	 */
	template<typename t>
	bool is(t v) const {
		return get<t>() == v;
	}
	
	auto operator<=>(const Object& o) const { 
		return feature <=> o.feature;
	}
	
	bool operator==(const Object& o) const { 
		return feature == o.feature;
	}
	
	/**
	 * @brief Sample a uniformly random object. This requires our features to end in __count
	 */	
	template<typename... T>
	static Object<T...> __sample() {
		Object<T...> ret;
		
		// this parameter pack just sets everything assuming __count is 
		// defined in our enum class. C++ is such a weird language. 
		(ret.set(  static_cast<T>(myrandom(static_cast<int>(T::__count)))  ), 
		 ...);
		
		return ret;		
	}
	
	static Object<feature_t...> sample() {
		return __sample<feature_t...>();
	}
	
	std::string string() const {
		std::string out = "[Object";
		(out += str((int)get<feature_t>()), ...);
		out += "]";
		return out;
	}
	
};

template<typename... feature_t> 
std::ostream& operator<<(std::ostream& o, const Object<feature_t...>& t) {
	o << t.string();
	return o;
}