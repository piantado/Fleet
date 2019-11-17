/* 
 * In this version, input specifies the prdata minus the txt 
 * 	--input=data/NewportAslin
 * and then we'll add in the data amounts, so that now each call will loop over amounts of data
 * and preserve the top hypotheses
 * 
 * 
 * Some notes -- 
				What if we add something that lets you access the previous character generated? cons(x,y) where y gets acess to x? cons(x,F(x))?
  * 					Not so easy to see exactly what it is, ithas to be a Fcons function where Fcons(x,i) = cons(x,Fi(x))
  * 					It's a lot like a lambda -- apply(lambda x: cons(x, Y[x]), Z)
  * 
  * */
  
#include <set>
#include <string>
#include <vector>

#include "Data.h"

using S = std::string;
using StrSet = std::set<S>;

const std::string my_default_input = "data/SimpleEnglish"; 
S alphabet="nvadt";
size_t max_length = 256; // max string length, else throw an error (128+ needed for count)
size_t max_setsize = 64; // throw error if we have more than this
size_t nfactors = 2; // how may factors do we run on?
static const double alpha = 0.95; // reliability of the data

const size_t PREC_REC_N   = 25;  // if we make this too high, then the data is finite so we won't see some stuff
const size_t MAX_LINES    = 1000000; // how many lines of data do we load? The more data, the slower...
const size_t MAX_PR_LINES = 1000000; 

std::vector<S> data_amounts={"1", "2", "5", "10", "50", "100", "500", "1000", "10000", "50000", "100000"}; // how many data points do we run on?

// Parameters for running a virtual machine
const double MIN_LP = -25.0; // -10 corresponds to 1/10000 approximately, but we go to -25 to catch some less frequent things that happen by chance
const unsigned long MAX_STEPS_PER_FACTOR   = 4096; //2048; //2048;
const unsigned long MAX_OUTPUTS_PER_FACTOR = 512; // 256; // 512; //256;

///~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
/// These define all of the types that are used in the grammar.
/// This macro must be defined before we import Fleet.
///~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

#define FLEET_GRAMMAR_TYPES S,bool,double,StrSet

#define CUSTOM_OPS op_UniformSample,op_P

///~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
/// This is a global variable that provides a convenient way to wrap our primitives
/// where we can pair up a function with a name, and pass that as a constructor
/// to the grammar. We need a tuple here because Primitive has a bunch of template
/// types to handle thee function it has, so each is actually a different type.
///~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

#include "Primitives.h"
#include "Builtins.h"

std::tuple PRIMITIVES = {
	Primitive("tail(%s)",      +[](S& s)     -> void       { if(s.length()>0) s.erase(0); }), //sreturn (s.empty() ? S("") : s.substr(1,S::npos)); }), // REPLACE: if(s.length() >0) s.erase(0)
	Primitive("head(%s)",      +[](S s)      -> S          { return (s.empty() ? S("") : S(1,s.at(0))); }), // MAYBE REPLACE:  S(1,vms.stack<S>().topref().at(0));
	Primitive("pair(%s,%s)",   +[](S& a, S b) -> void      { 
			if(a.length() + b.length() > max_length) throw VMSRuntimeError;
			a = a+b; // modify on stack
	}), // also add a function to check length to throw an error if its getting too long
	Primitive("\u00D8",        +[]()         -> S          { return S(""); }),
	Primitive("(%s==%s)",      +[](S x, S y) -> bool       { return x==y; }),
	Primitive("empty(%s)",     +[](S x) -> bool            { return x.length()==0; }),
	Primitive("(%s==%s)",      +[](S x, S y) -> bool       { return x==y; }),
	
	Primitive("insert(%s,%s)", +[](S x, S y) -> S { 
				
				size_t l = x.length();
				if(l == 0) 
					return y;
				else if(l + y.length() > max_length) 
					throw VMSRuntimeError;
				else {
					// put y into the middle of x
					size_t pos = l/2;
					S out = x.substr(0, pos); 
					out.append(y);
					out.append(x.substr(pos));
					return out;
				}				
			}),
	
	// set operations:
	Primitive("%s",         +[](S x) -> StrSet          { StrSet s; s.insert(x); return s; } ),
	Primitive("%s,%s",      +[](StrSet& s, S x) -> void { if(s.size() > max_setsize) throw VMSRuntimeError; else s.insert(x); } ),
	Primitive("%s\u2216%s", +[](StrSet& s, S x) -> void { s.erase(x); } ),
	
	// And add built-ins:
	Builtin::If<S>("if(%s,%s,%s)", 1.0),		
	Builtin::X<S>("x"),
	Builtin::Flip("flip()"),
	Builtin::SafeRecurse<S,S>("F(%s)")	
};

