#pragma once 

#include <sstream>
#include <queue>
#include <array>
#include <map>
#include <set>
#include <atomic>
#include <string>


/**
 * @class has_string - SFINAE for whether a type has a ".string" function. What an ugly nightmare. 
 * @author Steven Piantadosi
 * @date 12/11/22
 * @file str.h
 * @brief 
 */
template <typename T>
class has_string
{
    typedef char one;
    struct two { char x[2]; };

    template <typename C> static one test( decltype(&C::string) ) ;
    template <typename C> static two test(...);    

public:
    enum { value = sizeof(test<T>(0)) == sizeof(char) };
};
    

/**
 * @brief Split is returns a deque of s split up at the character delimiter. 
 * It handles these special cases:
 * split("a:", ':') -> ["a", ""]
 * split(":", ':')  -> [""]
 * split(":a", ':') -> ["", "a"]
 * @param s
 * @param delimiter
 * @return 
 */
std::deque<std::string> split(const std::string& s, const char delimiter) {
	std::deque<std::string> tokens;
	
	if(s.length() == 0) {
		return tokens;
	}
	
	size_t i = 0;
	while(i < s.size()) {
		size_t k = s.find(delimiter, i);
		
		if(k == std::string::npos) {
			tokens.push_back(s.substr(i,std::string::npos));
			return tokens;
		}
		else {		
			tokens.push_back(s.substr(i,k-i));
			i=k+1;
		}
	}	

	// if we get here, that means that the last found k was the last
	// character, which means we need to append ""
	tokens.push_back("");
	return tokens;
}

/**
 * @brief Split iwith a fixed return size, useful in parsing csv
 * @param s
 * @param delimiter
 * @return 
 */
template<size_t N>
std::array<std::string, N> split(const std::string& s, const char delimiter) {
	std::array<std::string, N> out;
	
	auto q = split(s, delimiter);
	
	// must be the right size 
	if(q.size() != N) {
		std::cerr << "*** String in split<N> has " << q.size() << " not " << N << " elements: " << s << std::endl;
		assert(false);
	}
	
	// convert to an array now that we know it's the right size
	size_t i = 0;
	for(auto& x : q) {
		out[i++] = x;
	}

	return out;
}

/**c
 * @brief A pythonesque string function. This gets specialized in many ways. 
 * @param x
 * @return 
 */	 
template<typename T>
std::string str(T x){

	// special handling of a few key types:
	if constexpr (std::is_pointer<T>::value) {
		std::ostringstream address;
		address << (void const *)x;
		return address.str();
	}
	else if constexpr(std::is_same<T,char>::value) {
		return S(1,x);
	}
	else if constexpr(has_string<T>::value) {
		return x.string();
	}
	else {
		return std::to_string(x);
	}
}

std::string str(const std::string& x){
	// Need to specialize this, otherwise std::to_string fails
	return x;
}

template<typename... T, size_t... I >
std::string str(const std::tuple<T...>& x, std::index_sequence<I...> idx){
	/**
	 * @brief A pythonesque string function
	 * @param x
	 * @return 
	 */
	return "<" + ( (std::to_string(std::get<I>(x)) + ",") + ...) + ">";
}
template<typename... T>
std::string str(const std::tuple<T...>& x ){
	/**
	 * @brief A pythonesque string function
	 * @param x
	 * @return 
	 */
	return str(x, std::make_index_sequence<sizeof...(T)>() );
}

template<typename A, typename B >
std::string str(const std::pair<A,B> x){
	/**
	 * @brief A pythonesque string function
	 * @param x
	 * @return 
	 */
	return "<" + str(x.first) + "," + str(x.second) + ">";
}

template<typename T, size_t N>
std::string str(const std::array<T, N>& a ){
	/**
	 * @brief A pythonesque string function
	 * @param x
	 * @return 
	 */
	std::string out = "<";
	for(auto& x : a) {
		out += str(x) + ",";
	}
	
	if(a.size()>0) 
		out.erase(out.size()-1); // remove that last dumb comma
	
	return out+">";
}


