/* Looks like it can still take a long time to run if we have many strings that get evaled -- can upper bound this too?
 * Problem is that number of strings might be exponential in probability (like if all are allowed)


 * 
 * Some notes -- 
 * 		-- seems like Saffran works best with levenshtein and doesn't work well with prefix
 * 		-- changing to run for a fixed amount of time seems to make a big difference in multithreading, otherwise lingering thereads just took forever
 * 		-- Now we load only the top 100 data lines (otherwise we are much slower on some langauges)
 * 		-- Ahha most differences between languages seem to be in how LONG we have run them for --- which is afunction of how much data we have for them. 
 * 
 * 		-- TODO: Change so that we can run infernece on a smaller subset of data than we compute precision/recall on, otherwise we have to get probs exactly right (and have no ties!)
 * 		
			What if we add something that lets you access the previous character generated? cons(x,y) where y gets acess to x? cons(x,F(x))?
  * 					Not so easy to see exactly what it is, ithas to be a Fcons function where Fcons(x,i) = cons(x,Fi(x))
  * 					It's a lot like a lambda -- apply(lambda x: cons(x, Y[x]), Z)
  * 	- Make our own type to index into lexica
  * 	- 
  * 
  * 		Do compositional proposals where we might run F[i-1](F[i-2](x)) for the last one...
  * 	-
  * 	- What if we evaluate the modelwith a predictive model, where we are always computing the probability underthe model of the next *character*
  * 		Would that be faster?
  * 
  * 	-- might ber good to always give it a _ character that it can pass around as a non-emptystring for recursion
  * *   -- what if you pass around an alphabet of internal signals, and have a dictionary mapping singals to strings
  * 
  * 	-- Seems like it could be important to have access to prior parts of the string that have been emitted?
  * 
  * 
  * 	- Conert sets to sets of terminals only
  * 
  * 	-- Add checkpointing -- save file every N samples
  * 	- Inverse inlining is going to help a lot with the FSM representations because "states" will correspond to functions, and we can pull some out. 
  * 
  * 	- maybe we can add those "smart proposal" rules as complex grammar productions -- extracted and parsed from what we specify?
  * 
  * 
  * 	Change so that prec/recall is defined against the biggest
  * 
  * */
  
// define some convenient types
#include <set>
#include <string>
using S = std::string;
using StrSet = std::set<S>;

enum class CustomOp { // NOTE: The type here MUST match the width in bitfield or else gcc complains
	op_CUSTOM_NOP,op_STREQ,op_EMPTYSTRING,op_EMPTY,\
	op_CDR,op_CAR,op_CONS,\
	op_P, op_TERMINAL,\
	op_String2Set,op_Setcons,op_UniformSample,
	op_Signal, op_SignalSwitch
};


#define NT_TYPES bool,    double,    std::string,  StrSet
#define NT_NAMES nt_bool, nt_double, nt_string,    nt_set

// Includes critical files. Also defines some variables (mcts_steps, explore, etc.) that get processed from argv 
#include "Fleet.h" 

size_t maxlength = 128; // max string length, else throw an error (128 needed for count)
size_t nfactors = 2; // how may factors do we run on?
S alphabet="nvadt";
const size_t PREC_REC_N = 50;  // if we make this too high, then the data is finite so we won't see some stuff
const size_t MAX_LINES = 1000; // how many lines of data do we load? The more data, the slower...
const size_t MAX_PR_LINES = 1000000; 

// Parameters for running a virtual machine
const double MIN_LP = -20.0; // -10 corresponds to 1/10000 approximately, but we go to -15 to catch some less frequent things that happen by chance; -18;  // in (ab)^n, top 25 strings will need this man lp
const unsigned long MAX_STEPS_PER_FACTOR   = 2048; //2048;
const unsigned long MAX_OUTPUTS_PER_FACTOR = 256;

std::vector<double> data_amounts = {1, 2, 5, 10, 20, 50, 100, 200, 500, 1000, 5000, 10000}; //1.0, 2.0, 5.0, 7.5, 10.0, 25.0, 50.0, 75.0, 100.0, 125.0, 150.0, 200.0, 300, 500.0, 750, 1000.0, 2000, 3000.0, 5000.0, 10000.0, 15000, 20000};

std::vector<Node*> smart_proposals; // these are filled in in main


