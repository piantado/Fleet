#pragma once 

#include<tuple>

/**
 * @class Object
 * @author piantado
 * @date 02/08/20
 * @file Main.cpp
 * @brief This stores an ojbect with feature FARGS which is meant to be something like enums.
 * 			 These are often used to store features that LOT expressions operate on, for instance
 * 			 the objects that boolean logic operates on. 
 */
template<typename... feature_t> // template of the features
struct Object { 
	std::tuple<feature_t...> feature;
	
	Object() { 	}
	
	Object(feature_t... types) : feature(types...) {		
	}
	
	/**
	 * @brief Get one of the feature values
	 * @return 
	 */	
	template<typename t> 
	t get() {
		return std::get<t>(feature);
	}

	/**
	 * @brief Check if this feature dimension is a given value
	 * @param v
	 * @return 
	 */
	template<typename t>
	bool is(t v) {
		return get<t>() == v;
	}
	
	bool operator<(const Object& o) const { 
		return feature < o.feature;
	}
	
	bool operator==(const Object& o) const { 
		return feature == o.feature;
	}
};