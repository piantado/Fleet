/* 
 * In this version, input specifies the prdata minus the txt 
 * 	--input=data/NewportAslin
 * and then we'll add in the data amounts, so that now each call will loop over amounts of data
 * and preserve the top hypotheses
 * 
 * */
  
#include <set>
#include <string>
#include <vector>
#include <numeric> // for gcd

#include "Data.h"

using S = std::string;
using StrSet = std::set<S>;

const std::string my_default_input = "data/English"; 
S alphabet="nvadtp";
size_t max_length = 256; // max string length, else throw an error (128+ needed for count)
size_t max_setsize = 64; // throw error if we have more than this
size_t nfactors = 2; // how may factors do we run on? (defaultly)

static constexpr float alpha = 0.01; // probability of insert/delete errors (must be a float for the string function below)

const size_t PREC_REC_N   = 25;  // if we make this too high, then the data is finite so we won't see some stuff
const size_t MAX_LINES    = 1000000; // how many lines of data do we load? The more data, the slower...
const size_t MAX_PR_LINES = 1000000; 

const size_t RESTART = 0;
const size_t NTEMPS = 20;
unsigned long SWAP_EVERY = 1500; // ms

std::vector<S> data_amounts={"1", "2", "5", "10", "20", "50", "100", "200", "500", "1000", "2000", "5000", "10000"}; // how many data points do we run on?
//std::vector<S> data_amounts={"1","1000"}; // how many data points do we run on?

// Parameters for running a virtual machine
/// NOTE: IF YOU CHANGE, CHANGE BELOW TOO ~~~~~~~~~~~~~~~~~~~~~~~~~~~~
unsigned long MAX_STEPS_PER_FACTOR   = 4096; //4096;  
unsigned long MAX_OUTPUTS_PER_FACTOR = 1024; //512; - make it bigger than
unsigned long PRINT_STRINGS = 256; // print at most this many strings for each hypothesis
double MIN_LP = -25.0; // -10 corresponds to 1/10000 approximately, but we go to -25 to catch some less frequent things that happen by chance
/// NOTE: IF YOU CHANGE, CHANGE BELOW TOO ~~~~~~~~~~~~~~~~~~~~~~~~~~~~

///~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
/// We declare these because we use a custom primitive below, which needs them in the 
/// same order as the grammar
///~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
 
#define MY_TYPES S,bool,double,StrSet

///~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
/// This is a global variable that provides a convenient way to wrap our primitives
/// where we can pair up a function with a name, and pass that as a constructor
/// to the grammar. We need a tuple here because Primitive has a bunch of template
/// types to handle thee function it has, so each is actually a different type.
///~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

#include "Primitives.h"
#include "Builtins.h"
//
#include "VirtualMachine/VirtualMachineState.h"
#include "VirtualMachine/VirtualMachinePool.h"

