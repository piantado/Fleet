#pragma once

#include "DiscreteDistribution.h"
#include "Hypotheses/Interfaces/ProgramLoader.h"

#include <vector>
#include <queue>


/**
* @brief Compute the marginal output of a collection of VirtualMachineStates (as we might get from run_vms)
* @param v
*/	
template<typename VirtualMachineState_t>
DiscreteDistribution<typename VirtualMachineState_t::output_t>  marginal_vms_output(std::vector<VirtualMachineState_t>& v) {
	
	DiscreteDistribution<typename VirtualMachineState_t::output_t> out;
	for(auto& vms : v) {
		if(vms.status == vmstatus_t::COMPLETE) { // can't add up probability for errors
			out.addmass(vms.get_output(), vms.lp);
		}
		else {
			assert(false && "*** You should not be computing marginal outcomes with VMS states that didn't complete.");
		}
	}
	return out;
}

	
	
/**
 * @class VirtualMachinePool
 * @author piantado
 * @date 02/02/20
 * @file VirtualMachinePool.h
 * @brief  This manages a collection of VirtualMachines -- this is what handles the enumeration of flip by probability. 
 * 			Basically each machine state stores the state of some evaluator and is able to push things back on to the Q
 * 			if it encounters a random flip
 * 			This stores pointers because it is impossible to copy out of std collections, so we are constantly
 * 			having to call VirtualMachineState constructors. Using pointers speeds us up by about 20%.
 */
template<typename VirtualMachineState_t>
class VirtualMachinePool {

	struct compare_VirtualMachineState_t_prt {
		bool operator()(const VirtualMachineState_t* lhs, const VirtualMachineState_t* rhs) { return lhs->lp < rhs->lp;	}
	};

	
public:	
	unsigned long max_state_run;
	unsigned long max_outputs;
	
	// how many steps have I run so far? -- this needs to be global so that we can keep track of 
	// whether we should push a new state that occurs too late. 
	unsigned long current_steps; 
	
	double min_lp; // prune out stuff with less probability than this
	double worst_lp = infinity;
	
	std::priority_queue<VirtualMachineState_t*, std::vector<VirtualMachineState_t*>, VirtualMachinePool::compare_VirtualMachineState_t_prt> Q; // Q of states sorted by probability
//	std::priority_queue<VirtualMachineState_t*, ReservedVector<VirtualMachineState_t*,1024>, VirtualMachinePool::compare_VirtualMachineState_t_prt> Q; // Does not seem to speed things up 


	VirtualMachinePool(unsigned long ms=2048, unsigned long mo=256, double mlp=-10) 
					   : max_state_run(ms), max_outputs(mo), current_steps(0), min_lp(mlp) {
	}
	
	// a constructor that also allows us to deduce VirtualMachineState_t type
	VirtualMachinePool(VirtualMachineState_t* vms, unsigned long ms, unsigned long mo, double mlp) 
					   : max_state_run(ms), max_outputs(mo), current_steps(0), min_lp(mlp) {
		push(vms);
	}
	
	
	virtual ~VirtualMachinePool() {
		clear();
	}
		
	virtual void clear() {
		while(!Q.empty()) {
			VirtualMachineState_t* vms = Q.top(); Q.pop();
			delete vms;
		}
		
		current_steps = 0;
		worst_lp = infinity;
	}
	
		
	bool wouldIadd(double lp) {
		/**
		 * @brief Returns true if I would add something with this lp, given my max_state_run and the stack. This lets us speed up by checking if we would add before copying/constructing a VMS
		 * @param lp
		 * @return 
		 */
		return lp >= min_lp and 
			   (Q.size() <= (max_state_run-current_steps) or lp > worst_lp);
	}
	
	void push(VirtualMachineState_t* o) { 
		/**
		 * @brief Add the VirtualMachineState_t o to this pool (but again checking if I'd add)
		 * @param o
		 */

		if(wouldIadd(o->lp)){ // TODO: might be able to add an optimization here that doesn't push if we don't have enough steps left to get it 
			Q.push(o);			
			worst_lp = std::min(worst_lp, o->lp); //keep track of the worst we've seen
		}
	}
	
