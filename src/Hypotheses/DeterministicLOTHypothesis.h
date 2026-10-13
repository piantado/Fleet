#pragma once 

#include "LOTHypothesis.h"

template<typename this_t, 
		 typename _input_t, 
		 typename _output_t, 
		 typename _Grammar_t,
		 _Grammar_t* grammar,
		 typename _datum_t=defaultdatum_t<_input_t, _output_t>, 
		 typename _data_t=std::span<_datum_t>,
		 typename _VirtualMachineState_t=typename _Grammar_t::VirtualMachineState_t
		 >
class DeterministicLOTHypothesis : public LOTHypothesis<this_t, _input_t, _output_t, _Grammar_t, grammar, _datum_t, _data_t, _VirtualMachineState_t> { 
public:

	using Super = LOTHypothesis<this_t, _input_t, _output_t, _Grammar_t, grammar, _datum_t, _data_t, _VirtualMachineState_t>;
	using Super::Super; 
	
	using input_t   = Super::input_t;
	using output_t  = Super::output_t;
	using ginput_t   = typename _Grammar_t::input_t;
	using goutput_t  = typename _Grammar_t::output_t;
	
	using call_output_t = output_t;
	using VirtualMachineState_t = Super::VirtualMachineState_t;
	
	// Compute the output of this function. Must be defined in subclasses. 
	virtual output_t compute_output(const input_t& in) {
		
		if constexpr(std::is_same_v<input_t, ginput_t> && 
					 std::is_same_v<output_t, goutput_t>) {
						 
			// defaultly, out output is just a call
			// NOTE: we might want to change this if e.g. our grammar type is different than
			// our output type (as in FOL)
			return this->call(in);
		 }
		 else {
			 assert(false && "*** You must overwrite compute_output if input!=ginput or output!=goutput");
		 }
	}
	
	/**
	 * @brief A variant of call that assumes no stochasticity and therefore outputs only a single value. 
	 * 		  (This uses a nullptr virtual machine pool, so will throw an error on flip)
	 * @param x
	 * @param err
	 * @return 
	 */
	virtual goutput_t call(const ginput_t x, const goutput_t& err=goutput_t{}) {
		if constexpr (std::is_same<typename VirtualMachineState_t::input_t, ginput_t>::value and 
					  std::is_same<typename VirtualMachineState_t::output_t, goutput_t>::value) {
			
			assert(not this->program.empty());
			
			// we can use this if we are guaranteed that we don't have a stochastic Hypothesis
			// the savings is that we don't have to create a VirtualMachinePool	or track distributions
			VirtualMachineState_t vms(x, err, nullptr);		

			vms.program = this->program; // write my program into vms (program->loader is used for everything else)
			
			// save that we've been called
			this->was_called = true; 
			
			const auto out = vms.run(); 	
			this->total_instruction_count_last_call = vms.runtime_counter.total;
			this->total_vms_steps = 1;
			
			return out;
			
		} else {
			print(typeid(ginput_t).name(), typeid(goutput_t).name(), typeid(typename VirtualMachineState_t::input_t).name(), typeid(typename VirtualMachineState_t::output_t).name()); 
			UNUSED(x); UNUSED(err); 
			assert(false && "*** Cannot use call when VirtualMachineState_t has different input_t or output_t."); 
		}
	}
	
};
