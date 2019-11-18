#pragma once
 
 // A collection of a bunch of magical metaprogramming code 

 #include <tuple>
 #include <type_traits>
 
///~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// Check if a variadic template Ts contains any type X
///~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

template<typename X, typename... Ts>
constexpr bool contains_type() {
	return std::disjunction<std::is_same<X, Ts>...>::value;
}

///~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
/// Find the numerical index (as a nonterminal_t) in a tuple of a given type
///~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

// When users define the macro FLEET_GRAMMAR_TYPES, as in 
// #define FLEET_GRAMMAR_TYPES int,double,char
// then we can use type2int to map each to a unique int. This mapping to ints is
// for example how Fleet stores information in the grammar

// When users define the macro FLEET_GRAMMAR_TYPES, as in 
// #define FLEET_GRAMMAR_TYPES int,double,char
// then we can use type2int to map each to a unique int. This mapping to ints is
// for example how Fleet stores information in the grammar

typedef size_t nonterminal_t;

// from https://stackoverflow.com/questions/42258608/c-constexpr-values-for-types
template <class T, class Tuple>
struct TypeIndex;

template <class T, class... Types>
struct TypeIndex<T, std::tuple<T, Types...>> {
    static const nonterminal_t value = 0;
};

template <class T, class U, class... Types>
struct TypeIndex<T, std::tuple<U, Types...>> {
    static const nonterminal_t value = 1 + TypeIndex<T, std::tuple<Types...>>::value;
};

///~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
/// Fancy trick to see if a class implements operator< (for filtering out in op_MEM code
/// so it doesn't give an error if we use t_input that doesn't implement operator<
/// as long as no op_MEM is called
///~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

#include <type_traits>
#include <utility>
// https://stackoverflow.com/questions/6534041/how-to-check-whether-operator-exists
template<class T, class EqualTo>
struct has_operator_lessthan_impl
{
    template<class U, class V>
    static auto test(U*) -> decltype(std::declval<U>() < std::declval<V>());
    template<typename, typename>
    static auto test(...) -> std::false_type;

    using type = typename std::is_same<bool, decltype(test<T, EqualTo>(0))>::type;
};

template<class T, class EqualTo = T>
struct has_operator_lessthan : has_operator_lessthan_impl<T, EqualTo>::type {};


///~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
/// For managing references in lambda arguments 
///~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~


// Count how many reference types
//template <class T, class... Types>
//struct CountReferences;
template <class T, class... Types>
struct CountReferences {
    static const size_t value = std::is_reference<T>::value + CountReferences<Types...>::value;
};
template <class T>
struct CountReferences<T> { static const size_t value = std::is_reference<T>::value; };


// If there ar eany references in the arguments, only the first can be a reference
template <class T, class... Types>
struct CheckReferenceIsFirst {
	static const bool value = (CountReferences<Types...>::value == 0);
};
template <class T>
struct CheckReferenceIsFirst<T> { static const bool value = true; };


///~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
/// List operations on args
///~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

template<class... Args>
struct TypeHead {
	// get hte first type in Args
	typedef typename std::tuple_element<0, std::tuple<Args...>>::type type;
};

//define a default template<T,args...> that gives head(args)::value if head(args)::value is a reference, otherwise T...
template<class T, class... args>
struct HeadIfReferenceElseT {
	typedef typename std::conditional<std::is_reference<typename TypeHead<args...>::type>::value,
									  typename std::decay<typename TypeHead<args...>::type>::type,
									  T
									  >::type type;
};	
template<class T> 
struct HeadIfReferenceElseT<T> { 
	typedef T type;
};

///~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
/// Access the n'th argument of tuples
///~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

//https://stackoverflow.com/questions/20162903/template-parameter-packs-access-nth-type-and-nth-element

template <int I, class... Ts>
decltype(auto) va_get(Ts&&... ts) {
  return std::get<I>(std::forward_as_tuple(ts...));
}