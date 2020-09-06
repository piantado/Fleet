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
	
	Callable() { }

	/**
	 * @brief Can this be evalutaed (really should be named -- can be called?). Sometimes partial hypotheses
	 * 		  can't be called.
	 */
	[[nodiscard]]  virtual bool is_evaluable() const = 0; 
		
	/**
	 * @brief Run the virtual machine on input x, and marginalize over execution paths to return a distribution
	 * 		  on outputs. Note that loader must be a program loader, and that is to handle recursion and 
	 *        other function calls. 
	 * @param x - input
	 * @param err - output value on error
	 * @param loader - where to load recursive calls
	 * @param max_steps - max steps the virtual machine pool will run for
	 * @param max_outputs - max outputs the virtual machine pool will run for
	 * @param minlp - the virtual machine pool doesn't consider paths less than this probability
	 * @return 
	 */	
	DiscreteDistribution<output_t> call(const input_t x, const output_t err, ProgramLoader* loader){
					
		VirtualMachineState_t* vms = new VirtualMachineState_t(x, err);	
		push_program(vms->opstack); // write my program into vms (loader is used for everything else)

		VirtualMachinePool<VirtualMachineState_t> pool; 		
		pool.push(vms);		
		return pool.run(loader);				
	}
	
	auto operator()(const input_t x, const output_t err=output_t{}){ // just fancy syntax for call
		return call(x,err);
	}

	/**
	 * @brief A variant of call that assumes no stochasticity and therefore outputs only a single value. 
	 * 		  (This uses a nullptr virtual machine pool, so will throw an error on flip)
	 * @param x
	 * @param err
	 * @param loader
	 * @return 
	 */
	output_t callOne(const input_t x, const output_t err, ProgramLoader* loader) {
		// we can use this if we are guaranteed that we don't have a stochastic Hypothesis
		// the savings is that we don't have to create a VirtualMachinePool		
		VirtualMachineState_t vms(x, err);		

		push_program(vms.opstack); // write my program into vms (loader is used for everything else)
		return vms.run(loader); // default to using "this" as the loader		
	}
	
	output_t callOne(const input_t x, const output_t err=output_t{}) {
		return callOne(x,err,this);	
	}
		
	/**
	 * @brief Default call with myself as the loader.
	 * @param x
	 * @param err
	 * @return 
	 */	
	virtual DiscreteDistribution<output_t> call(const input_t x, const output_t err) {
		// Note there's some fanciness there because this is a Callable type, which then should then call push_program (or whatever) overwritten by the subclass
		return call(x, err, this); // defaulty I myself handle recursion/loading 
	}
	
	
	/**
	 * @brief A fancy form of calling which returns all the VMS states that completed. The normal call function 
	 * 		  marginalizes out the execution path.
	 * @return a vector of virtual machine states
	 */	
	std::vector<VirtualMachineState_t> call_vms(const input_t x, const output_t err, ProgramLoader* loader){
					
		VirtualMachineState_t* vms = new VirtualMachineState_t(x, err);	
		push_program(vms->opstack); // write my program into vms (loader is used for everything else)

		VirtualMachinePool<VirtualMachineState_t> pool; 		
		pool.push(vms);		
		return pool.run_vms(loader);				
	}
	
	
	/**
	 * @brief Call, assuming no stochastics, return the virtual machine instead of the output
	 * @param x
	 * @param err
	 * @param loader
	 * @return 
	 */
	VirtualMachineState_t callOne_vms(const input_t x, const output_t err, ProgramLoader* loader) {
		VirtualMachineState_t vms(x, err);		
		push_program(vms.opstack); // write my program into vms (loader is used for everything else)
		vms.run(loader); // default to using "this" as the loader		
		return vms;
	}
	VirtualMachineState_t callOne_vms(const input_t x, const output_t err=output_t{}) {
		return callOne_vms(x,err,this);
	}
	
};