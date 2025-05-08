
#pragma once

#include <set>
#include <map>
#include "IO.h"
#include "Numerics.h"
#include "OrderedLock.h"

#include "FleetArgs.h"
#include "OrderedLock.h"
#include "Interfaces/Serializable.h"


/**
 * @class TopN
 * @author steven piantadosi
 * @date 03/02/20
 * @file Top.h
 * @brief A TopN is a n object you can "add" hypotheses to (via add or <<) and it stores the best N of them.
 * 			This is widely used in Fleet in order to find good approximations to the top hypotheses found in MCTS or MCMC.
 * 			The default constructor here reads from FleetArgs::ntop as the default value
 * */
template<class T>
class TopN : public Serializable<TopN<T>> {	
	using Hypothesis_t = T;
	
	OrderedLock lock;
	
	const static char SerializationDelimiter = '\n';
	
public:
	const static size_t MAX_N = SIZE_MAX; // largest N we can have -- useful if we need to store union of everything
	
	std::set<T> s; 
	bool print_best; // should we print the best each time we see it?
	std::atomic<size_t> N;	
	
	/**
	 * @class HasPosterior
	 * @author piantado
	 * @date 07/05/20
	 * @file Top.h
	 * @brief Check if a type has a posterior function or not
	 */
	template <typename X, typename = double>
	struct HasPosterior : std::false_type { };
	template <typename X>
	struct HasPosterior <X, decltype((void) X::posterior, 0)> : std::true_type { };
	//https://stackoverflow.com/questions/1005476/how-to-detect-whether-there-is-a-specific-member-variable-in-class
		
	TopN(size_t n=FleetArgs::ntop) : N(n) { set_print_best(FleetArgs::top_print_best); }
	
	TopN(const TopN<T>& x) {
		clear();
		set_size(x.N);
		set_print_best(x.print_best); // must be set before we add!
		add(x);
	}
	TopN(TopN<T>&& x) {
		set_size(x.N);
		set_print_best(x.print_best);
		s = std::move(x.s);
	}
	
	void operator=(const TopN<T>& x) {
		clear();
		set_print_best(x.print_best);				
		set_size(x.N);
		add(x);
	}
	void operator=(TopN<T>&& x) {
		set_print_best(x.print_best);
		set_size(x.N);
		s = std::move(x.s);
	}

	void set_size(size_t n) {
		/**
		 * @brief Set the size of n that I cna have. NOTE: this does not resize the existing data. 
		 * @param n
		 */
		assert((empty() or n >= N) && "*** Settting TopN size to something smaller probably won't do what you want");
		N = n;
	}
	void set_print_best(bool b) {
		/**
		 * @brief As I add things, should I print the best I've seen so far? 
		 * @param b
		 */
		if(N == 0) { CERR "# *** Warning since N=0, TopN will not print the best even though you set_print_best(true)!" ENDL; }
		print_best = b;
	}

	size_t size() const {
		/**
		 * @brief How many are currently in the set? (NOT the total number allowed)
		 * @return 
		 */
		
		return s.size();
	}
	bool empty() const {
		/**
		 * @brief Is it empty?
		 * @return 
		 */
		
		return s.size() == 0;
	}
	
	const std::set<T>& values() const {
		/**
		 * @brief Return a multiset of all the values in TopN
		 * @return 
		 */
		
		return s;
	}
	

	/**
	 * @brief Does this contain x?
	 * @param x
	 * @return 
	 */
	bool contains(const T& x) const {
		// TODO: Lock guard?
		return s.find(x) != s.end();
	}

	/**
	 * @brief Adds x to the TopN. This will return true if we added (false otherwise)
	 * @param x
	 * @return 
	 */
	[[maybe_unused]] bool add(const T& x) { 
		/**
		 * @brief Add x. NOTE that we do not add objects x such that x.posterior == -infinity or NaN
		 * @param x
		 */
		 
		if(N == 0) return false; // if we happen to not store anything
		
		// enable this when we use posterior as the variable, we don't add -inf values
		if constexpr (HasPosterior<T>::value) {			
			if(std::isnan(x.posterior) or x.posterior == -infinity) 
				return false;
		}
		
		std::lock_guard guard(lock);

		// if we aren't in there and our posterior is better than the worst
		if(not contains(x)) {
			if(s.size() < N or (empty() or worst() < x )) { // skip adding if its the worst -- can't call worst_score due to lock
				T xcpy = x;
				
				if(print_best and (empty() or best() < x ))  {
					xcpy.show();
				}
			
				// add this one
				s.insert(xcpy); 
				
				// and remove until we are the right size
				while(s.size() > N) {
					s.erase(s.begin()); 
				}
				
				return true; // we added
			}
		} 
		
		return false; // we didn't add
		
	}
	
