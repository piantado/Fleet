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
/// Fancy trick to see if a class implements operator< (for filtering out in op_MEM code
/// so it doesn't give an error if we use input_t that doesn't implement operator<
/// as long as no op_MEM is called
///~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

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
/// Check if a given function as a .posterior or not
///~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
//https://stackoverflow.com/questions/1005476/how-to-detect-whether-there-is-a-specific-member-variable-in-class

#include <type_traits>

template <typename T, typename = double>
struct HasPosterior : std::false_type { };

template <typename T>
struct HasPosterior <T, decltype((void) T::posterior, 0)> : std::true_type { };

///~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
/// Check if something is a base class of something else
// https://stackoverflow.com/questions/34672441/stdis-base-of-for-template-classes
///~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

template< template<typename...> class base, typename derived>
struct is_base_of_template_impl {
    template<typename... Ts>
    static constexpr std::true_type  test(const base<Ts...> *);
    static constexpr std::false_type test(...);
    using type = decltype(test(std::declval<derived*>()));
};

template < template <typename...> class base,typename derived>
using is_base_of_template = typename is_base_of_template_impl<base,derived>::type;

///~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
/// Get the n'th type in a parameter pack
// https://stackoverflow.com/questions/15953139/get-the-nth-type-of-variadic-template-templates
///~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~


template<std::size_t N, typename... types>
struct get_Nth_type {
    using type = typename get_Nth_type<N - 1, types...>::type;
};

template<typename T, typename... types>
struct get_Nth_type<0, T, types...> {
    using type = T;
};


