#pragma once 


/**
 * @class Searchable
 * @author steven piantadosi
 * @date 03/02/20
 * @file Searchable.h
 * @brief A class is searchable if permits us to enumerate and make its neighbors.
 * This class is used by MCTS and allows us to incrementally search a hypothesis.
 */
template<typename HYP, typename t_input, typename t_output>
class Searchable {
public:
	virtual int  neighbors() const = 0; // how many neighbors do I have?
	virtual HYP  make_neighbor(int k) const = 0; // not const since its modified
	virtual bool is_evaluable() const = 0; // can I call this hypothesis? Note it might still have neighbors (a in factorized lexica)
	// NOTE: here there is both neighbors() and can_evaluate becaucse it's possible that I can evaluate something
	// that has neghbors but also can be evaluated
	
	// TODO: Should copy_and_complete be in here? Would allow us to take a partial and score it; but that's actually handled in playouts
	//       and maybe doesn't seem like it should necessarily be pat of searchable
	//       Maybe actually the evaluation function should be in here -- evaluate_state?
};