class MyGrammar : public Grammar { 
public:
	MyGrammar() : Grammar() {
		add( new Rule(nt_string, BuiltinOp::op_X,            "x",            {},                               10.0) );	

		for(size_t a=0;a<nfactors;a++) {	
			auto s = std::to_string(a);
			add( new Rule(nt_string, BuiltinOp::op_RECURSE,     S("F")+s+"(%s)",      {nt_string},   1.0/nfactors, a) );		
			add( new Rule(nt_string, BuiltinOp::op_MEM_RECURSE, S("memF")+s+"(%s)",   {nt_string},   1.0/nfactors, a) );		
		}
		
		// push for each
		for(size_t ai=0;ai<alphabet.length();ai++) {
			add( new Rule(nt_string, CustomOp::op_TERMINAL,   Q(alphabet.substr(ai,1)),          {},       10.0/alphabet.length(), ai) );
		}

		add( new Rule(nt_string, CustomOp::op_EMPTYSTRING,  "\u00D8",          {},                            10.0) );
		
		add( new Rule(nt_string, CustomOp::op_CONS,         "%s+%s",        {nt_string,nt_string},            1.0) );
		add( new Rule(nt_string, CustomOp::op_CAR,          "car(%s)",      {nt_string},                      1.0) );
		add( new Rule(nt_string, CustomOp::op_CDR,          "cdr(%s)",      {nt_string},                      1.0) );
		
		add( new Rule(nt_string, BuiltinOp::op_IF,           "if(%s,%s,%s)", {nt_bool, nt_string, nt_string},  1.0) );
		
		// NOTE: This rule samples from the *characters* occuring in s
		add( new Rule(nt_string, CustomOp::op_UniformSample,"{%s}",         {nt_set},                      1.0) );
		add( new Rule(nt_set,    CustomOp::op_String2Set,   "%s",           {nt_string},                   1.0) );
		add( new Rule(nt_set,    CustomOp::op_Setcons,      "%s,%s",        {nt_string, nt_set},           1.0) );
		
		add( new Rule(nt_bool,   BuiltinOp::op_FLIPP,        "flip(%s)",     {nt_double},                      1.0) );
		add( new Rule(nt_bool,   CustomOp::op_EMPTY,        "empty(%s)",    {nt_string},                      1.0) );
		add( new Rule(nt_bool,   CustomOp::op_STREQ,        "(%s==%s)",     {nt_string,nt_string},            1.0) );
		
		
		for(size_t a=1;a<10;a++) { // pack probability into arg, out of 10
			std::string s = std::to_string(double(a)/10.0).substr(1,3); // substr just truncates lesser digits
			add( new Rule(nt_double, CustomOp::op_P,      s,          {},                              (a==5?10.0:1.0), a) );
		}
		
	}
};


class InnerHypothesis;
class InnerHypothesis : public  LOTHypothesis<InnerHypothesis,Node,nt_string,S,S> {
public:

	InnerHypothesis(Grammar* g)          : LOTHypothesis<InnerHypothesis,Node,nt_string,S,S>(g)   {}
	InnerHypothesis(Grammar* g, Node* v) : LOTHypothesis<InnerHypothesis,Node,nt_string,S,S>(g,v) {}
	InnerHypothesis(Grammar* g, S c)     : LOTHypothesis<InnerHypothesis,Node,nt_string,S,S>(g,c) {}
	
	virtual abort_t dispatch_rule(Instruction i, VirtualMachinePool<S,S>* pool, VirtualMachineState<S,S>* vms, Dispatchable<S,S>* loader) {
		/* Dispatch the functions that I have defined. Returns true on success. 
		 * Note that errors might return from this 
		 * */
		switch(i.getCustom()) {
			CASE_FUNC0(CustomOp::op_EMPTYSTRING, S,          [](){ return S("");} )
			CASE_FUNC1(CustomOp::op_EMPTY,       bool,  S,   [](const S& s){ return s.size()==0;} )
			CASE_FUNC2(CustomOp::op_STREQ,       bool,  S,S, [](const S& a, const S b){return a==b;} )
			CASE_FUNC1(CustomOp::op_CDR,         S, S,       [](const S& s){ return (s.empty() ? S("") : s.substr(1,S::npos)); } )		
			CASE_FUNC1(CustomOp::op_CAR,         S, S,       [](const S& s){ return (s.empty() ? S("") : S(1,s.at(0))); } )		
			CASE_FUNC2e(CustomOp::op_CONS,       S, S,S,
								[](const S& x, const S& y){ S a = x; a.append(y); return a; },
								[](const S& x, const S& y){ return (x.length()+y.length()<maxlength ? abort_t::NO_ABORT : abort_t::SIZE_EXCEPTION ); }
								)
			CASE_FUNC0(CustomOp::op_TERMINAL,       S,     [i](){ return alphabet.substr(i.arg, 1);} ) // arg stores the index argument
			
			CASE_FUNC0(CustomOp::op_P,             double,           [i](){ return double(i.arg)/10.0;} )
			
			CASE_FUNC1(CustomOp::op_String2Set,    StrSet, S, [](const S& x){ StrSet s; s.insert(x); return s; }  )

			CASE_FUNC2(CustomOp::op_Setcons,       StrSet, S, StrSet, [](S& x, StrSet& y){ y.insert(x); return y; }  )
			
			case CustomOp::op_UniformSample: {
					// implement sampling from the set.
					// to do this, we read the set and then push all the alternatives onto the stack
					StrSet s = vms->getpop<StrSet>();
					
					// now just push on each, along with their probability
					// which is here decided to be uniform.
					const double lp = (s.empty()?-infinity:-log(s.size()));
					for(auto x : s) {
						pool->copy_increment_push(vms,x,lp);
					}
					
					// TODO: we can make this faster, like in flip, by following one of the paths?
					return abort_t::RANDOM_CHOICE; // if we don't continue with this context 
			}
			default: {
				assert(0 && "Should not get here!");
			}
		}
		return abort_t::NO_ABORT;
	}
	
	
};