#include "Fleet.h" 

class InnerHypothesis;
class InnerHypothesis : public  LOTHypothesis<InnerHypothesis,Node,S,S> {
public:
	using Super = LOTHypothesis<InnerHypothesis,Node,S,S>;
	using Super::Super; // inherit constructors
	
	virtual vmstatus_t dispatch_custom(Instruction i, VirtualMachinePool<S,S>* pool, VirtualMachineState<S,S>* vms, Dispatchable<S,S>* loader) {
		assert(i.is<CustomOp>());
		switch(i.as<CustomOp>()) {
			case CustomOp::op_P: {
				vms->template push<double>( double(i.arg)/10.0 );
				break;
			}			
			case CustomOp::op_UniformSample: {
					// implement sampling from the set.
					// to do this, we read the set and then push all the alternatives onto the stack
					StrSet s = vms->template getpop<StrSet>();
					
					// now just push on each, along with their probability
					// which is here decided to be uniform.
					const double lp = (s.empty()?-infinity:-log(s.size()));
					for(const auto& x : s) {
						pool->copy_increment_push(vms,x,lp);
					}

					// TODO: we can make this faster, like in flip, by following one of the paths?
					return vmstatus_t::RANDOM_CHOICE; // if we don't continue with this context 
			}
			default: {assert(0 && "Should not get here!");}
		}
		return vmstatus_t::GOOD;
	}
	
	
};


class MyHypothesis : public Lexicon<MyHypothesis, InnerHypothesis, S, S> {
public:	
	
	using Super = Lexicon<MyHypothesis, InnerHypothesis, S, S>;
	
	MyHypothesis()                       : Super()   {}
	MyHypothesis(const MyHypothesis& h)  : Super(h)  {}


	bool check_reachable() const {
		// checks if the last factor calls all the earlier (else we're "wasting" factors)
		// We do this by making the graph of what factors calls which other, and then
		// computing the transitive closure
		
		const size_t N = factors.size();
		assert(N > 0); 
		
		// is calls[i][j] stores whether factor i calls factor j
		bool calls[N][N]; 
		
		// everyone calls themselves, zero the rest
		for(size_t i=0;i<N;i++) {
			for(size_t j=0;j<N;j++){
				calls[i][j] = (i==j);
			}
		}
		
		for(size_t i=0;i<N;i++){
			
			for(const auto& n : factors[i].value) {
				if(n.rule->instr.is_a(BuiltinOp::op_RECURSE,
									  BuiltinOp::op_MEM_RECURSE,
									  BuiltinOp::op_SAFE_RECURSE,
									  BuiltinOp::op_SAFE_MEM_RECURSE)) {
					calls[i][n.rule->instr.arg] = true;
				}
			}
		}
		
		// now we take the transitive closure to see if calls[N-1] calls everything (eventually)
		// otherwise it has probability of zero
		// TOOD: This could probably be lazier because we really only need to check reachability
		for(size_t a=0;a<N;a++) {	
		for(size_t b=0;b<N;b++) {
		for(size_t c=0;c<N;c++) {
			calls[b][c] = calls[b][c] or (calls[b][a] and calls[a][c]);		
		}
		}
		}

		// don't do anything if we have uncalled functions from the root
		for(size_t i=0;i<N;i++) {
			if(not calls[N-1][i]) {
				return false;
			}
		}		
		return true;		
	}

