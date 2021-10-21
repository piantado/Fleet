#pragma once


/**
 * @class CachedCallHypothesis
 * @author Steven Piantadosi
 * @date 20/10/21
 * @file CachedCallHypothesis.h
 * @brief This is a hypothesis that allows you to cache an entire call on data. 
 * 			NOTE: This only catches std::exception, which is not good news
 */

template<typename this_t, typename output_t> // the LOTHypothesis_type from which we read outputs and data etc
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
	
	template<typename data_t>
	void cached_callOne(const data_t& data, bool break_on_error=true) {
		
		// NOTE THIS DOES NOT WORK IF WE HAVE RECURSION 
		// at the level of a lexicon!
		
		if(cached_data != &data) {
			// PRINTN("CACHE MISS", this, cached_data, string());
			cache.resize(data.size());
			for(size_t di=0;di<data.size();di++) {
				try { 				
					cache[di] = static_cast<this_t*>(this)->callOne(data[di].input); 
				} catch( std::exception& e){  
					got_error = true; 
					cache[di] = output_t{};
					if(break_on_error) break; // this break is useful when errors give us -inf, it's much faster
				} 
			}
			// store who is cached
			cached_data = &data;
		}
		//else {
		//	PRINTN("CACHE HIT", this, cached_data, string());			
		//}
	}
	
};