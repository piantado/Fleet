#ifndef PLAYOUTS_H
#define PLAYOUTS_H

/* These are default playout types for MCTS */

double structural_playouts(const MyHypothesis* h0) {
	// This does a "default" playout for MCTS that runs MCMC only if the structure (expression)
	// is incomplete, as determined by can_evaluate()
	if(h0->complete()) { // it has no gaps, so we don't need to do any structural search or anything
		auto h = h0->copy();
		h->compute_posterior(data);
		callback(h);
		double v = h->posterior;
		delete h;
		return v;
	}
	else { // we have to do some more search
		auto h = h0->copy_and_complete();
		auto q = MCMC(h, data, callback, mcmc_steps, mcmc_restart, true);
		double v = q->posterior;
		//std::cerr << v << "\t" << h0->string() << std::endl;
		delete q;
		return v;
	}
}


#endif