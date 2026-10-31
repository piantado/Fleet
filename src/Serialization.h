#pragma once

// ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// Serialization.h -- the delimiters used by Fleet's serialize()/deserialize() and strict
// parsing helpers shared by every deserializer.
//
// Serialized formats (none of these do any quoting or escaping):
//
//   Node (Node::parseable)      nt:format;nt:format;...      preorder, one entry per node
//   LOTHypothesis               prior \t likelihood \t posterior \t <Node>
//   Lexicon                     prior | likelihood | posterior | key | <INNER> | key | <INNER> ...
//   TopN                        N \n <T> \n <T> ...
//   save()/load() files         one serialized object per line
//
// Which characters are therefore unsafe inside a rule format:
//   ;   (RuleDelimiter) always -- it splits the node list
//   \n  (TopNDelimiter) whenever a hypothesis is stored in a TopN or a save()/load() file
//   |   (LexiconDelimiter) whenever the hypothesis is a factor of a Lexicon
//   the NullRule's format "■" (as a whole format) -- it is read back as a gap
//   a format shared by two rules of the same nonterminal with the same argument types
// A tab is fine: LOTHypothesis::deserialize splits only at the first three tabs, and ':'
// is fine because the nonterminal is everything before the FIRST ':'. Overloads (one format
// and nonterminal, different argument types) are fine: from_parseable picks the one whose
// children have the right types (only a partial tree whose distinguishing children are null
// gaps is ambiguous, and throws). Grammar::add_vms warns about unsafe formats (they are
// allowed, since a grammar might never be serialized).
//
// Deserializers throw DeserializationError on malformed input; they never assert, so bad
// input fails the same way with and without NDEBUG.
// ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

#include <charconv>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>
#include <vector>

#include "cppstr.h"

namespace Serialization {
	constexpr char NTDelimiter        = ':';  // nt:format
	constexpr char RuleDelimiter      = ';';  // between nodes of a tree
	constexpr char HypothesisDelimiter= '\t'; // between prior, likelihood, posterior, value
	constexpr char LexiconDelimiter   = '|';  // between a Lexicon's scores, keys, and factors
	constexpr char TopNDelimiter      = '\n'; // between a TopN's size and its elements
	constexpr char ConstantDelimiter  = '@';  // between the constants of a ConstantContainer
}

/**
 * @brief Thrown by every deserialize/from_parseable on malformed or unparseable input.
 */
struct DeserializationError : public std::runtime_error {
	using std::runtime_error::runtime_error;
};

/**
 * @brief Parse s as a T, requiring that ALL of s is consumed. For arithmetic types this uses
 *        std::from_chars, which (unlike std::stod/stoul) rejects leading whitespace, a leading
 *        '+', trailing junk, and a minus sign on unsigned types (stoul("-4") wraps around), and
 *        which reads subnormals (std::stod throws out_of_range on them). It reads inf, -inf,
 *        nan, and -nan, so it inverts cppstr's str() exactly for every double and float.
 *        Other types fall back to cppstr's str_to<T>.
 * @param s    the string
 * @param what a description of the field, for the error message
 */
template<typename T>
T parse_exact(const std::string_view s, const char* what="value") {
	if constexpr (std::is_same_v<T,bool>) {
		if(s == "1") return true;
		if(s == "0") return false;
		throw DeserializationError(std::string("*** Could not parse ") + what + " as a bool: \"" + std::string(s) + "\"");
	}
	else if constexpr (std::is_arithmetic_v<T> and not std::is_same_v<T,char>) {
		T x{};
		const auto [ptr, ec] = std::from_chars(s.data(), s.data()+s.size(), x);
		if(ec != std::errc{} or ptr != s.data()+s.size() or s.empty()) {
			throw DeserializationError(std::string("*** Could not parse ") + what + " from \"" + std::string(s) + "\"");
		}
		return x;
	}
	else {
		try {
			return str_to<T>(std::string(s));
		} catch(const std::exception& e) {
			throw DeserializationError(std::string("*** Could not parse ") + what + " from \"" + std::string(s) + "\": " + e.what());
		}
	}
}

/**
 * @brief Split s at the FIRST k occurrences of delimiter, giving exactly k+1 pieces (the last
 *        piece keeps any further delimiters). Throws DeserializationError if s has fewer than
 *        k delimiters.
 */
inline std::vector<std::string> split_first(const std::string& s, const char delimiter, const size_t k, const char* what="string") {
	std::vector<std::string> out;
	out.reserve(k+1);
	size_t start = 0;
	for(size_t i=0;i<k;i++) {
		const auto pos = s.find(delimiter, start);
		if(pos == std::string::npos) {
			throw DeserializationError(std::string("*** Malformed ") + what + ": expected at least " + std::to_string(k) +
			                           " delimiters but found " + std::to_string(i) + " in \"" + s + "\"");
		}
		out.push_back(s.substr(start, pos-start));
		start = pos+1;
	}
	out.push_back(s.substr(start));
	return out;
}
