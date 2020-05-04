#pragma once 


typedef size_t nonterminal_t;

/// Helpers to Find the numerical index (as a nonterminal_t) in a tuple of a given type

// When users define the macro FLEET_GRAMMAR_TYPES, as in 
// #define FLEET_GRAMMAR_TYPES int,double,char
// then we can use type2int to map each to a unique int. This mapping to ints is
// for example how Fleet stores information in the grammar

// When users define the macro FLEET_GRAMMAR_TYPES, as in 
// #define FLEET_GRAMMAR_TYPES int,double,char
// then we can use type2int to map each to a unique int. This mapping to ints is
// for example how Fleet stores information in the grammar

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