std::tuple PRIMITIVES = {
	Primitive("tail(%s)",      +[](S& s)     -> void       { if(s.length()>0) s.erase(0); }), //sreturn (s.empty() ? S("") : s.substr(1,S::npos)); }), // REPLACE: if(s.length() >0) s.erase(0)
	Primitive("head(%s)",      +[](S s)      -> S          { return (s.empty() ? S("") : S(1,s.at(0))); }), // MAYBE REPLACE:  S(1,vms.stack<S>().topref().at(0));
	Primitive("pair(%s,%s)",   +[](S& a, S b) -> void      { 
			if(a.length() + b.length() > max_length) 
				throw VMSRuntimeError;
			a.append(b); // modify on stack
	}), // also add a function to check length to throw an error if its getting too long

	Primitive("\u00D8",        +[]()         -> S          { return S(""); }, 10.0), // same general prob as entire alphabet
	
	Primitive("(%s==%s)",      +[](S x, S y) -> bool       { return x==y; }),
	Primitive("empty(%s)",     +[](S x) -> bool            { return x.length()==0; }),
	

	Primitive("and(%s,%s)",    +[](bool a, bool b) -> bool { return (a and b); }), // optional specification of prior weight (default=1.0)
	Primitive("or(%s,%s)",     +[](bool a, bool b) -> bool { return (a or b); }),
	Primitive("not(%s)",       +[](bool a)         -> bool { return (not a); }),
	
	
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
			}, 0.2), // make insert dispreffered relative to pair
	
	
	// add an alphabet symbol (\Sigma)
	Primitive("\u03A3", +[]() -> StrSet {
		StrSet out; 
		for(const auto& a: alphabet) {
			out.emplace(1,a);
		}
		return out;
	}, 5.0),
	
	// set operations:
	Primitive("{%s}",         +[](S x) -> StrSet          { StrSet s; s.insert(x); return s; }, 10.0),
	Primitive("(%s\u222A%s)", +[](StrSet& s, StrSet x) -> void { 
		if(s.size() + x.size() > max_setsize) 
			throw VMSRuntimeError; 

		s.insert(x.begin(), x.end());
	}),
	
	Primitive("(%s\u2216%s)", +[](StrSet s, StrSet x) -> StrSet {
		StrSet output; 
		
		// this would usually be implemented like this, but it's overkill (and slower) because normally 
		// we just have single elemnents
		std::set_difference(s.begin(), s.end(), x.begin(), x.end(), std::inserter(output, output.begin()));
		
//		for(auto& v : s) {
//			if(not x.contains(v)) {
//				output.insert(std::move(v));
//			}
//		}
//		
		return output;
	}),	
	
	
	// NOTE: All custom ops must come before Builtins 
	
	// Define our custom op here. To do this, we simply define a primitive whose first argument is vmstatus_t&. This servers as our return value
	// since the return value of this lambda is needed by grammar to decide the nonterminal. If so, we must also take vms, pool, and loader.
	Primitive("sample(%s)", +[](StrSet s) -> S { return S{}; }, 
						    +[](VirtualMachineState<S,S,MY_TYPES>* vms, VirtualMachinePool<VirtualMachineState<S,S,MY_TYPES>>* pool, ProgramLoader* loader) -> vmstatus_t  {
		
		// This function is a bit more complex than it otherwise would be because this was taking 40% of time initially. 
		// The reason was that it was copying the entire vms stack for single elements (which are the most common sets) 
		// and so we have optimized this out. 
								
		// One useful optimization here is that sometimes that set only has one element. So first we check that, and if so we don't need to do anything 
		// also this is especially convenient because we only have one element to pop
		if(vms->template gettopref<StrSet>().size() == 1) {
			auto it = vms->template gettopref<StrSet>().begin();
			S x = *it;
			vms->push<S>(std::move(x));
			assert(++it == vms->template gettopref<StrSet>().end()); // just double-check its empty
			
			vms->template stack<StrSet>().pop();
		}
		else {
			// else there is more than one, so we have to copy the stack and increment the lp etc for each
			// NOTE: The function could just be this latter branch, but that's much slower because it copies vms
			// even for single stack elements
			
			// implement sampling from the set.
			// to do this, we read the set and then push all the alternatives onto the stack
			StrSet s = vms->template getpop<StrSet>();
			
			// now just push on each, along with their probability
			// which is here decided to be uniform.
			const double lp = (s.empty()?-infinity:-log(s.size()));
			for(const auto& x : s) {
				bool b = pool->copy_increment_push(vms,x,lp);
				if(not b) break; // we can break since all of these have the same lp -- if we don't add one, we won't add any!
			}
		}
			
		return vmstatus_t::RANDOM_CHOICE; // if we don't continue with this context		
		
	}),
	
	
	// And add built-ins:
	Builtin::If<S>("if(%s,%s,%s)", 1.0),		
	Builtin::If<StrSet>("if(%s,%s,%s)", 1.0),		
	Builtin::If<double>("if(%s,%s,%s)", 1.0),		
	Builtin::X<S>("x"),
	Builtin::FlipP("flip(%s)", 10.0)

};

///~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
/// Declare a grammar
///~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
 
#include "Grammar.h"

class MyGrammar : public Grammar<MY_TYPES> {
	using Super = Grammar<MY_TYPES>;
	using Super::Super;
};

///~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
/// Declare our hypothesis type
///~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

#include "LOTHypothesis.h"

class InnerHypothesis final : public LOTHypothesis<InnerHypothesis,S,S,MyGrammar> {
public:
	using Super = LOTHypothesis<InnerHypothesis,S,S,MyGrammar>;
	using Super::Super; // inherit constructors
};

#include "Lexicon.h"


class MyHypothesis final : public Lexicon<MyHypothesis, InnerHypothesis, S, S> {
public:	
	
