#pragma once 


/**
 * @class Searchable
 * @author steven piantadosi
 * @date 03/02/20
 * @file Searchable.h
 * @brief A class is searchable if permits us to enumerate and make its neighbors.
 * This class is used by MCTS and allows us to incrementally search a hypothesis.
 */
template<typename this_t, typename... Args>
class Searchable {
public:

	/**
	 * @brief Count the number of neighbors that are possible. 
	 * 			(This should be size_t but int is more convenient.)
	 */
	[[nodiscard]]  virtual int  neighbors() const = 0; // how many neighbors do I have?
	
	/**
	 * @brief Modify this hypothesis to become the k'th neighbor. NOTE This does not compile since it might not be complete. 
	 * @param k
	 */	
	virtual void expand_to_neighbor(int k)  = 0; // modifies this hypothesis rather than making a new one
	

	/**
	 * @brief Return a new hypothesis which is the k'th neighbor (just calls expand_to_neighbor) 
	 *        NOTE This does not compile since it might not be complete. 
	 * @param k
	 * @return 
	 */	
	[[nodiscard]]  virtual this_t  make_neighbor(int k) const {
		this_t out =  *static_cast<this_t*>(const_cast<Searchable<this_t,Args...>*>(this));
		out.expand_to_neighbor(k);
		return out; 
	}
	
	/**
	 * @brief What is the prior of the k'th neighbor? This does not need to return the full prior, only relative (among ks)
	 * @param k
	 */	
	virtual double neighbor_prior(int k)    = 0; // what is the prior of this neighbor?

	 
	/**
	 * @brief Fill in all the holes in this hypothesis, at random, modifying self. 
	 * 		  NOTE for LOTHypotheses this will also compile, which is what we need to do for a LOTHypothesis
	 */	
	virtual void complete() = 0; 						
	
	/**
	 * @brief Check if we can evaluate this node (meaning compute a prior and posterior). NOTE that this is not the
	 * 		  same as whether it has zero neighbors, since lexica might have neighbors but be evalable. 
	 */	
	[[nodiscard]]  virtual bool is_evaluable() const       = 0; 
	
};
