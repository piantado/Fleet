// A version of the number game
enum CustomOp {
	op_One,op_Plus,op_Minus,op_Times,op_Divide,op_Power,op_Neg,
	op_Prime,op_Constant, op_Range
};

const double reliability = 0.99;
const int N = 100; // what number do we go up to?

// We represnet as floats so we can use nan
#define NT_TYPES float
#define NT_NAMES nt_float

#include "Fleet.h" 

class MyGrammar : public Grammar { 
public:
	MyGrammar() : Grammar() {
		add( Rule(nt_float, BuiltinOp::op_X,   "x",          {},                            5.0) );		
		add( Rule(nt_float, op_One,            "1",          {},                            5.0) );		

		for(int i=0;i<=N;i++) {
			if(i==1) continue; // defined in previous rule
			add( Rule(nt_float, op_Constant, str(i),         {},                            10.0/N, i) );		
		}

		add( Rule(nt_float, op_Plus,         "(%s+%s)",   	  {nt_float,nt_float},            1.0) );
		add( Rule(nt_float, op_Minus,        "(%s-%s)",   	  {nt_float,nt_float},            1.0) );
		add( Rule(nt_float, op_Neg,          "(-%s)",   	      {nt_float},                   1.0) );
		add( Rule(nt_float, op_Times,        "(%s*%s)",   	  {nt_float,nt_float},            1.0) );
		add( Rule(nt_float, op_Divide,       "(%s/%s)",   	  {nt_float,nt_float},            1.0) );
		add( Rule(nt_float, op_Power,        "(%s^%s)",   	  {nt_float,nt_float},            1.0) );
		
		// We'll implement ranges here by taking the interesction of some function of x
		// with an upper and lower bound -- e.g. range(x,45,55) is all numbers between 45 and 55
		add( Rule(nt_float, op_Range,        "range(%s,%s,%s)",   	  {nt_float,nt_float,nt_float},            1.0) );
	}
};


class MyHypothesis : public LOTHypothesis<MyHypothesis,Node,nt_float,float,float,     float, std::multiset<float> > {
public:
	using Super = LOTHypothesis<MyHypothesis,Node,nt_float,float,float,               float, std::multiset<float>>;
	
	// I must implement all of these constructors
	MyHypothesis()                      : Super() {} 
	MyHypothesis(Grammar* g)            : Super(g)   {}		// who knows what the constants should do
	MyHypothesis(Grammar* g, Node v)    : Super(g,v) {}

	virtual double compute_prior() {
		if(value.count() > 25) { return prior = -infinity; }
		else {
			return prior = Super::compute_prior();
		} 
	}

	virtual double compute_likelihood(const t_data& data, const double breakout=-infinity) {
		// TODO: This uses 0 for NAN -- probably not the best...
		
		std::set<float> s;
		float fx;
		for(int i=0;i<=N and !CTRL_C;i++) {
			fx = this->callOne(i, 0);
			
			if(fx >=0 and fx <= N and !std::isnan(fx)) { // important to check bounds for size principle
				s.insert(fx);
			}
		}
		
		double sz = s.size(); // size principle likelihood
		
		// now go through and compute the likelihood
		likelihood = 0.0;
		for(auto& d : data) {
			bool b = (s.find(d)!=s.end()); // does it contain?
			likelihood += log(  (b ? reliability / sz : 0.0) + (1.0-reliability)/N);
		}
		
		return likelihood;
	}
	
	abovmstatus_tspatch_rule(Instruction i, VirtualMachinePool<float,float>* pool, VirtualMachineState<float,float>& vms, Dispatchable<float, float>* loader ) {
		switch(i.getCustom()) {
			CASE_FUNC0(op_One,         float,           [](){ return 1.0;} )
			CASE_FUNC1(op_Neg,         float, float,      [](const float x){ return -x;} )
			CASE_FUNC2(op_Plus,        float, float, float, [](const float x, const float y){ return x+y; })
			CASE_FUNC2(op_Minus,       float, float, float, [](const float x, const float y){ return x-y; })
			CASE_FUNC2(op_Times,       float, float, float, [](const float x, const float y){ return x*y; })
			CASE_FUNC2(op_Divide,      float, float, float, [](const float x, const float y){ return (y==0 ? 0 : x/y); }) // may raise arithmeticExcpetion below
			CASE_FUNC0(op_Constant,    float, [i](){ return i.arg; })
			CASE_FUNC2(op_Power,       float, float, float, [](const float x, const float y){ return pow(x,y); })	
			CASE_FUNC3(op_Range,       float, float, float, float, [](const float x, const float m, const float M){ return (x >= m and x <= M ? x : NaN); })
			
			default: assert(0); // should never get here
		}
		return vmstatus_t::GOOD;
	}
	
	virtual bool operator==(const MyHypothesis& h) const {
		return value == h.value;
	}
	
};

////////////////////////////////////////////////////////////////////////////////////////////

MyGrammar grammar;
TopN<MyHypothesis> top;

////////////////////////////////////////////////////////////////////////////////////////////


int main(int argc, char** argv){ 
	signal(SIGINT, fleet_interrupt_handler);
	using namespace std;
	COUT "# Starting " ENDL;
	
	// default include to process a bunch of global variables: mcts_steps, mcc_steps, etc
	auto app = Fleet::DefaultArguments("Number game");
	CLI11_PARSE(app, argc, argv);
	Fleet_initialize();

	top.set_size(ntop); // set by above macro
	

	MyHypothesis::t_data mydata = {2,8,16,2,8,16};	

	MyHypothesis h0(&grammar);
	h0 = h0.restart();
	
	MCMCChain samp(h0, &mydata, top);
	tic();
	samp.run(mcmc_steps, runtime); //30000);		
	tic();

	
	top.print();
	
	
	COUT "# Global sample count:" TAB FleetStatistics::global_sample_count ENDL;
//	COUT "# MCTS tree size:" TAB m.size() ENDL;	
	COUT "# Elapsed time:" TAB elapsed_seconds() << " seconds " ENDL;
	COUT "# Samples per second:" TAB FleetStatistics::global_sample_count/elapsed_seconds() ENDL;
	COUT "# VM ops per second:" TAB FleetStatistics::vm_ops/elapsed_seconds() ENDL;
//	COUT "# MCTS steps per second:" TAB m.statistics.N/elapsed_seconds() ENDL;	
}
