#pragma once

#include "VirtualMachine/VirtualMachineState.h"
#include "VirtualMachine/VirtualMachinePool.h"
#include "DiscreteDistribution.h"
#include "RuntimeCounter.h"
#include "ProgramLoader.h"

/**
 * @class Callable
 * @author Steven Piantadosi
 * @date 03/09/20
 * @file Callable.h
 * @brief A callable function is one that can be called. It must be able to load a program.
 * 			Putting this into an interface allows us to call the object to get output, by compiling
 * 			and running on a VirtualMachine
 */
template<typename input_t, typename output_t, typename VirtualMachineState_t>
class Callable : public ProgramLoader {
public:
	// runcount here is a class which can optionally be passed to VirtualMachineState 
	// 	to count up how many times each operation has been called. However, it defaults to nullptr
	// in which case it is ignored. 
	RuntimeCounter* runtime_counter;

	Callable() : runtime_counter(nullptr) { }


	[[nodiscard]]  virtual bool is_evaluable() const = 0; 
		
	// we defaultly map outputs to log probabilities
	// the this_t must be a ProgramLoader, but other than that we don't care. NOte that this type gets passed all the way down to VirtualMachine
	// and potentially back to primitives, allowing us to access the current Hypothesis if we want
	// LOADERthis_t is the kind of Hypothesis we use to load, and it is not the same as this_t
	// because in a Lexicon, we want to use its InnerHypothesis
	DiscreteDistribution<output_t> call(const input_t x, const output_t err, ProgramLoader* loader, 
				unsigned long max_steps=2048, unsigned long max_outputs=256, double minlp=-10.0){
					
		VirtualMachineState_t* vms = new VirtualMachineState_t(x, err, runtime_counter);	
		push_program(vms->opstack); // write my program into vms (loader is used for everything else)

		VirtualMachinePool<VirtualMachineState_t> pool(max_steps, max_outputs, minlp); 		
		pool.push(vms);		
		return pool.run(loader);				
	}
	
	auto operator()(const input_t x, const output_t err=output_t{}){ // just fancy syntax for call
		return call(x,err);
	}

	output_t callOne(const input_t x, const output_t err, ProgramLoader* loader) {
		// we can use this if we are guaranteed that we don't have a stochastic Hypothesis
		// the savings is that we don't have to create a VirtualMachinePool		
		VirtualMachineState_t vms(x, err, runtime_counter);		

		push_program(vms.opstack); // write my program into vms (loader is used for everything else)
		return vms.run(loader); // default to using "this" as the loader		
	}
	
	output_t callOne(const input_t x, const output_t err=output_t{}) {
		return callOne(x,err,this);	
	}
	
	virtual DiscreteDistribution<output_t> call(const input_t x, const output_t err) {
		// Note there's some fanciness there because this is a Callable type, which then should then call push_program (or whatever) overwritten by the subclass
		return call(x, err, this); // defaulty I myself handle recursion/loading 
	}
	
	
};