class MyHypothesis : public Lexicon<MyHypothesis, InnerHypothesis, S, S> {
public:	
	static constexpr double alpha = 0.99;
//	static constexpr size_t maxnodes = 100; // toss anything with more than this many nodes total
	
	double myndata = NaN; // store this in compute likelihood so we can print it later
	
	MyHypothesis(Grammar* g) : Lexicon<MyHypothesis,InnerHypothesis,S,S>(g) {}
	
	MyHypothesis(const MyHypothesis& h) : Lexicon<MyHypothesis,InnerHypothesis,S,S>(h.grammar) {
		for(auto v : h.factors) {
			factors.push_back(v->copy());
		}
		this->prior      = h.prior;
		this->likelihood = h.likelihood;
		this->posterior  = h.posterior;
		this->myndata    = h.myndata;
	}
	
	virtual ~MyHypothesis() {
		// do nothing, base class destructor called automatically
	}

	virtual double compute_prior() {
		// since we aren't searching over nodes, we are going to enforce a prior that requires
		// each function to be called -- this should make the search a bit more efficient by 
		// allowing us to prune out the functions which could be found with a smaller number of factors
		
		size_t N = factors.size();
		if(N==0) return prior = -infinity;
		
		bool calls[N * N]; // is calls[i][j] stores whether factor i calls factor j
		
		for(size_t i=0;i<N;i++) {
			for(size_t j=0;j<N;j++){
				calls[i*N+j] = (i==j);
			}
		}
		
		for(size_t i=0;i<N;i++){

			std::function<void(Node*)> myfun = [&](Node* n) {
				if(n->rule->instr.is_a(BuiltinOp::op_RECURSE,BuiltinOp::op_MEM_RECURSE)) {
					calls[i*N + n->rule->instr.arg] = true;
				}
			};

			factors[i]->value->map(myfun);
		}
		
		// now we take the transitive closure to see if calls[N-1] calls everything (eventually)
		// otherwise it has probability of zero
		for(size_t a=0;a<N;a++) {	
			for(size_t b=0;b<N;b++) {
				for(size_t c=0;c<N;c++) {
					calls[b*N + c] = calls[b*N+c] || (calls[b*N+a] && calls[a*N+c]);		
				}
			}
		}

		// don't do anything if we have uncalled functions
		for(size_t i=0;i<N;i++) {
			if(!calls[(N-1)*N+i]) {
				return prior = -infinity;
			}
		}
		
		// else default call 
		return prior = Lexicon<MyHypothesis,InnerHypothesis,S,S>::compute_prior();
	}
	


	/********************************************************
	 * Calling
	 ********************************************************/
	 
	// Define our own call without factors -- this will call each successively
	// and pass the output to the next
	DiscreteDistribution<S> call(const S x, const S err) {
		
		DiscreteDistribution<S> cur;
		if(!has_valid_indices()) return cur;
		
		size_t i = factors.size()-1; 
		return factors[i]->call(x,err,this,MAX_STEPS_PER_FACTOR,MAX_OUTPUTS_PER_FACTOR,MIN_LP); 
	}
	 
	 
	 // We assume input,output with reliability as the number of counts that input was seen going to that output
	 double compute_single_likelihood(const t_datum& datum) { assert(0); }
	 