	[[maybe_unused]] bool operator<<(const T& x) {
		/**
		 * @brief Friendlier syntax for adding.
		 * @param x
		 */
		
		return add(x);
	}
	
	void add(const TopN<T>& x) { // add from a whole other topN
		/**
		 * @brief Add everything in x to this one. 
		 * @param x
		 */
	
		for(auto& h: x.s){
			add(h);
		}
	}
	void add(const std::set<T>& x) { // add from a whole other topN
		/**
		 * @brief Add everything in x to this one. 
		 * @param x
		 */
	
		for(auto& h: x){
			add(h);
		}
	}
	void operator<<(const TopN<T>& x) {
		add(x);
	}
	
	/**
	 * @brief Pops off the top -- you usually won't want to do this and it's not efficient
	 */	
	void pop() {
		if(not empty())
			s.erase(  std::next(s.rbegin()).base() ); // convert reverse iterator to forward to delete https://stackoverflow.com/questions/1830158/how-to-call-erase-with-a-reverse-iterator
	}
	
	const T& best() const { 
		/**
		 * @brief Returns a reference to the best element currently stored
		 * @return 
		 */
		
		assert( (!s.empty()) && "You tried to get the max from a TopN that was empty");
		return *s.rbegin();  
	}
	const T& worst() const { 
		/**
		 * @brief Returns a reference to the worst element currently stored
		 * @return 
		 */
		
		assert( (!s.empty()) && "You tried to get the min from a TopN that was empty");
		return *s.begin(); 
	}
	
	double Z(double temp=1.0) { // (can't be const because it locks)
		/**
		 * @brief Compute the logsumexp of all of the elements stored at temperature T
		 * @return 
		 */
	
		double z = -infinity;
		std::lock_guard guard(lock);
		for(const auto& x : s) 
			z = logplusexp(z, x.posterior/temp);
		return z;       
	}
	
	void print(std::string prefix="") {
		/**
		 * @brief Sort and print from worst to best
		 * @param prefix - an optional prefix to print before each line
		 */
		
		for(auto& h : sorted()) {
			h.show(prefix);
		}
	}
	
	/**
	 * @brief Sorted values
	 * @param increasing -- do we sort increasing or decreasing?
	 */
	std::vector<T> sorted(bool increasing=true) const {
		
		std::vector<T> v(s.size());
		
		std::copy(s.begin(), s.end(), v.begin());
		
		std::sort(v.begin(), v.end(), std::less<T>()); 
		
		// this prevents us from having to define operator>
		// because it should be faster to do std::sort(v.begin(), v.end(), std::greater<T>());
		if(not increasing) {
			std::reverse(v.begin(), v.end());
		}
		
		return v;
	}
	
	void clear() {
		/**
		 * @brief Remove everything 
		 */
		
		std::lock_guard guard(lock);
		s.erase(s.begin(), s.end());
	}
	
	template<typename data_t>
	[[nodiscard]] TopN compute_posterior(data_t& data){
		/**
		 * @brief Returns a NEW TopN where each current hypothesis is evaluated on the data. NOTE: If a hypothesis has a new posterior of -inf or NaN, it won't be added. 
		 * @param data
		 * @return 
		 */
		
		// NOTE: Since this modifies the hypotheses, it returns a NEW TopN
		TopN o(N);
		for(auto h : values() ){
			h.compute_posterior(data);
			o << h;
		}
		return o;
	}
	
	virtual std::string serialize() const override { 
		std::string out = str(N);
		for(auto& h : s) {
			out += SerializationDelimiter + h.serialize();
		}
		return out;
	}
	
	static TopN<T> deserialize(const std::string& s) { 
		
		TopN<T> out;
		auto q = split(s, SerializationDelimiter);
		out.set_size(string_to<size_t>(q.front())); q.pop_front();
		
		for(auto& x : q) {
			out << T::deserialize(x);
		}
		
		return out; 
	}
	
	
};

// Handy to define this so we can put TopN into a set
template<typename HYP>
void operator<<(std::set<HYP>& s, TopN<HYP>& t){ 
	for(auto h: t.values()) {
		s.insert(h);
	}
}

// A best is a TopN with n=1, and permits casting to type T
template<class T>
struct Best : public TopN<T> {
	Best() : TopN<T>(1) {}
	
	operator T(){
		return this->best();
	}
//	operator T(){
//		return this->best();
//	}
//	
//	operator T(){
//		return this->best();
//	}
};