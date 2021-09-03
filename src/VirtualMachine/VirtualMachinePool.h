#pragma once

#include "DiscreteDistribution.h"
#include "VirtualMachineControl.h"

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
class VirtualMachinePool : public VirtualMachineControl {

	/**
	 * @class compare_VirtualMachineState_t_prt
	 * @author Steven Piantadosi
	 * @date 06/09/20
	 * @file VirtualMachinePool.h
	 * @brief Compare pointers to VirtualMachineStates by their log probability
	 */	
	struct compare_VirtualMachineState_t_prt {
		bool operator()(const VirtualMachineState_t* lhs, const VirtualMachineState_t* rhs) { return lhs->lp < rhs->lp;	}
	};
	
public:	

	typedef typename VirtualMachineState_t::output_t output_t;
	typedef typename VirtualMachineState_t::input_t  input_t;
	
	// how many steps have I run so far? -- this needs to be global so that we can keep track of 
	// whether we should push a new state that occurs too late. 
	unsigned long total_vms_steps; 
	double worst_lp;
	unsigned long total_instruction_count;
	
	std::priority_queue<VirtualMachineState_t*, std::vector<VirtualMachineState_t*>, VirtualMachinePool::compare_VirtualMachineState_t_prt> Q; // Q of states sorted by probability
	//std::priority_queue<VirtualMachineState_t*, ReservedVector<VirtualMachineState_t*,16>, VirtualMachinePool::compare_VirtualMachineState_t_prt> Q; // Does not seem to speed things up 

	VirtualMachinePool() : total_vms_steps(0), worst_lp(infinity), total_instruction_count(0) { 
	}
	

	virtual ~VirtualMachinePool() {
		clear();
	}
	
	/**
	 * @brief Remove everything and reset my values
	 */	
	virtual void clear() {
		while(not Q.empty()) {
			VirtualMachineState_t* vms = Q.top(); Q.pop();
			delete vms;
		}
		total_vms_steps = 0;
		worst_lp = infinity;
	}
	
	/**
	 * @brief Returns true if I would add something with this lp, given my MAX_STEPS and the stack. This lets us speed up by checking if we would add before copying/constructing a VMS
	 * @param lp
	 * @return 
	 */
	bool wouldIadd(double lp) {
		// we won't add stuff that's worse than MIN_LP or that will never run
		// (it will never run if it's lp < worst_lp and we don't have enough steps to get there)
		return lp > MIN_LP and 
			   (Q.size() <= (MAX_STEPS-total_vms_steps) or lp > worst_lp);
	}
	
	/**
	 * @brief Add the VirtualMachineState_t o to this pool (but again checking if I'd add)
	 * @param o
	 */
	void push(VirtualMachineState_t* o) { 
		if(wouldIadd(o->lp)){ // TODO: might be able to add an optimization here that doesn't push if we don't have enough steps left to get it 
			Q.push(o);			
			worst_lp = std::min(worst_lp, o->lp); //keep track of the worst we've seen
		} 
		// else nothing 
	}
	
	/**
	 * @brief This is an important opimization where we will make a copy of x, push v into it's stack, and increment its lp by lpinc only if it will 
	 * 			be added to the queue, which we check in the pool here. This saves us from having to use the VirtualMachineState constructor (e.g. making a 
	 * 			copy, which is expensive if we are copying all the stacks) if the copy won't actually be added to the queue.
	 * @param x
	 * @param v
	 * @param lpinc
	 * @return 
	 */	
	template<typename T>
	bool copy_increment_push(const VirtualMachineState_t* x, T v, double lpinc) {
		if(wouldIadd(x->lp + lpinc)) {	
			assert(x->status == vmstatus_t::GOOD);
			auto s = new VirtualMachineState_t(*x); // copy
			s->template push<T>(v); // add v
			s->lp += lpinc;
			this->push(s);	
			return true;
		}	
		return false;
	}
	
	/**
	 * @brief Same as copy_increment_push, but does not make a copy -- just add
	 * @param s
	 * @param v
	 * @param lpinc
	 * @return 
	 */
	template<typename T>
	bool increment_push(VirtualMachineState_t* s, T v, double lpinc) {		
		if(wouldIadd(s->lp + lpinc)) {		
			assert(s->status == vmstatus_t::GOOD);
			s->template push<T>(v); // add this
			s->lp += lpinc;
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
	DiscreteDistribution<output_t> run() { 

		DiscreteDistribution<output_t> out;
		
		total_vms_steps = 0;
		while(total_vms_steps < MAX_STEPS && out.size() < MAX_OUTPUTS && !Q.empty()) {
			
			VirtualMachineState_t* vms = Q.top(); Q.pop();
			assert(vms->status == vmstatus_t::GOOD);
			assert(vms->lp >= MIN_LP);
			
			// if we ever go back to the non-pointer version, we might need fanciness to move out of top https://stackoverflow.com/questions/20149471/move-out-element-of-std-priority-queue-in-c11
			assert(vms->lp >= MIN_LP);
			
			total_vms_steps++;
			
			auto y = vms->run();
			
			if(vms->status == vmstatus_t::COMPLETE) { // can't add up probability for errors
				out.addmass(y, vms->lp);
				total_instruction_count += vms->runtime_counter.total;
			}
			
			// not else if because we need to delete complete ones too
			if(vms->status != vmstatus_t::RANDOM_CHOICE_NO_DELETE) {
				total_instruction_count += vms->runtime_counter.total; /// HMM Do we want to count these too? I guess since its called "total"....
				delete vms; // if our previous copy isn't pushed back on the stack, delete it
			} 
			else {
				// else restore to running order when we're RANDOM_CHOICE_NO_DELETE
				vms->status = vmstatus_t::GOOD;
			}
		}

		// this leaves some in the stack, but they are cleaned up by the destructor
		
		return out;		
	}
	
	
	/**
	 * @brief Run but return a vector of completed virtual machines instead of marginalizing outputs. 
	 * 			You might want this if you needed to separate execution paths, or wanted to access runtime counts. This also
	 * 			can get joint distributions on outputs by running a VirtualMachineState multiple times
	 * @param loader
	 * @return 
	 */	
	std::vector<VirtualMachineState_t> run_vms() { 
		
		std::vector<VirtualMachineState_t> out;
		
		total_vms_steps = 0;
		while(total_vms_steps < MAX_STEPS && !Q.empty()) {
			
			VirtualMachineState_t* vms = Q.top(); Q.pop();
			assert(vms->status == vmstatus_t::GOOD);
			assert(vms->lp >= MIN_LP);
			
			total_vms_steps++;
			
			vms->run();
				
			if(vms->status == vmstatus_t::COMPLETE) {
				out.push_back(*vms);
				total_instruction_count += vms->runtime_counter.total; 
			}
			
			if(vms->status != vmstatus_t::RANDOM_CHOICE_NO_DELETE) { 
				total_instruction_count += vms->runtime_counter.total; 
				delete vms; // if our previous copy isn't pushed back on the stack, delete it
			}
			else {
				// else restore to running order
				vms->status = vmstatus_t::GOOD;
			}
		}

		return out;		
	}
	
};
