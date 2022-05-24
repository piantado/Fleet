#pragma once 

#include "LOTHypothesis.h"

template<typename this_t, 
		 typename _input_t, 
		 typename _output_t, 
		 typename _Grammar_t,
		 _Grammar_t* grammar,
		 typename _datum_t=defaultdatum_t<_input_t, _output_t>, 
		 typename _data_t=std::vector<_datum_t>,
		 typename _VirtualMachineState_t=typename _Grammar_t::VirtualMachineState_t
		 >
class StochasticLOTHypothesis : public LOTHypothesis<this_t, _input_t, _output_t, _Grammar_t, grammar, _datum_t, _data_t, _VirtualMachineState_t> { 
public:
	using Super = LOTHypothesis<this_t, _input_t, _output_t, _Grammar_t, grammar, _datum_t, _data_t, _VirtualMachineState_t>;
	using Super::Super; 
	
	using Grammar_t = _Grammar_t;
	using input_t   = _input_t;
	using output_t  = _output_t;
	using VirtualMachineState_t = _VirtualMachineState_t;
	
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
			assert(not this->program.empty());
		    
			VirtualMachinePool<VirtualMachineState_t> pool; 		
			
			VirtualMachineState_t* vms = new VirtualMachineState_t(x, err, &pool);	
			
			vms->program = this->program; // copy our program into vms
			
			// Ok this is a little odd -- in the original FLT we did not have this set was_called=true
			// since it was not called by another factor; in retrospect, this is odd because it means the first one called
			// is never was_called unless it was called by another factor (e.g. not counting the original function call)
			// But probably it makes sense to make anything in here set was_called=true
			this->was_called = true; 

			pool.push(vms); // put vms into the pool
			
			const auto out = pool.run();	
			
			// update some stats
			this->total_instruction_count_last_call = pool.total_instruction_count;
			this->total_vms_steps = pool.total_vms_steps;
			
			return out;
			
	  } else { UNUSED(x); UNUSED(err); assert(false && "*** Cannot use call when VirtualMachineState_t has different input_t or output_t."); }
	}
};
