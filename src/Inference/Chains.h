#ifndef PARALLEL_CHAINS_H
#define PARALLEL_CHAINS_H


template<typename HYP>
class Chain {
public:
	HYP* current; 
	unsigned long n; // how many steps have I been run?

	Chain(HYP* h0) : current(h0), n(0) {
		
	}
	
	~Chain() {
		delete current;
	}
	
	Chain(const Chain<HYP>& c) {
		current = c.current->copy();		
		n = c.n;
	}
	
	double posterior() {
		return current->posterior;
	}
	
	void run(t_data mydata, size_t n, void (*callback)(HYP*)) {
		current = MCMC(current, n, 0, 0, 0, callback, mydata, 0, true); 
	}
		
	bool operator<(const Chain& x) {
		return current->posterior < x.current->posterior;
	}
	
	bool operator==(const Chain& x) {
		return current == x->current;
	}
	
};

template<typename HYP>
class ParallelChains {
	
public:
	std::vector<Chain<HYP>> chains;
	unsigned long n; // how many steps have I run?
	
	ParallelChains() : n(0) {
		
	}
	
	void run(t_data mydata, size_t steps, size_t mcmc, void (*callback)(HYP*)) {

		for(size_t s=0;s<steps && (!stop);s++){
			
			// choose which chain
			size_t b=0, i=1;
			for(;i<chains.size();i++){
				if( (chains[i].posterior() + sqrt(log(n)/(1+chains[i].n))) > (chains[b].posterior() + sqrt(log(n)/(1+chains[b].n))) ) {
					b = i; 
				}
			}
			
			// b now stors the best by this uct score
			chains[b].run(mydata, mcmc, callback);

			n += mcmc;
		}
		
	}
	
	void add(HYP* h) {
		Chain<HYP> c(h);
		chains.push_back(c);
	}
	
	
};


#endif