	template<typename T>
	bool copy_increment_push(const VirtualMachineState_t* x, T v, double lpinc) {
		/**
		 * @brief This is an important opimization where we will make a copy of x, push v into it's stack, and increment its lp by lpinc only if it will 
		 * 			be added to the queue, which we check in the pool here. This saves us from having to use the VirtualMachineState constructor (e.g. making a 
		 * 			copy, which is expensive if we are copying all the stacks) if the copy won't actually be added to the queue.
		 * @param x
		 * @param v
		 * @param lpinc
		 * @return 
		 */
 
		if(wouldIadd(x->lp + lpinc)) {	
			auto s = new VirtualMachineState_t(*x); // copy
			s->template push<T>(v); // add v
			s->increment_lp(lpinc);
			this->push(s);	
			return true;
		}	
		return false;
	}
	
	template<typename T>
	bool increment_push(VirtualMachineState_t* s, T v, double lpinc) {
		/**
		 * @brief Same as copy_increment_push, but does not make a copy -- just add
		 * @param s
		 * @param v
		 * @param lpinc
		 * @return 
		 */
		if(wouldIadd(s->lp + lpinc)) {		
			s->template push<T>(v); // add this
			s->increment_lp(lpinc);
			this->push(s);	
			return true;
		}	
		return false;
	}

	/**
	 * @brief This runs and adds up the probability mass for everything, returning a dictionary outcomes->log_probabilities. This is the main 
	 * 		  running loop, which pops frmo the top of our queue, runs, and continues until we've done enough or all. 
	 * 		  Note that objects lower than min_lp are not ever pushed onto the queue.
	 * @param dispatcher
	 * @param loader
	 * @return 
	 */
	DiscreteDistribution<typename VirtualMachineState_t::output_t> run(ProgramLoader* loader) { 

		
		DiscreteDistribution<typename VirtualMachineState_t::output_t> out;
		
		current_steps = 0;
		while(current_steps < max_state_run && out.size() < max_outputs && !Q.empty()) {
			
			VirtualMachineState_t* vms = Q.top(); Q.pop();
			// if we ever go back to the non-pointer version, we might need fanciness to move out of top https://stackoverflow.com/questions/20149471/move-out-element-of-std-priority-queue-in-c11
			assert(vms->lp >= min_lp);
			
			current_steps++;
			
			auto y = vms->run(this, loader);
				
			if(vms->status == vmstatus_t::COMPLETE) { // can't add up probability for errors
				out.addmass(y, vms->lp);
			}
			
			if(vms->status != vmstatus_t::RANDOM_CHOICE_NO_DELETE) {
				delete vms; // if our previous copy isn't pushed back on the stack, delete it
			}
		}

		// this leaves some in the stack, but they are cleaned up by the destructor
		
		return out;		
	}
	
	
	/**
	 * @brief Run but return a vector of completed virtual machines instead of marginalizing outputs. 
	 * 			You might want this if you needed to separate execution paths, or wanted to access runtime counts
	 * @param loader
	 * @return 
	 */	
	std::vector<VirtualMachineState_t> run_vms(ProgramLoader* loader) { 

		
		std::vector<VirtualMachineState_t> out;
		
		current_steps = 0;
		while(current_steps < max_state_run && out.size() < max_outputs && !Q.empty()) {
			
			VirtualMachineState_t* vms = Q.top(); Q.pop();
			assert(vms->lp >= min_lp);
			
			current_steps++;
			
			vms->run(this, loader);
				
			if(vms->status == vmstatus_t::COMPLETE) { // can't add up probability for errors
				out.push_back(*vms); // note this pushes a copy
			}
			
			if(vms->status != vmstatus_t::RANDOM_CHOICE_NO_DELETE) {
				delete vms; // if our previous copy isn't pushed back on the stack, delete it
			}
		}

		return out;		
	}
	
};