template<typename T>
std::string str(const std::vector<T>& a, const std::string _sep=","){
	/**
	 * @brief A pythonesque string function
	 * @param x
	 * @return 
	 */
	std::string out = "[";
	for(const auto& x : a) {
		out += str(x) + _sep;
	}
	if(a.size()>0) 
		out.erase(out.size()-1); // remove that last dumb comma
	
	return out+"]";
}

template<typename T, typename U>
std::string str(const std::map<T,U>& a ){
	/**
	 * @brief A pythonesque string function
	 * @param x
	 * @return 
	 */
	std::string out = "{";
	for(auto& x : a) {
		out += str(x.first) + ":" + str(x.second) + ",";
	}
	if(a.size()>0) 
		out.erase(out.size()-1); // remove that last dumb comma
	out += "}";
	
	return out;
}

template<typename T>
std::string str(const std::set<T>& a ){
	/**
	 * @brief A pythonesque string function
	 * @param x
	 * @return 
	 */
	std::string out = "{";
	for(auto& x : a) {
		out += str(x)+",";
	}
	if(a.size()>0) 
		out.erase(out.size()-1); // remove that last dumb comma
	out += "}";
	
	return out;
}


template<typename T>
std::string str(const std::atomic<T>& a ){
	/**
	 * @brief A pythonesque string function
	 * @param x
	 * @return 
	 */
	return str(a.load());
}


template<typename... Args>
std::string str(std::string _sep, Args... args){

	std::string out = "";
	((out += str(args)+_sep), ...);
	
	// at the end of this we have one extra sep, so let's delete it 
	out.erase(out.size()-_sep.size());
	
	return out; 
}



/**
 * @brief Fleet includes this templated function to allow us to convert strings to a variety of formats. 
 * 		  This is mostly used for reading data from text files. This recursively will unpack containers
 * 		  with some standard delimiters. Since it can handle Fleet types (e.g. defaultdata_t) it
 *        can be used to unpack data stored in strings (see e.g. Models/Sorting)
 * @param s
 * @return 
 */
template<typename T>
T string_to(const std::string s) {
	
	// process some special cases here
	if constexpr(is_specialization<T,std::map>::value) {
		T m;
		for(auto& r : split(s, ',')) { // TODO might want a diff delim here
			auto [x, y] = split<2>(r, ':');
			m[string_to<typename T::key_type>(x)] = string_to<typename T::mapped_type>(y);	
		}
		return m;
	}
	if constexpr(is_specialization<T,std::pair>::value) {
		auto [x,y] = split<2>(s, ':');
		return {string_to<typename T::first_type>(x), string_to<typename T::second_type>(y)};		
	}
	else if constexpr(is_specialization<T,std::vector>::value) {
		T v;
		for(auto& x : split(s, ',')) {
			v.push_back(string_to<typename T::value_type>(x));
		}
		return v;
	}
	else if constexpr(is_specialization<T,std::multiset>::value) {
		T ret;
		for(auto& x : split(s, ',')) {
			ret.insert( string_to<typename T::key_type>(x));
		}
		return ret; 
	}
	else {
		// Otherwise we must have a string constructor
		return T{s};
//		assert(false);
//		return T{};
	}	
}

template<> std::string   string_to(const std::string s) { return s; }
template<> int           string_to(const std::string s) { return std::stoi(s); }
template<> long          string_to(const std::string s) { return std::stol(s); }
template<> unsigned long string_to(const std::string s) { return std::stoul(s); }
template<> double        string_to(const std::string s) { return std::stod(s); }
template<> float         string_to(const std::string s) { return std::stof(s); }
template<> bool          string_to(const std::string s) { assert(s.size()==1); return s=="1"; } // 0/1 for t/f

//template<typename T, typename U>
//std::pair<T,U> string_to(const std::string s) {	
//	auto [x,y] = split<2>(s, ':');
//	return {string_to<T>(x), string_to<U>(y)};		
//}