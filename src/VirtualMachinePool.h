#ifndef VIRTUAL_MACHINE_POOL
#define VIRTUAL_MACHINE_POOL

#include "Fleet.h"

template<typename t_x, typename t_return>
class VirtualMachineState;

template<typename t_x, typename t_return>
class VirtualMachinePool {
	// This manages a collection of VirtualMachines -- this is what handles the enumeration of flip by probability. 
	// Basically each machine state stores the state of some evaluator and is able to push things back on to the Q
	// if it encounters a random flip
	
	static const unsigned long MAX_STEPS = 512;
	double min_lp; // prune out stuff with less probability than this
	
public:
	std::priority_queue<VirtualMachineState<t_x,t_return>*> Q; // Q of states sorted by probability

	VirtualMachinePool(double mlp=-10) : min_lp(mlp) {
	}
	
	void push(VirtualMachineState<t_x,t_return>* o) { //NOTE: can NOT take a reference
		//CERR "POOL PUSHING " TAB &o ENDL;
		if(o->lp >= min_lp){
			Q.push(o);
		}
		else { // this assumes we take ownership of everything with o
			delete o;
		}
	}
	
	std::map<t_return, double> run(Dispatchable<t_x,t_return>* dispatcher, Dispatchable<t_x,t_return>* loader) { 
		// This runs and adds up the probability mass for everything, returning a dictionary outcomes->log_probabilities
		
		std::map<t_return,double> out;
		
		size_t steps = 0;
		while(!Q.empty()) {
			VirtualMachineState<t_x,t_return>* vms = Q.top(); Q.pop();
			
			assert(vms->lp >= min_lp);
			
			if(steps++ < MAX_STEPS) { // do the actual stuff; otehrwise we just delete below
				auto y = vms->run(this, dispatcher, loader);
				
				if(vms->aborted == NO_ABORT) { // can't add up probability for errors
				//	CERR "Output " TAB y TAB vms.aborted TAB vms.x TAB vms.recursion_depth TAB vms.lp TAB Q.size() ENDL;
					if(out.find(y) == out.end()) {
						out[y] = vms->lp;
					}
					else {
						out[y] = logplusexp(out[y], vms->lp);
					}
				}
			}
			
			delete vms;
		}
		
		return out;		
	}
	
};

#endif