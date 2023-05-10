#include <functional>
#include <cassert>

/* Some helpful functions for functional programming. This defines
 * 
 * The type ft<a,b,c> is a std::function that takes b,c as arguments and returns a
 * 
 * */


template<typename out, typename... args>
using ft = std::function<out(args...)>;

// compose f and g: f(g(args))
template <typename Fout, typename Gout, typename... Gargs >
ft<Fout,Gargs...> compose(ft<Fout,Gout>& f, ft<Gout, Gargs...>& g) {
	return [=](Gargs... args) { return f(g(args...)); };
}