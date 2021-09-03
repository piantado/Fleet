#pragma once

#include "VirtualMachine/VirtualMachineState.h"
#include "VirtualMachine/VirtualMachinePool.h"
#include "DiscreteDistribution.h"
#include "Program.h"

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
class Callable : public ProgramLoader<VirtualMachineState_t> {
public:
	
	// store the the total number of instructions on the last call
	// (summed up for stochastic, else just one for deterministic)
	unsigned long total_instruction_count_last_call;
	unsigned long total_vms_steps;
	
	// A Callable stores its program
	Program<VirtualMachineState_t> program;
	
	
	/**
	 * @brief Constructor for a callable 
	 */	
	Callable() : total_instruction_count_last_call(0), total_vms_steps(0) { }

	Callable(const Callable& c) {
		total_instruction_count_last_call = c.total_instruction_count_last_call;
		total_vms_steps = c.total_vms_steps;
		program = c.program; 
		if(c.program.loader == &c) 
			program.loader=this;  // something special here -- if c's loader was itself, make this myself; else leave it
	}

	Callable(const Callable&& c) {
		total_instruction_count_last_call = c.total_instruction_count_last_call;
		total_vms_steps = c.total_vms_steps;
		program = std::move(c.program); 
		if(c.program.loader == &c) 
			program.loader=this;  // something special here -- if c's loader was itself, make this myself; else leave it
	}

	Callable& operator=(const Callable& c) {
		total_instruction_count_last_call = c.total_instruction_count_last_call;
		total_vms_steps = c.total_vms_steps;
		program = c.program; 
		if(c.program.loader == &c) 
			program.loader=this;  // something special here -- if c's loader was itself, make this myself; else leave it
		return *this;
	}

	Callable& operator=(const Callable&& c) {
		total_instruction_count_last_call = c.total_instruction_count_last_call;
		total_vms_steps = c.total_vms_steps;
		program = std::move(c.program); 
		if(c.program.loader == &c) 
			program.loader=this;  // something special here -- if c's loader was itself, make this myself; else leave it
		return *this;
	}



	/**
	 * @brief Can this be evalutaed (really should be named -- can be called?). Sometimes partial hypotheses
	 * 		  can't be called.
	 */
	[[nodiscard]]  virtual bool is_evaluable() const = 0; 
		

	/**
	 * @brief Convert our tree into a program for a virtual machine, and store it in Callable::program. 
	 * @return 
	 */	
	void compile(ProgramLoader<VirtualMachineState_t>* pl=nullptr);
	

	/**
	 * @brief Run the virtual machine on input x, and marginalize over execution paths to return a distribution
	 * 		  on outputs. Note that loader must be a program loader, and that is to handle recursion and 
	 *        other function calls. 
	 * @param x - input
	 * @param err - output value on error
	 * @param loader - where to load recursive calls
	 * @return 
	 */	
	virtual DiscreteDistribution<output_t> call(const input_t x, const output_t& err=output_t{}) {
		
		// The below condition is needed in case we create e.g. a lexicon whose input_t and output_t differ from the VirtualMachineState
		// in that case, these functions are all overwritten and must be called on their own. 
		if constexpr (std::is_same<typename VirtualMachineState_t::input_t, input_t>::value and 
				      std::is_same<typename VirtualMachineState_t::output_t, output_t>::value) {
			
			assert(not program.empty());
			
			VirtualMachinePool<VirtualMachineState_t> pool; 		
			
			VirtualMachineState_t* vms = new VirtualMachineState_t(x, err, &pool);	
			
			vms->program = program; // copy our program into vms
			
			pool.push(vms); // put vms into the pool
			
			const auto out = pool.run();	
			
			// update some stats
			total_instruction_count_last_call = pool.total_instruction_count;
			total_vms_steps = pool.total_vms_steps;
			
			return out;
			
	
	  } else { UNUSED(x); UNUSED(err); assert(false && "*** Cannot use call when VirtualMachineState_t has different input_t or output_t."); }
	}
	
	virtual DiscreteDistribution<output_t>  operator()(const input_t x, const output_t& err=output_t{}){ // just fancy syntax for call
		return call(x,err);
	}

	/**
	 * @brief A variant of call that assumes no stochasticity and therefore outputs only a single value. 
	 * 		  (This uses a nullptr virtual machine pool, so will throw an error on flip)
	 * @param x
	 * @param err
	 * @return 
	 */
	virtual output_t callOne(const input_t x, const output_t& err=output_t{}) {
		if constexpr (std::is_same<typename VirtualMachineState_t::input_t, input_t>::value and 
					  std::is_same<typename VirtualMachineState_t::output_t, output_t>::value) {
			
		    assert(not program.empty());
			
			// we can use this if we are guaranteed that we don't have a stochastic Hypothesis
			// the savings is that we don't have to create a VirtualMachinePool		
			VirtualMachineState_t vms(x, err, nullptr);		

			vms.program = program; // write my program into vms (program->loader is used for everything else)
			
			const auto out = vms.run(); 	
			total_instruction_count_last_call = vms.runtime_counter.total;
			total_vms_steps = 1;
			
			return out;
			
		} else { UNUSED(x); UNUSED(err); assert(false && "*** Cannot use call when VirtualMachineState_t has different input_t or output_t."); }
	}

	
	/**
	 * @brief A fancy form of calling which returns all the VMS states that completed. The normal call function 
	 * 		  marginalizes out the execution path.
	 * @return a vector of virtual machine states
	 */	
	std::vector<VirtualMachineState_t> call_vms(const input_t x, const output_t& err=output_t{}){
		if constexpr (std::is_same<typename VirtualMachineState_t::input_t, input_t>::value and 
					  std::is_same<typename VirtualMachineState_t::output_t, output_t>::value) {
			assert(not program.empty());
				
			VirtualMachinePool<VirtualMachineState_t> pool; 		
			VirtualMachineState_t* vms = new VirtualMachineState_t(x, err, &pool);	
			vms->program = program; 
			pool.push(vms);		
			
			const auto out = pool.run_vms();					
			total_instruction_count_last_call = pool.total_instruction_count;
			total_vms_steps = pool.total_vms_steps;
			
			return out;
			
		} else { UNUSED(x); UNUSED(err); assert(false && "*** Cannot use call when VirtualMachineState_t has different input_t or output_t."); }		
	}
	
	
	/**
	 * @brief Call, assuming no stochastics, return the virtual machine instead of the output
	 * @param x
	 * @param err
	 * @return 
	 */
	VirtualMachineState_t callOne_vms(const input_t x, const output_t& err=output_t{}) {
		if constexpr (std::is_same<typename VirtualMachineState_t::input_t, input_t>::value and 
					  std::is_same<typename VirtualMachineState_t::output_t, output_t>::value) {
			assert(not program.empty());
				
			VirtualMachineState_t vms(x, err, nullptr);
			vms.program = program; // write my program into vms (loader is used for everything else)
			vms.run(); // default to using "this" as the loader	

			total_instruction_count_last_call = vms.runtime_counter.total;
			total_vms_steps = 1;
	
			return vms;
			
		} else { UNUSED(x); UNUSED(err); assert(false && "*** Cannot use call when VirtualMachineState_t has different input_t or output_t."); }		
	}

	
};