	virtual double compute_prior() {
		// since we aren't searching over nodes, we are going to enforce a prior that requires
		// each function to be called -- this should make the search a bit more efficient by 
		// allowing us to prune out the functions which could be found with a smaller number of factors
		
		if(not check_reachable()) {
			return prior = -infinity;
		}
		else {
			return prior = Lexicon<MyHypothesis,InnerHypothesis,S,S>::compute_prior();
		}
	}
	


	/********************************************************
	 * Calling
	 ********************************************************/
	 
	virtual DiscreteDistribution<S> call(const S x, const S err) {
		// this calls by calling only the last factor, which, according to our prior,
		// must call everything else
		
		if(!has_valid_indices()) return DiscreteDistribution<S>();
		
		size_t i = factors.size()-1; 
		return factors[i].call(x,err,this,MAX_STEPS_PER_FACTOR,MAX_OUTPUTS_PER_FACTOR,MIN_LP); 
	}
	
	 
	 
	 // We assume input,output with reliability as the number of counts that input was seen going to that output
	 virtual double compute_single_likelihood(const t_datum& datum) { assert(0); }
	 

	 double compute_likelihood(const t_data& data, const double breakout=-infinity) {
		// this version goes through and computes the predictive probability of each prefix
		 
		// call -- treats all input as emptystrings
		const auto M = call(S(""), S("<err>")); 
		
		likelihood = 0.0;
		const double lpnoise = log((1.0-alpha)/alphabet.size());
		const double lpend   = log(alpha); // end a string with this probability
		for(const auto& a : data) {
			S astr = a.output;
			double alp = -infinity; // the model's probability of this
			for(const auto& m : M.values()) {
				const S mstr = m.first;
				if(is_prefix(mstr, astr)) {
					alp = logplusexp(alp, m.second + lpnoise * (astr.size() - mstr.size()) + lpend);
				}
			}
			likelihood += alp * a.reliability; 
			
			if(likelihood == -infinity) {
				return likelihood;
			}
			if(likelihood < breakout) {	
				likelihood = -infinity;
				break;				
			}
		}
		return likelihood; 
	
	 }
	 
	 void print(std::string prefix="") {
		std::lock_guard guard(Fleet::output_lock); // better not call Super wtih this here
		extern MyHypothesis::t_data prdata;
		extern std::string current_data;
		auto o = this->call(S(""), S("<err>"));
		auto [prec, rec] = get_precision_and_recall(std::cout, o, prdata, PREC_REC_N);
		COUT "#\n";
		COUT "# "; o.print();	COUT "\n";
		COUT prefix << current_data TAB this->born TAB this->posterior TAB this->prior TAB this->likelihood TAB QQ(this->parseable()) TAB prec TAB rec;
		COUT "" TAB QQ(this->string()) ENDL
	}
	 
};


std::string prdata_path = ""; 
MyHypothesis::t_data prdata; // used for computing precision and recall -- in case we want to use more strings?
S current_data = "";


////////////////////////////////////////////////////////////////////////////////////////////
// Main
////////////////////////////////////////////////////////////////////////////////////////////

