#pragma once

#include "Fleet.h"
#include "DiscreteDistribution.h"



template<typename t_x, typename t_return>
class VirtualMachinePool {
	// This manages a collection of VirtualMachines -- this is what handles the enumeration of flip by probability. 
	// Basically each machine state stores the state of some evaluator and is able to push things back on to the Q
	// if it encounters a random flip
	
	const unsigned long max_steps;
	const unsigned long max_outputs;
	
	double min_lp; // prune out stuff with less probability than this
	double worst_lp = infinity;
	
public:

	std::priority_queue<VirtualMachineState<t_x,t_return>,
						std::vector<VirtualMachineState<t_x,t_return>> > Q; // Q of states sorted by probability

	VirtualMachinePool(unsigned long ms=2048, unsigned long mo=256, double mlp=-20) 
					   : max_steps(ms), max_outputs(mo), min_lp(mlp) {
	}
		
	bool wouldIadd(double lp) {
		// returns true if I would add something with this lp, given my max_steps and the stack
		return lp >= min_lp && (Q.size() < max_steps || lp > worst_lp);
	}
	
	void push(VirtualMachineState<t_x,t_return>& o) { 
		//CERR "POOL PUSHING " TAB &o ENDL;
		if(wouldIadd(o.lp)){ // TODO: might be able to add an optimization here that doesn't push if we don't have enough steps left to get it 
			Q.push(o);			
			worst_lp = MIN(worst_lp, o.lp); //keep track of the worst we've seen
		}
	}
	
	template<typename T>
	void copy_increment_push(const VirtualMachineState<t_x,t_return>& x, T v, double lpinc) {
		// This is an important opimization where we will make a copy of x, 
		// push v into it's stack, and increment its lp by lpinc only if it will
		// be added to the queue, whcih we check in the pool here. This saves us from
		// having to use the VirtualMachineState constructor (e.g. making a copy, which is
		// expensive if we are copying all the stacks) if the copy won't actually be added
		// to the queue
		if(wouldIadd(x.lp + lpinc)) {		
			VirtualMachineState<t_x,t_return> s = x;
			s.template push<T>(v); // add this
			s.increment_lp(lpinc);
			this->push(s);	
		}	
	}
	
	
	DiscreteDistribution<t_return> run(Dispatchable<t_x,t_return>* dispatcher, Dispatchable<t_x,t_return>* loader) { 
		// This runs and adds up the probability mass for everything, returning a dictionary outcomes->log_probabilities
		
		DiscreteDistribution<t_return> out;
		
		size_t steps = 0;
		while(steps < max_steps && out.size() < max_outputs && !Q.empty()) {

			VirtualMachineState<t_x,t_return> vms = std::move(Q.top()); Q.pop();
			assert(vms.lp >= min_lp);
			
			steps++;
			
			auto y = vms.run(this, dispatcher, loader);
				
			if(vms.aborted == abort_t::NO_ABORT) { // can't add up probability for errors
				out.addmass(y, vms.lp);
			}
		}
		
		// this leaves some in the stack, but they are cleaned up by the destructor
		
		return out;		
	}
	
};