	 double compute_likelihood(const t_data& data) {
		 // this versino goes through and computes the predictive probability of each prefix
		 likelihood = 0.0;
		 myndata = 0;
		 
		 // call -- treats all input as emptystrings
		 auto M = call(S(""),S("<err>")); 

		 // first pre-compute a prefix tree
		 // TODO: This might be faster to store in an actual tree
		 std::map<S,double> prefix_tree;
		 for(auto m : M.values()){
			 S mstr = m.first + "!"; // add a stop symbol
			 for(size_t i=0;i<=mstr.length();i++){
				 S pfx = mstr.substr(0,i);
				 if(prefix_tree.count(pfx) == 0) {
					 prefix_tree[pfx] = m.second;
				 } else {
					 prefix_tree[pfx] = logplusexp(prefix_tree[pfx], m.second);
				 }
			 }			 
		 }
		 
		 // pulling these out seems to make things go faster
		 const double lcor = log(alpha);
		 const double lerr = log((1.0-alpha)/alphabet.size());
		 
		 // now go through and compute a character-by-character predictive likelihood
		 for(auto a : data) {
			 S astr = a.output + "!"; // add a stop symbol
			 for(size_t i=0;i<=astr.length();i++) {
				S pfx = astr.substr(0,i);
				S prd = astr.substr(i,1); // get the next character we predict
				
				// get conditoinal probability
				if(prefix_tree.count(pfx+prd)) { // conditional prob with outlier likelihood
					likelihood += a.reliability * (lcor + prefix_tree[pfx+prd] - prefix_tree[pfx]); 
				} else {
					likelihood += a.reliability * lerr * (astr.length() - i); // we don't have to keep going -- we will have this many errors
					break;
				}	
			 }
			 myndata += a.reliability;
		 }
		 
		 return likelihood;		 
	 }
	 
/*
	 double compute_likelihood(const t_data& data) {
		 likelihood = 0.0;
		 myndata = 0.0;
		 
		 auto M = call(S(""),S("<err>")); // assumes all input has empty strings
		 
		 for(auto a : data) {
			 
			 // sum up the probability of all of ways we could generate a.output 
			 double lpa = -infinity; 
			 
			 for(auto m : M.values()) {
//				 if(is_prefix(m.first, a.output)) { // we flip an alpha coin to decide how many to add, and then choose at random from those we do add
//					// TODO: May not make sense to use prefix likelihood if we're not concatenative
//					lpa = logplusexpf(lpa, m.second + log(alpha) + log((1.0-alpha)/alphabet.size())*(a.output.length()-m.first.length()));
//				 }
				 lpa = logplusexpf(lpa, m.second - (alphabet.size()) * levenshtein_distance(a.output, m.first));
			 }
			 
			 likelihood += lpa * a.reliability; // scale by the amount of data
			 myndata += a.reliability;
		 }
		 
		 return likelihood;		 
	 }
*/

};




#include "Data.h"
MyHypothesis::t_data mydata;
MyHypothesis::t_data prdata; // used for computing precision and recall -- in case we want to use more strings?

TopN<MyHypothesis> top;
TopN<MyHypothesis> all; // we build up this big hypothesis space over all data. 
MyGrammar* grammar;
MyHypothesis* h0; // used to initialize search chains in parallel search
pthread_mutex_t output_lock;
 
void print(MyHypothesis& h, std::string prefix) {
	pthread_mutex_lock(&output_lock); 
	auto o = h.call(S(""), S("<err>"));
	COUT "#\n## ";
	o.print();
	COUT "\n";
	COUT "### " << h.parseable() ENDL;
	COUT prefix << h.born TAB h.myndata TAB h.posterior TAB top.count(h) TAB h.prior TAB h.likelihood TAB h.likelihood/h.myndata TAB "";
	print_precision_and_recall(std::cout, o, prdata, PREC_REC_N);
	COUT "" TAB QQ(h.string()) ENDL
	pthread_mutex_unlock(&output_lock); 
}
void print(MyHypothesis& h) {
	print(h, std::string(""));
}

void callback(MyHypothesis* h) {
	
	// print the next max
	if(h->posterior > top.best_score())
		print(*h, std::string("#### "));
	
	top << *h; 
	
	// print out with thinning
	if(thin > 0 && FleetStatistics::global_sample_count % thin == 0) {
		print(*h);
	}
	
	// do checkpointing
	if(checkpoint > 0 && FleetStatistics::global_sample_count % checkpoint == 0) {
		all.print(print);
	}
	
}



////////////////////////////////////////////////////////////////////////////////////////////
// Main
////////////////////////////////////////////////////////////////////////////////////////////