int main(int argc, char** argv){ 
	
	input_path = my_default_input; // set this so it's not fleet's normal input default
	
	// default include to process a bunch of global variables: mcts_steps, mcc_steps, etc
	auto app = Fleet::DefaultArguments("Formal language learner");
	app.add_option("-N,--nfactors",      nfactors, "How many factors do we run on?");
	app.add_option("-L,--maxlength",     max_length, "Max allowed string length");
	app.add_option("-A,--alphabet",  alphabet, "The alphabet of characters to use");
	app.add_option("-P,--prdata",  prdata_path, "What data do we use to compute precion/recall?");
	CLI11_PARSE(app, argc, argv);

	Fleet_initialize();
	COUT "# Using alphabet=" << alphabet ENDL;
	
	
	// Input here is going to specify the PRdata path, minus the txt
	
	if(prdata_path == "") {	prdata_path = input_path+".txt"; }
	
	load_data_file(prdata, prdata_path.c_str()); // put all the data in prdata
	for(auto d : prdata) {	// Add a check for any data not in the alphabet
		for(size_t i=0;i<d.output.length();i++){
			if(alphabet.find(d.output.at(i)) == std::string::npos) {
				CERR "*** Character '" << d.output.at(i) << "' in " << d.output << " is not in the alphabet '" << alphabet << "'" ENDL;
				assert(0);
			}
		}
	}
	
	
	
	Grammar grammar(PRIMITIVES);
	
	for(size_t a=1;a<10;a++) { // pack probability into arg, out of 10
		std::string s = std::to_string(double(a)/10.0).substr(1,3); // substr just truncates lesser digits
		grammar.add<double>(CustomOp::op_P, s, (a==5?5.0:1.0), a);
	}	

	for(size_t a=0;a<nfactors;a++) {	
		auto s = std::to_string(a);
		grammar.add<S,S>(BuiltinOp::op_SAFE_RECURSE, S("F")+s+"(%s)", 1.0/nfactors, a);
		grammar.add<S,S>(BuiltinOp::op_SAFE_MEM_RECURSE, S("memF")+s+"(%s)", 1.0/nfactors, a);
	}

	// push for each
	for(size_t ai=0;ai<alphabet.length();ai++) {
		grammar.add<S>(BuiltinOp::op_ALPHABET,   Q(alphabet.substr(ai,1)),  10.0/alphabet.length(), (int)alphabet.at(ai) );
	}
	
	
	MyHypothesis h0; 
	for(size_t fi=0;fi<nfactors;fi++) {// start with the right number of factors
		InnerHypothesis f(&grammar);
		h0.factors.push_back(f.restart());
	}
		
	// we are building up data and TopNs to give t parallel tempering
	std::vector<MyHypothesis::t_data> datas; // load all the data	
	std::vector<TopN<MyHypothesis>> tops;
	for(size_t i=0;i<data_amounts.size();i++){ 
		MyHypothesis::t_data d;
		
		S data_path = input_path + "-" + data_amounts[i] + ".txt";	
		load_data_file(d, data_path.c_str());
		
		datas.push_back(d);
		tops.push_back(TopN<MyHypothesis>(ntop));
	}
	
	// Run
	ParallelTempering samp(h0, datas, tops);
	
	tic();	
	samp.run(mcmc_steps, runtime, 15.0, 60.0);	
	tic();
	
	// And finally print
	TopN<MyHypothesis> all; 
	for(auto& tn : tops) { 
		all << tn; // will occur in some weird order since they're not in all the data
	} 

	for(size_t i=0;i<data_amounts.size();i++) {
		TopN<MyHypothesis> tmptop; // just so they print in order - but note this will remove the NAs, so some later strings may have fewer!
		for(auto h: all.values()) {
			h.compute_posterior(datas[i]);
			tmptop << h;
		}
		tmptop.print(data_amounts[i]);
	}


	// Vanilla MCMC
//	for(auto da : data_amounts) {
//		S data_path = input_path + "-" + da + ".txt";
//		load_data_file(mydata, data_path.c_str());
//		tic();
//		MCMCChain chain(h0, &mydata, all);
//		chain.run(mcmc_steps, runtime);
//		tic();	
//	}
//	top.print();
//	
	
	COUT "# Global sample count:" TAB FleetStatistics::global_sample_count ENDL;
	COUT "# Elapsed time:"        TAB elapsed_seconds() << " seconds " ENDL;
	COUT "# Samples per second:"  TAB FleetStatistics::global_sample_count/elapsed_seconds() ENDL;
}