	using Super = Lexicon<MyHypothesis, InnerHypothesis, S, S>;
	using Super::Super;

	virtual double compute_prior() override {
		// since we aren't searching over nodes, we are going to enforce a prior that requires
		// each function to be called -- this should make the search a bit more efficient by 
		// allowing us to prune out the functions which could be found with a smaller number of factors
		return prior = (check_reachable() ? Lexicon<MyHypothesis,InnerHypothesis,S,S>::compute_prior() : -infinity);
	}
	

	/********************************************************
	 * Calling
	 ********************************************************/
	 
	virtual DiscreteDistribution<S> call(const S x, const S err) override {
		// this calls by calling only the last factor, which, according to our prior,
		// must call everything else
		
		if(!has_valid_indices()) return DiscreteDistribution<S>();
		
		size_t i = factors.size()-1; 
		return factors[i].call(x, err, this, MAX_STEPS_PER_FACTOR, MAX_OUTPUTS_PER_FACTOR, MIN_LP); 
	}
	 
	 // We assume input,output with reliability as the number of counts that input was seen going to that output
	 virtual double compute_single_likelihood(const datum_t& datum) override { assert(0); }
	 

	 double compute_likelihood(const data_t& data, const double breakout=-infinity) override {
		// this version goes through and computes the predictive probability of each prefix
		 
		const auto& M = call(S(""), S("<err>")); 
		
		likelihood = 0.0;
		
		const float log_A = log(alphabet.size());

		for(const auto& a : data) {
			double alp = -infinity; // the model's probability of this
			for(const auto& m : M.values()) {
				
				// we can always take away all character and generate a anew
				alp = logplusexp(alp, m.second + p_delete_append<alpha,alpha>(m.first, a.output, log_A));
				
				// In an old version of this, we considered a noise model where you just add characters on the end
				// with probability gamma. The trouble with that is that sometimes long new data has strings that
				// are not generated by the "best" (due to approximations used in not evaluation *all* possible
				// execution traces). As a result, we have changed this to allow deletions and insertions, both 
				// of which must be from the end. 
//				if(is_prefix(mstr, astr)) {
//					alp = logplusexp(alp, m.second + lpnoise * (astr.size() - mstr.size()) + lpend);
//				}
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
	 
	 void print(std::string prefix="") override {
		std::lock_guard guard(output_lock); // better not call Super wtih this here
		extern MyHypothesis::data_t prdata;
		extern std::string current_data;
		auto o = this->call(S(""), S("<err>"));
		auto [prec, rec] = get_precision_and_recall(std::cout, o, prdata, PREC_REC_N);
		COUT "#\n";
		COUT "# "; o.print(PRINT_STRINGS);	COUT "\n";
		COUT prefix << current_data TAB this->born TAB this->posterior TAB this->prior TAB this->likelihood TAB QQ(this->parseable()) TAB prec TAB rec;
		COUT "" TAB QQ(this->string()) ENDL
	}
	
	 
};


std::string prdata_path = ""; 
MyHypothesis::data_t prdata; // used for computing precision and recall -- in case we want to use more strings?
S current_data = "";
bool long_output = false; // if true, we allow extra strings, recursions etc. on output

////////////////////////////////////////////////////////////////////////////////////////////
// Main
////////////////////////////////////////////////////////////////////////////////////////////

#include "Top.h"
#include "ParallelTempering.h"
#include "Fleet.h" 

int main(int argc, char** argv){ 
	
	input_path = my_default_input; // set this so it's not fleet's normal input default
	
	// default include to process a bunch of global variables: mcts_steps, mcc_steps, etc
	Fleet fleet("Formal language learner");
	fleet.add_option("-N,--nfactors",      nfactors, "How many factors do we run on?");
	fleet.add_option("-L,--maxlength",     max_length, "Max allowed string length");
	fleet.add_option("-A,--alphabet",  alphabet, "The alphabet of characters to use");
	fleet.add_option("-P,--prdata",  prdata_path, "What data do we use to compute precion/recall?");
	fleet.add_flag("-l,--long-output",  long_output, "Allow extra computation/recursion/strings when we output");
	fleet.initialize(argc, argv); 
	
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
	
	// ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
	// Define the grammar
	// ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
	
	MyGrammar grammar(PRIMITIVES);
		
	for(int a=1;a<=Pdenom/2;a++) { // pack probability into arg, out of 20, since it never needs to be greater than 1/2	
		std::string s = str(a/std::gcd(a,Pdenom)) + "/" + str(Pdenom/std::gcd(a,Pdenom)); // std::to_string(double(a)/24.0).substr(1,4); // substr just truncates lesser digits
		grammar.add<double>(BuiltinOp::op_P, s, (a==Pdenom/2?5.0:1.0), a);
	}
	
	for(size_t a=0;a<nfactors;a++) {	
		auto s = std::to_string(a);
		// NOTE: As of June 11, I took out the SAFE part of these
		grammar.add<S,S>(BuiltinOp::op_RECURSE, S("F")+s+"(%s)", 1.0/nfactors, a);
		grammar.add<S,S>(BuiltinOp::op_MEM_RECURSE, S("memF")+s+"(%s)", 0.2/nfactors, a); // disprefer mem when we don't need it
	}

	// push for each
	for(size_t ai=0;ai<alphabet.length();ai++) {
		grammar.add<S>(BuiltinOp::op_ALPHABET,   Q(alphabet.substr(ai,1)),  10.0/alphabet.length(), (int)alphabet.at(ai) );
	}
	
	// ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
	// Construct a hypothesis
	// ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
	
	MyHypothesis h0; 
	for(size_t fi=0;fi<nfactors;fi++) {// start with the right number of factors
		InnerHypothesis f(&grammar);
		h0.factors.push_back(f.restart());
	}
		
	// we are building up data and TopNs to give t parallel tempering
	std::vector<MyHypothesis::data_t> datas; // load all the data	
	std::vector<TopN<MyHypothesis>> tops;
	for(size_t i=0;i<data_amounts.size();i++){ 
		MyHypothesis::data_t d;
		
		S data_path = input_path + "-" + data_amounts[i] + ".txt";	
		load_data_file(d, data_path.c_str());
		
		datas.push_back(d);
		tops.push_back(TopN<MyHypothesis>(ntop));
	}

	// ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
	// Actually run
	// ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
	
	TopN<MyHypothesis> all(ntop); 
//	all.set_print_best(true);
	
	tic();	
	for(size_t di=0;di<datas.size() and !CTRL_C;di++) {
		
		// the max temperature here is going to be the amount of data
		// that's because if we make it much bigger, we waste lower chains; if we make it much smaller, 
		// we don't get good mixing in the lowest chains. 
		// No theory here, this just seems to be about what works well. 
		ParallelTempering samp(h0, &datas[di], all, NTEMPS, std::max(1, stoi(data_amounts[di]))); 
//		for(auto& x: datas[di]) { CERR di TAB x.output TAB x.reliability ENDL;	}
		samp.run(Control(mcmc_steps/datas.size(), runtime/datas.size(), nthreads, RESTART), SWAP_EVERY, 5*60*1000);	

		// set up to print using a larger set if we were given this option
		if(long_output){
			MAX_STEPS_PER_FACTOR   = 32000; //4096; 
			MAX_OUTPUTS_PER_FACTOR = 12000; //512; - make it bigger than
			PRINT_STRINGS = 1024;
			max_length = 2048; 
			MIN_LP = -100;
		}
		
		all.print(data_amounts[di]);
		
		if(long_output) {
			max_length = 256;
			MAX_STEPS_PER_FACTOR   = 4096; 
			MAX_OUTPUTS_PER_FACTOR = 1024; 
			PRINT_STRINGS = 128;
			MIN_LP = -25;
		}	
		
		if(di+1 < datas.size()) {
			all = all.compute_posterior(datas[di+1]); // update for next time
		}
		
		// start next time on the best hypothesis we've found so far
		if(not all.empty()) {
			h0 = all.best();			
		}	
		
	}
	tic();
	
	
	// Vanilla MCMC, serial
//	for(size_t i=0;i<datas.size();i++){
//		tic();
//		MCMCChain chain(h0, &datas[i], all);
//		chain.run(Control(mcmc_steps/datas[i].size(), runtime/datas.size()));
//		tic();	
//	}

	// Multiple chain MCMC
//	for(size_t i=0;i<datas.size();i++){
//		tic();
//		ChainPool chains(h0, &datas[i], all, 1);
//		assert(chains.pool.size() == 1);
//		chains.run(Control(mcmc_steps/datas.size(), runtime/datas.size()));
//		tic();	
//	}
//	
}