int main(int argc, char** argv){ 

	pthread_mutex_init(&output_lock, nullptr); 
	using namespace std;
	
	// default include to process a bunch of global variables: mcts_steps, mcc_steps, etc
	FLEET_DECLARE_GLOBAL_ARGS()

	int mcts_scoring = 0;

	// and my own args
	app.add_option("-S,--mcts-scoring",  mcts_scoring, "How to score MCTS?");
	app.add_option("-N,--nfactors",      nfactors, "How many factors do we run on?");
	app.add_option("-L,--maxlength",     maxlength, "Max allowed string length");
	app.add_option("-A,--alphabet",  alphabet, "The alphabet of characters to use");
	CLI11_PARSE(app, argc, argv);

	Fleet_initialize();
	
	grammar = new MyGrammar(); // must happen after alphabet is set
	
	top.set_size(ntop); // set by above macro
	
	load_data_file(prdata, input_path.c_str(), MAX_PR_LINES); // put all the data in prdata
	//load_data_file(mydata, input_path.c_str(), MAX_LINES);
	
	// Add a check for any data not in the alphabet
	for(auto d : prdata) {
		for(size_t i=0;i<d.output.length();i++){
			if(alphabet.find(d.output.at(i)) == std::string::npos) {
				CERR "CHARACTER " << d.output.at(i) << " in " << d.output << " is not in the alphabet " << alphabet ENDL;
				assert(0);
			}
		}
	}
	tic();
	
//	h0 = new MyHypothesis(grammar);
//	h0->factors.push_back(new InnerHypothesis(grammar, S("if:flip:.5:%s+%s:if:flip:.5:'a':'b':F:0:\u00D8:\u00D8")));
//	//h0->compute_posterior(prdata);
//	print(*h0);
//	return 0;
	
	// Vanilla MCMC
//	MyHypothesis* h0 = new MyHypothesis(grammar);
//	h0->factors.push_back(new InnerHypothesis(grammar));
	//h0->factors.push_back(new InnerHypothesis(grammar));
	//h0->factors.push_back(new InnerHypothesis(grammar));
	//MCMC(h0, mydata, callback, mcmc_steps, mcmc_restart, true);
	
	//	parallel_MCMC(nthreads, h0, &mydata, callback, mcmc_steps, mcmc_restart);
//	top.print(print);

	h0 = new MyHypothesis(grammar);
	for(size_t fi=0;fi<nfactors;fi++) // start with the right number of factors
		h0->factors.push_back(new InnerHypothesis(grammar));
	
	for(auto da : data_amounts) {
		top.clear();
		
		// reload and scale the data to the right amount
		mydata.clear();
		load_data_file(mydata, input_path.c_str(), MAX_LINES);
		for(size_t i=0;i<mydata.size();i++){
			mydata[i].reliability *= da; 
		}
		h0->compute_posterior(mydata);
		
		parallel_MCMC(nthreads, h0, &mydata, callback,  mcmc_steps, mcmc_restart, true, runtime / data_amounts.size() );
		
		// start on the best for the next round of data
		if(!top.empty()) 
			h0 = top.max().copy();
		
		all << top;
		
		if(CTRL_C) break; // so we don't clear top 
	}	
	
	all.print(print);
	

	// Standard MCTS	
//	MyHypothesis* h0 = new MyHypothesis(grammar);
//	//h0->factors.push_back(new InnerHypothesis(grammar, grammar->expand_from_names<Node>("if:flip:0.5:'a':%s+%s:'a':F:0:'a'")));
//	
//	MyHypothesis::MAX_FACTORS = 3; // set the number of factors
//	MCTSNode<MyHypothesis> m(explore, h0, structural_playouts, (MCTSNode<MyHypothesis>::ScoringType)mcts_scoring );
//	parallel_MCTS(&m, mcts_steps, nthreads);
//	tic();
//	
//	if(!concise) {
//		m.print("tree.txt");
//		top.print(print);
//	}
//

	

//	
//	COUT mcts_steps TAB mcmc_steps TAB explore TAB mcmc_restart TAB mcts_scoring TAB (top.empty()?-infinity:top.max().posterior) TAB m.size() TAB global_sample_count TAB elapsed_seconds() ENDL;
//	
	//top.print(print);
//	m.print("tree.txt");
//	COUT "# MCTS tree size:"      TAB m.size() ENDL;		

	tic();
	CERR "# Global sample count:" TAB FleetStatistics::global_sample_count ENDL;
	CERR "# Elapsed time:"        TAB elapsed_seconds() << " seconds " ENDL;
	CERR "# Samples per second:"  TAB FleetStatistics::global_sample_count/elapsed_seconds() ENDL;
//	
}
