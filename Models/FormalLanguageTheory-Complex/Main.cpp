/* //
 * In this version, input specifies the prdata minus the txt 
 * 	--input=data/NewportAslin
 * and then we'll add in the data amounts, so that now each call will loop over amounts of data
 * and preserve the top hypotheses
 * 
 * NOTE: Since the PNAS resubmission, we have changed this to use char and changed serialization so that column will be different. 
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
size_t max_length = 128; // (more than 256 needed for count, a^2^n, a^n^2, etc -- see command line arg)
size_t max_setsize = 64; // throw error if we have more than this
size_t nfactors = 2; // how may factors do we run on? (defaultly)

static constexpr float alpha = 0.01; // probability of insert/delete errors (must be a float for the string function below)

size_t PREC_REC_N   = 25;  // if we make this too high, then the data is finite so we won't see some stuff
const size_t MAX_LINES    = 1000000; // how many lines of data do we load? The more data, the slower...
const size_t MAX_PR_LINES = 1000000; 

const size_t NTEMPS = 5; 
const size_t MAX_TEMP = 2.0; 
unsigned long PRINT_STRINGS; // print at most this many strings for each hypothesis

std::vector<S> data_amounts={"1", "2", "5", "10", "20", "50", "100", "200", "500", "1000", "2000", "5000", "10000", "50000", "100000"}; // how many data points do we run on?
//std::vector<S> data_amounts={"100"}; // how many data points do we run on?

// useful for printing -- so we know how many tokens there were in the data
size_t current_ntokens = 0; // how many tokens are there currently? Just useful to know

///~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
/// Declare a grammar
///~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

#include "Grammar.h"
#include "Singleton.h"

class MyGrammar : public Grammar<S,S,     S,char,bool,double,StrSet,int>,
				  public Singleton<MyGrammar> {
public:
	MyGrammar() {
		add("tail(%s)", +[](S s) -> S { 
			if(s.length()>0) 
				s.erase(0); 
			return s;
		});
		
		add_vms<S,S,S>("append(%s,%s)",  new std::function(+[](MyGrammar::VirtualMachineState_t* vms, int) {
			S b = vms->getpop<S>();
			S& a = vms->stack<S>().topref();
			
			if(a.length() + b.length() > max_length) throw VMSRuntimeError();
			else 									 a += b; 
		}));
		
		add_vms<S,S,char>("pair(%s,%s)",  new std::function(+[](MyGrammar::VirtualMachineState_t* vms, int) {
			char b = vms->getpop<char>();
			S& a = vms->stack<S>().topref();
			
			if(a.length() + 1 > max_length) throw VMSRuntimeError();
			else 							a += b; 
		}));

//		add("c2s(%s)", +[](char c) -> S { return S(1,c); });
		add("head(%s)", +[](S s) -> S { return (s.empty() ? S("") : S(1,s.at(0))); }); // head here could be a char, except that it complicates stuff, so we'll make it a string str
		add("\u00D8", +[]() -> S { return S(""); }, 10.0);
		add("(%s==%s)", +[](S x, S y) -> bool { return x==y; });
		add("empty(%s)", +[](S x) -> bool { 	return x.length()==0; });
		
		add("insert(%s,%s)", +[](S x, S y) -> S { 
			size_t l = x.length();
			if(l == 0) 
				return y;
			else if(l + y.length() > max_length) 
				throw VMSRuntimeError();
			else {
				// put y into the middle of x
				size_t pos = l/2;
				S out = x.substr(0, pos); 
				out.append(y);
				out.append(x.substr(pos));
				return out;
			}				
		}, 0.2);
		
		
		// add an alphabet symbol (\Sigma)
		add("\u03A3", +[]() -> StrSet {
			StrSet out; 
			for(const auto& a: alphabet) {
				out.emplace(1,a);
			}
			return out;
		}, 5.0);
		
		// set operations:
		add("{%s}", +[](S x) -> StrSet  { 
			StrSet s; s.insert(x); return s; 
		}, 10.0);
		
		add("(%s\u222A%s)", +[](StrSet s, StrSet x) -> StrSet { 
			for(auto& xi : x) {
				s.insert(xi);
				if(s.size() > max_setsize) throw VMSRuntimeError();
			}
			return s;
		});
		
		add("(%s\u2216%s)", +[](StrSet s, StrSet x) -> StrSet {
			StrSet output; 
			
			// this would usually be implemented like this, but it's overkill (and slower) because normally 
			// we just have single elemnents
			std::set_difference(s.begin(), s.end(), x.begin(), x.end(), std::inserter(output, output.begin()));
			
			return output;
		});

		// Define our custom op here, which works on the VMS, because otherwise sampling is very slow
		// here we can optimize the small s examples
		add_vms<S,StrSet>("sample(%s)", new std::function(+[](MyGrammar::VirtualMachineState_t* vms, int) {
						
			// implement sampling from the set.
			// to do this, we read the set and then push all the alternatives onto the stack
			StrSet s = vms->template getpop<StrSet>();
				
			if(s.size() == 0 or s.size() > max_setsize) {
				throw VMSRuntimeError();
			}
			
			// One useful optimization here is that sometimes that set only has one element. So first we check that, and if so we don't need to do anything 
			// also this is especially convenient because we only have one element to pop
			if(s.size() == 1) {
				auto it = s.begin();
				S x = *it;
				vms->push<S>(std::move(x));
			}
			else {
				// else there is more than one, so we have to copy the stack and increment the lp etc for each
				// NOTE: The function could just be this latter branch, but that's much slower because it copies vms
				// even for single stack elements
				
				// now just push on each, along with their probability
				// which is here decided to be uniform.
				const double lp = -log(s.size());
				for(const auto& x : s) {
					bool b = vms->pool->copy_increment_push(vms,x,lp);
					if(not b) break; // we can break since all of these have the same lp -- if we don't add one, we won't add any!
				}
				
				vms->status = vmstatus_t::RANDOM_CHOICE; // we don't continue with this context		
			}
		}));
			
		add("and(%s,%s)",    Builtins::And<MyGrammar>);
		add("or(%s,%s)",     Builtins::Or<MyGrammar>);
		add("not(%s)",       Builtins::Not<MyGrammar>);
		
		add("x",             Builtins::X<MyGrammar>, 5);
		
		add("if(%s,%s,%s)",  Builtins::If<MyGrammar,S>);
		add("if(%s,%s,%s)",  Builtins::If<MyGrammar,StrSet>);
		add("if(%s,%s,%s)",  Builtins::If<MyGrammar,double>);

		add("flip(%s)",      Builtins::FlipP<MyGrammar>, 10.0);

		const int pdenom=24;
		for(int a=1;a<=pdenom/2;a++) { 
			std::string s = str(a/std::gcd(a,pdenom)) + "/" + str(pdenom/std::gcd(a,pdenom)); 
			if(a==pdenom/2) {
				add_terminal( s, double(a)/pdenom, 5.0);
			}
			else {
				add_terminal( s, double(a)/pdenom, 1.0);
			}
		}
		
		add("F%s(%s)" ,  Builtins::LexiconRecurse<MyGrammar,int>, 1./4.);
		add("Fm%s(%s)",  Builtins::LexiconMemRecurse<MyGrammar,int>, 1./4.);
//		add("Fs%s(%s)",  Builtins::LexiconSafeRecurse<MyGrammar,int>, 1./4.);
//		add("Fsm%s(%s)", Builtins::LexiconSafeMemRecurse<MyGrammar,int>, 1./4.);
	}
} grammar;

///~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
/// Declare our hypothesis type
///~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

#include "LOTHypothesis.h"

class InnerHypothesis : public LOTHypothesis<InnerHypothesis,S,S,MyGrammar,&grammar> {
public:
	using Super = LOTHypothesis<InnerHypothesis,S,S,MyGrammar,&grammar>;
	using Super::Super;
	
	static constexpr double regenerate_p = 0.7;
	
	[[nodiscard]] virtual std::pair<InnerHypothesis,double> propose() const override {
		
		std::pair<Node,double> x;
		if(flip(regenerate_p)) {
			x = Proposals::regenerate(&grammar, value);	
		}
		else {
			if(flip()) x = Proposals::insert_tree(&grammar, value);	
			else       x = Proposals::delete_tree(&grammar, value);	
		}
		return std::make_pair(InnerHypothesis(std::move(x.first)), x.second); 
	}	
	
};

#include "Lexicon.h"

class MyHypothesis final : public Lexicon<MyHypothesis, int,InnerHypothesis, S, S> {
public:	
	
	using Super = Lexicon<MyHypothesis, int,InnerHypothesis, S, S>;
	using Super::Super;

	virtual DiscreteDistribution<S> call(const S x, const S& err=S{}) {
		// this calls by calling only the last factor, which, according to our prior,
		
		// make myself the loader for all factors
		for(auto& [k,f] : factors) f.program.loader = this; 

		extern size_t nfactors;
		assert(nfactors == factors.size());
		return factors[nfactors-1].call(x, err); // we call the factor but with this as the loader.  
	}
	 
	 // We assume input,output with reliability as the number of counts that input was seen going to that output
	 virtual double compute_single_likelihood(const datum_t& datum) override { assert(0); }
	 

	 double compute_likelihood(const data_t& data, const double breakout=-infinity) override {
		// this version goes through and computes the predictive probability of each prefix
		 
		const auto& M = call(S(""), S("<err>")); 
		
		const float log_A = log(alphabet.size());

		likelihood = 0.0;
		for(const auto& a : data) {
			double alp = -infinity; // the model's probability of this
			for(const auto& m : M.values()) {				
				// we can always take away all character and generate a anew
				alp = logplusexp(alp, m.second + p_delete_append<alpha,alpha>(m.first, a.output, log_A));
			}
			likelihood += alp * a.count; 
			
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
		extern std::pair<double,double> mem_pr;
		auto o = this->call(S(""), S("<err>"));
		auto [prec, rec] = get_precision_and_recall(o, prdata, PREC_REC_N);
		COUT "#\n";
		COUT "# "; o.print(PRINT_STRINGS);	COUT "\n";
		COUT prefix << current_data TAB current_ntokens TAB mem_pr.first TAB mem_pr.second TAB this->born TAB this->posterior TAB this->prior TAB this->likelihood TAB QQ(this->serialize()) TAB prec TAB rec;
		COUT "" TAB QQ(this->string()) ENDL
	}
	
	 
};


std::string prdata_path = ""; 
MyHypothesis::data_t prdata; // used for computing precision and recall -- in case we want to use more strings?
S current_data = "";
bool long_output = false; // if true, we allow extra strings, recursions etc. on output
bool long_strings = false; 
std::pair<double,double> mem_pr; 

////////////////////////////////////////////////////////////////////////////////////////////
// Main
////////////////////////////////////////////////////////////////////////////////////////////

#ifndef DO_NOT_INCLUDE_MAIN

#include "VirtualMachine/VirtualMachineState.h"
#include "VirtualMachine/VirtualMachinePool.h"
#include "TopN.h"
#include "ParallelTempering.h"
#include "Fleet.h" 

int main(int argc, char** argv){ 
	
	// Set this
	VirtualMachineControl::MIN_LP = -15;
	
	FleetArgs::input_path = my_default_input; // set this so it's not fleet's normal input default
	
	// default include to process a bunch of global variables: mcts_steps, mcc_steps, etc
	Fleet fleet("Formal language learner");
	fleet.add_option("-N,--nfactors",      nfactors, "How many factors do we run on?");
	fleet.add_option("-L,--maxlength",     max_length, "Max allowed string length");
	fleet.add_option("-A,--alphabet",  alphabet, "The alphabet of characters to use");
	fleet.add_option("-P,--prdata",  prdata_path, "What data do we use to compute precion/recall?");
	fleet.add_option("--prN",  PREC_REC_N, "How many data points to compute precision and recall?");	
	fleet.add_flag("-l,--long-output",  long_output, "Allow extra computation/recursion/strings when we output");
	fleet.initialize(argc, argv); 
	
	COUT "# Using alphabet=" << alphabet ENDL;


	// ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
	// Set up the grammar using command line arguments
	// ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
	
	// each of the recursive calls we are allowed
	for(size_t i=0;i<nfactors;i++) {	
		grammar.add_terminal( str(i), (int)i, 1.0/nfactors);
	}
		
	for(const char c : alphabet) {
		grammar.add_terminal( Q(S(1,c)), c, 5.0/alphabet.length());
	}

	// ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
	// Load the data
	// ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
	
	// Input here is going to specify the PRdata path, minus the txt
	if(prdata_path == "") {	prdata_path = FleetArgs::input_path+".txt"; }
	
	load_data_file(prdata, prdata_path.c_str()); // put all the data in prdata
	for(auto d : prdata) {	// Add a check for any data not in the alphabet
		for(size_t i=0;i<d.output.length();i++){
			if(alphabet.find(d.output.at(i)) == std::string::npos) {
				CERR "*** Character '" << d.output.at(i) << "' in " << d.output << " is not in the alphabet '" << alphabet << "'" ENDL;
				assert(0);
			}
		}
	}
	
	// We are going to build up the data
	std::vector<MyHypothesis::data_t> datas; // load all the data	
	for(size_t i=0;i<data_amounts.size();i++){ 
		MyHypothesis::data_t d;
		
		S data_path = FleetArgs::input_path + "-" + data_amounts[i] + ".txt";	
		load_data_file(d, data_path.c_str());
		
		datas.push_back(d);
	}

	// ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
	// Actually run
	// ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
		
	// Build up an initial hypothesis with the right number of factors
	MyHypothesis h0;
	for(size_t i=0;i<nfactors;i++) { h0[i] = InnerHypothesis::sample(); }
 
	
	TopN<MyHypothesis> all; 
	//	all.set_print_best(true);
	
	ParallelTempering samp(h0, &datas[0], FleetArgs::nchains, MAX_TEMP); 
//	ChainPool samp(h0, &datas[0], NTEMPS); 

	// Set these up as the defaults as below
	VirtualMachineControl::MAX_STEPS  = 1024; 
	VirtualMachineControl::MAX_OUTPUTS = 256; 
	VirtualMachineControl::MIN_LP = -15;
	PRINT_STRINGS = 512;	
	
	tic();	
	for(size_t di=0;di<datas.size() and !CTRL_C;di++) {
		auto& this_data = datas[di];
		
		samp.set_data(&this_data, true);
		
		// compute the prevision and recall if we just memorize the data
		{
			double cz = 0; // find the normalizer for counts			
			for(auto& d: this_data) cz += d.count; 
			
			DiscreteDistribution<S> mem_d;
			for(auto& d: this_data) 
				mem_d.addmass(d.output, log(d.count)-log(cz)); 
			
			// set a global var to be used when printing hypotheses above
			mem_pr = get_precision_and_recall(mem_d, prdata, PREC_REC_N);
		}
		
		
		// set this global variable so we know
		current_ntokens = 0;
		for(auto& d : this_data) { UNUSED(d); current_ntokens++; }
				
		for(auto& h : samp.run(Control(FleetArgs::steps/datas.size(), FleetArgs::runtime/datas.size(), FleetArgs::nthreads, FleetArgs::restart))
						| print(FleetArgs::print) | all) {
			UNUSED(h);
		}	

		// set up to print using a larger set if we were given this option
		if(long_output){
			VirtualMachineControl::MAX_STEPS  = 32000; 
			VirtualMachineControl::MAX_OUTPUTS = 16000;
			VirtualMachineControl::MIN_LP = -40;
			PRINT_STRINGS = 5000;
		}

		all.print(data_amounts[di]);
		
		// restore
		if(long_output) {
			VirtualMachineControl::MAX_STEPS  = 1024; 
			VirtualMachineControl::MAX_OUTPUTS = 256; 
			VirtualMachineControl::MIN_LP = -15;
			PRINT_STRINGS = 512;
		}	
		
		if(di+1 < datas.size()) {
			all = all.compute_posterior(datas[di+1]); // update for next time
		}
		
	}
	tic();
	
}

#endif
