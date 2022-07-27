#pragma once


/**
 * @class CachedCallHypothesis
 * @author Steven Piantadosi
 * @date 20/10/21
 * @file CachedCallHypothesis.h
 * @brief This is a hypothesis that allows you to cache an entire call on data. 
 * 			NOTE: This only catches std::exception, which is not good news
 */

template<typename this_t, typename datum_t, typename output_t> // the LOTHypothesis_type from which we read outputs and data etc
class CachedCallHypothesis  {
	
	void const* cached_data = nullptr; // when null, we recompute the cache
public:
	std::vector<output_t> cache;
	bool got_error; // did any data point throw an error?
	
	CachedCallHypothesis() : got_error(false) {	}
	
	void clear_cache() {
		cached_data = nullptr;
		got_error = false;
	}
	
	/**
	 * @brief This is how we access the data before calling -- needed to say how this interfaces with the data
	 * @param di
	 * @return 
	 */	
	virtual output_t cached_call_wrapper(const datum_t& di) { throw NotImplementedError(); }
// {
//		return static_cast<this_t*>(this)->call(di.input);
//	}
//	
	void cached_call(const std::vector<datum_t>& data, bool break_on_error=true) {
		
		// NOTE THIS DOES NOT WORK IF WE HAVE RECURSION 
		// at the level of a lexicon!
		
		if(cached_data != &data) {
			// PRINTN("CACHE MISS", this, cached_data, string());
			cache.resize(data.size());
			for(size_t di=0;di<data.size();di++) {
				try { 				
					cache[di] = this->cached_call_wrapper(data[di]);
				} catch( std::exception& e){  
					got_error = true; 
					cache[di] = output_t{};
					if(break_on_error) break; // this break is useful when errors give us -inf, it's much faster
				} 
			}
			// store who is cached
			cached_data = &data;
		}
	}
	
};