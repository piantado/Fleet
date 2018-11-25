
// We require a macro to define our ops as a string BEFORE we import Fleet
// these get incorporated into the op_t type
#define MY_OPS op_STREQ,op_EMPTYSTRING,op_EMPTY,op_A,op_B,op_CDR,op_CAR,op_CONS //=7

// Define our types. Fleet should use these to create both t_nonterminal and 
// VMstack -- a tuple with these types
// NOTE: We require that these types be unique or else all hell breaks loose
// and correspondingly NT_NAMES are used to define an enum, t_nonterminal
#define NT_TYPES bool,short,std::string,     double
#define NT_NAMES nt_bool,nt_short,nt_string, nt_unused

// Includes critical files. Also defines some variables (mcts_steps, explore, etc.) that get processed from argv 
#include "Fleet.h" 

class MyGrammar : public Grammar { 
public:
	MyGrammar() : Grammar() {
		add( new Rule(nt_string, op_X,            "x",            0, {},                               5.0) );		
		add( new Rule(nt_string, op_RECURSE,      "F(%s)",        1, {nt_string},                      2.0) );		

		add( new Rule(nt_string, op_A,            "'a'",          0, {},                               2.50) );
		add( new Rule(nt_string, op_B,            "'b'",          0, {},                               2.50) );
		add( new Rule(nt_string, op_EMPTYSTRING,  "''",           0, {},                               1.0) );
		
		add( new Rule(nt_string, op_CONS,         "cons(%s,%s)",  2, {nt_string,nt_string},            1.0) );
		add( new Rule(nt_string, op_CAR,          "car(%s)",      1, {nt_string},                      1.0) );
		add( new Rule(nt_string, op_CDR,          "cdr(%s)",      1, {nt_string},                      1.0) );
		
		add( new Rule(nt_string, op_IF,           "if(%s,%s,%s)", 3, {nt_bool, nt_string, nt_string},  1.0) );
		
		add( new Rule(nt_bool,   op_FLIP,         "flip()",       0, {},                               5.0) );
		add( new Rule(nt_bool,   op_EMPTY,        "empty(%s)",    1, {nt_string},                      1.0) );
		add( new Rule(nt_bool,   op_STREQ,        "(%s==%s)",     2, {nt_string,nt_string},            1.0) );
	}
};


/* Define a class for handling my specific hypotheses and data. Everything is defaulty a PCFG prior and 
 * regeneration proposals, but I have to define a likelihood */
class MyHypothesis : public LOTHypothesis<MyHypothesis,Node,nt_string, std::string, std::string> {
public:

	static const size_t MAX_LENGTH = 32; // longest strings cons will handle
	static constexpr double gamma      = 0.99; // this coin flip says when we end 
	static const std::string err;
	static constexpr size_t maxnodes = 25;
	
	// I must implement all of these constructors
	MyHypothesis(Grammar* g)            : LOTHypothesis<MyHypothesis,Node,nt_string,std::string,std::string>(g) {}
	MyHypothesis(Grammar* g, Node* v)   : LOTHypothesis<MyHypothesis,Node,nt_string,std::string,std::string>(g,v) {}
	
	virtual double compute_prior() {
		size_t cnt = value->count();
		
		if(cnt > maxnodes) prior = -infinity;
		else  			   prior = LOTHypothesis<MyHypothesis,Node,nt_string, std::string, std::string>::compute_prior();
		
		return prior;
	}
	
	double compute_single_likelihood(const t_datum& x) {
		auto out = call(x.input, err);
		//print(out);
		//COUT x.input TAB "->" TAB x.output << "\t";
		return logplusexp( log(x.reliability)+(out.count(x.output)?out[x.output]:-infinity),  // probability of generating the output
					       log(1.0-x.reliability) + (x.output.length())*(log(1.0-gamma) + log(0.5)) + log(gamma) // probability under noise; 0.5 comes from alphabet size
						   );
	}
	
	t_abort dispatch_rule(op_t op, VirtualMachinePool<std::string,std::string>* pool, VirtualMachineState<std::string,std::string>* vms ) {
		/* Dispatch the functions that I have defined. Returns true on success. 
		 * Note that errors might return from this 
		 * */
		using S = std::string;
		switch(op) {
			CASE_FUNC0(op_A,           S,          [](){ return S("a");} )
			CASE_FUNC0(op_B,           S,          [](){ return S("b");} )
			CASE_FUNC0(op_EMPTYSTRING, S,          [](){ return S("");} )
			CASE_FUNC1(op_EMPTY,       bool,  S,   [](const S s){ return s.size()==0;} )
			CASE_FUNC2(op_STREQ,       bool,  S,S, [](const S a, const S b){return a==b;} )
			CASE_FUNC1(op_CDR,         S, S,       [](const S s){ return (s.empty() ? S("") : s.substr(1,S::npos)); } )		
			CASE_FUNC1(op_CAR,         S, S,       [](const S s){ return (s.empty() ? S("") : S(1,s.at(0))); } )		
			CASE_FUNC2e(op_CONS,       S, S,S,
								[](const S x, const S y){ S a = x; a.append(y); return a; },
								[](const S x, const S y){ return (x.length()+y.length()<MAX_LENGTH ? NO_ABORT : SIZE_EXCEPTION ); }
								)
			default:
				assert(0); // should never get here
		}
		return NO_ABORT;
	}
};
const std::string MyHypothesis::err = "<err>";


MyHypothesis::t_data data;
TopN<MyHypothesis> top;


void print(MyHypothesis& h) {
	h.call("", "<err>").print();
	COUT "\t" << top.count(h) TAB  h.posterior TAB h.prior TAB h.likelihood TAB h.string() ENDL;
}


void callback(MyHypothesis* h) {
	top << *h; 
	
	global_sample_count++;
	
	// print out with thinning
	if(thin > 0 && global_sample_count % thin == 0) 
		print(*h);
}


double structural_playouts(const MyHypothesis* h0) {
	// This does a "default" playout for MCTS that runs MCMC only if the structure (expression)
	// is incomplete, as determined by can_evaluate()
	if(h0->is_evaluable()) { // it has no gaps, so we don't need to do any structural search or anything
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

////////////////////////////////////////////////////////////////////////////////////////////


int main(int argc, char** argv){ 
	signal(SIGINT, fleet_interrupt_handler);
	using namespace std;
	
	// default include to process a bunch of global variables: mcts_steps, mcc_steps, etc
	FLEET_DECLARE_GLOBAL_ARGS()
	CLI11_PARSE(app, argc, argv);
	top.set_size(ntop); // set by above macro
	
	MyGrammar grammar;
	
	// set up the data
	data.push_back(    MyHypothesis::t_datum({std::string(""), std::string("ab"), 0.99})    );

//	auto h0 = new MyHypothesis(&grammar, nullptr);
	
	//MCTSNode<MyHypothesis> m(explore, h0, structural_playouts );
	//m.search(mcts_steps);
//	tic();
//	parallel_MCTS(&m, mcts_steps, nthreads);
//	tic();
//	m.print();
//	COUT "# MCTS tree size:" TAB m.size() ENDL;	

	auto h0 = new MyHypothesis(&grammar);
//	parallel_MCMC(nthreads, h0, &data, callback, mcmc_steps, mcmc_restart);
	MCMC<MyHypothesis>(h0, data, callback, mcmc_steps);
		
	
	top.print(print);
	
	COUT "# Global sample count:" TAB global_sample_count ENDL;
	COUT "# Elapsed time:" TAB elapsed_seconds() << " seconds " ENDL;
	COUT "# Samples per second:" TAB global_sample_count/elapsed_seconds() ENDL;
	
}