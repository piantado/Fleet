/*
 * 	TODO: This likelihood is not quite right because people in the experiment are sampling
 *        without replacement.
 * 
 * 	Languages:
 * 	- FLT language
 *  - Simplified FLT
 *  - Dehaene
 * 	- Dehaene + subst
 *  - FLT plus subst
 * 
 * 
 * 	
 * */

//#define DO_NOT_INCLUDE_MAIN 1
#include <cstddef>
#include <vector>

//#include "../FormalLanguageTheory-Complex/Main.cpp"
const size_t MAX_FACTORS = 3;

#include "Data/HumanDatum.h"
#include "TopN.h"
#include "Fleet.h"
#include "Miscellaneous.h"
#include "Strings.h"
#include "MCMCChain.h"
#include "Coroutines.h"

std::string alphabet = "abcd";
std::string hypothesis_path = "hypotheses.txt";
std::string runtype         = "both"; // can be both, hypotheses (just find hypotheses), or grammar (just do mcmc, loading from hypothesis_path)

std::vector<size_t> ntops = {1,5,10,25,100,250,500,1000}; // save the top this many from each hypothesis

using S = std::string; 

std::string substitute(std::string newA, std::string s) {			
	for(size_t i=0;i<s.size();i++) {
		auto idx = alphabet.find(s[i]);
		s[i] = newA[idx % newA.size()]; // takes mod newA.size
	}
	return s;
}

///~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
/// Declare a grammar
///~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

#include "Grammar.h"
#include "Singleton.h"

class MyGrammar : public Grammar<S,S,     S,char,bool,double,StrSet,int>,
				  public Singleton<MyGrammar> {
public:
	MyGrammar() {
		
		
		// add a primitive here that stochastically samples a substitution
		// from the alphabet
		
		
		
		
		add("substitute(%s,%s)", substitute);

		
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
		add("head(%s)", +[](S s) -> S { return (s.empty() ? emptystring : S(1,s.at(0))); }); // head here could be a char, except that it complicates stuff, so we'll make it a string str
		add("\u00D8", +[]() -> S { return emptystring; }, CONSTANT_P);
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
		});
		
		
		// add an alphabet symbol (\Sigma)
		add("\u03A3", +[]() -> StrSet {
			StrSet out; 
			for(const auto& a: alphabet) {
				out.emplace(1,a);
			}
			return out;
		}, CONSTANT_P);
		
		// set operations:
		add("{%s}", +[](S x) -> StrSet  { 
			StrSet s; s.insert(x); return s; 
		}, CONSTANT_P);
		
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
		
		add("x",             Builtins::X<MyGrammar>, CONSTANT_P);
		
		add("if(%s,%s,%s)",  Builtins::If<MyGrammar,S>);
		add("if(%s,%s,%s)",  Builtins::If<MyGrammar,StrSet>);
		add("if(%s,%s,%s)",  Builtins::If<MyGrammar,double>);

		add("flip(%s)",      Builtins::FlipP<MyGrammar>, CONSTANT_P);

		const int pdenom=24;
		for(int a=1;a<=pdenom/2;a++) { 
			std::string s = str(a/std::gcd(a,pdenom)) + "/" + str(pdenom/std::gcd(a,pdenom)); 
			if(a==pdenom/2) {
				add_terminal( s, double(a)/pdenom, CONSTANT_P);
			}
			else {
				add_terminal( s, double(a)/pdenom, 1.0);
			}
		}
		
		add("F%s(%s)" ,  Builtins::LexiconRecurse<MyGrammar,int>, 1./2.);
		add("Fm%s(%s)",  Builtins::LexiconMemRecurse<MyGrammar,int>, 1./2.);
	}
} grammar;

///~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
/// Declare an inner hypothesis type
///~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

#include "LOTHypothesis.h"

class InnerHypothesis : public LOTHypothesis<InnerHypothesis,S,S,MyGrammar,&grammar> {
public:
	using Super = LOTHypothesis<InnerHypothesis,S,S,MyGrammar,&grammar>;
	using Super::Super;
	
	static constexpr double regenerate_p = 0.7;
	
	[[nodiscard]] virtual std::optional<std::pair<InnerHypothesis,double>> propose() const override {
		try { 
			std::optional<std::pair<Node,double>> x;

			if(flip(regenerate_p))       x = Proposals::regenerate(&grammar, value);	
			else if(flip(0.1))  x = Proposals::sample_function_leaving_args(&grammar, value);
			else if(flip(0.1))  x = Proposals::swap_args(&grammar, value);
			else if(flip())     x = Proposals::insert_tree(&grammar, value);	
			else                x = Proposals::delete_tree(&grammar, value);			
			
			if(not x) { return {}; }
			
			return std::make_pair(InnerHypothesis(std::move(x.value().first)), x.value().second); 
				
		} catch (DepthException& e) { return {}; }
	}	
	
};


///~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
/// Declare our hypothesis type
///~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

#include "Lexicon.h"

class MyHypothesis final : public Lexicon<MyHypothesis, int,InnerHypothesis, S, S> {
public:	
	
	using Super = Lexicon<MyHypothesis, int,InnerHypothesis, S, S>;
	using Super::Super;

	virtual DiscreteDistribution<S> call(const S x, const S& err=S{}) {
		// this calls by calling only the last factor, which, according to our prior,
		
		// make myself the loader for all factors
		for(auto& [k,f] : factors) {
			f.program.loader = this; 
			f.was_called = false; // zero this please
		}

		return factors[factors.size()-1].call(x, err); // we call the factor but with this as the loader.  
	}
	 
	 // We assume input,output with reliability as the number of counts that input was seen going to that output
	 virtual double compute_single_likelihood(const datum_t& datum) override { assert(0); }
	 

	 double compute_likelihood(const data_t& data, const double breakout=-infinity) override {
		// this version goes through and computes the predictive probability of each prefix
		 
		const auto& M = call(emptystring, errorstring); 
		
		// calling "call" first made was_called false on everything, and
		// this will only be set to true if it was called (via push_program)
		// so here we check and make sure everything was used and if not
		// we give it a -inf likelihood
		// NOTE: This is a slow way to do this because it requires running the hypothesis
		// but that's hard to get around with how factosr work. 
		// NOTE that this checks factors over ALL prob. outcomes
		for(auto& [k,f] : factors) {
			if(not f.was_called) {
				return likelihood=-infinity;
			}
		}
		
		// otherwise let's compute the likelihood
		
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
		auto o = this->call(emptystring, errorstring);
		auto [prec, rec] = get_precision_and_recall(o, prdata, PREC_REC_N);
		COUT "#\n";
		COUT "# "; o.print(PRINT_STRINGS);	COUT "\n";
		COUT prefix << current_data TAB current_ntokens TAB mem_pr.first TAB mem_pr.second TAB this->born TAB this->posterior TAB this->prior TAB this->likelihood TAB QQ(this->serialize()) TAB prec TAB rec;
		COUT "" TAB QQ(this->string()) ENDL
	}
	
	 
};



// The format of data for grammar inferece
using MyHumanDatum = HumanDatum<MyHypothesis>;

#include "GrammarHypothesis.h"

// Define a grammar inference class -- nothing special needed here
class MyGrammarHypothesis final : public GrammarHypothesis<MyGrammarHypothesis, MyHypothesis, MyHumanDatum> {
public:
	using Super = GrammarHypothesis<MyGrammarHypothesis, MyHypothesis, MyHumanDatum>;
	using Super::Super;
	using data_t = Super::data_t;		
	
	virtual double human_chance_lp(const typename datum_t::output_t& r, const datum_t& hd) const override {
		// here we are going to make chance be exponential in the length of the response
		return -(double)r.length()*log(alphabet.size()); // NOTE: Without the double case, we negate r.length() first and it's awful
	}
};


int main(int argc, char** argv){ 	
	
	alphabet = "abcd"; // set this as the default. 
	
	Fleet fleet("An example of grammar inference for formal languages");
	fleet.add_option("--hypotheses",  hypothesis_path, "Where to load or save hypotheses.");
	fleet.add_option("--runtype",  runtype, "hypotheses = do we just do mcmc on hypotheses; grammar = mcmc on the grammar, loading the hypotheses; both = do both");
	fleet.add_option("-a,--alphabet", alphabet, "Alphabet we will use"); 	// add my own args
	fleet.initialize(argc, argv);

	// Add to the grammar
	for(size_t i=0;i<MAX_FACTORS;i++) {	
		grammar.add_terminal( str(i), (int)i, 1.0/MAX_FACTORS);
	}
	
	for(const char c : alphabet) {
		grammar.add_terminal( Q(S(1,c)), c, CONSTANT_P/alphabet.length());
	}

	///////////////////////////////
	// Read the human data
	///////////////////////////////	
	
	std::vector<MyHumanDatum> human_data;         // what do we run grammar inference on
	std::vector<MyHypothesis::data_t*> mcmc_data; // what will we run search over MyHypothesis on?
	for(auto [row, stimulus, all_responses] : read_csv<3>("human-data.csv", '\t')) {
		
		// split up stimulus into components
		auto this_data = new MyHypothesis::data_t();
		auto decay_pos = new std::vector<int>(); 
		int i=0;
		for(auto& s : string_to<std::vector<S>>(stimulus)) { // convert to a string vector and then to data
			this_data->push_back(MyHypothesis::datum_t{.input=EMPTY_STRING, .output=s});
			decay_pos->push_back(i++);
			
			// add a check that we're using the right alphabet here
			check_alphabet(s, alphabet);
		}
		size_t ndata = this_data->size(); // note that below we might change this_data's pointer, but we still need this length
			
		// process all_responses into a map from strings to counts
		auto m = string_to<std::vector<std::pair<std::string,unsigned long>>>(all_responses);
		
		// This glom thing will use anything overlapping in mcmc_data, and give
		// us a new pointer if it can. This decreases the amount we need to run MCMC search
		// on and saves memory; NOTE: This changes this_data
		glom(mcmc_data, this_data); 	
		
		// now just put into the data
		human_data.push_back(MyHumanDatum{.data=this_data, 
										  .ndata=ndata, 
										  .predict=const_cast<S*>(&EMPTY_STRING), 
										  .responses=m,
										  .chance=NaN, // should not be used via human_chance_lp above
										  .decay_position=decay_pos,
										  .my_decay_position=i-1
										  });
	}
	
	COUT "# Loaded " << human_data.size() << " human data points and " << mcmc_data.size() << " mcmc data points" ENDL;
	
	///////////////////////////////
	// Run a search over hypotheses
	///////////////////////////////	
	

	if(runtype == "hypotheses" or runtype == "both") {
		
		std::vector<std::set<MyHypothesis>> all_hypotheses(ntops.size());
		
		// here, we need to run on each number of factors
		for(size_t nf=1;nf<=MAX_FACTORS;nf++) {
		
			// Need to fix each of the recursive arguments to only use those up to nf
			// NOTE That we can't use grammar.clear_all because that will change the
			// counts of how things are aligned in the grammar inference			
			nonterminal_t nt = grammar.nt<int>();
			grammar.Z[nt] = 0.0; // reset 
			for(size_t i=0;i<MAX_FACTORS;i++) {	
				Rule* r = grammar.get_rule(nt, str(i));
				if(i < nf) {
					r->p = 1.0/nf; // change this rule -- warning, kinda dangerous
					grammar.Z[nt] += 1.0/nf;
				}
				else {
					r->p = 0.0; // set to be zero to assure it's never sampled. 
				}
			}
			
			auto h0 = MyHypothesis::sample({}); 
			for(size_t i=0;i<nf;i++) 
				h0.factors[i] = InnerHypothesis::sample();
			
			auto hyps = get_hypotheses_from_mcmc(h0, mcmc_data, InnerControl(), ntops);
		
			for(size_t t=0;t<ntops.size();t++) 
				all_hypotheses[t].insert(hyps[t].begin(), hyps[t].end() );
		}
		
		for(size_t t=0;t<ntops.size();t++) 
			save(FleetArgs::output_path+"."+str(ntops[t]), all_hypotheses[t]);
		
		CTRL_C = 0; // reset control-C so we can break again for the next mcmc		
	}

	
	///////////////////////////////
	// Run the grammar inference
	///////////////////////////////	
	
	if(runtype == "grammar" or runtype == "both") { 
		
		auto hypotheses = load<MyHypothesis>(hypothesis_path);
		COUT "# Hypothesis size: " TAB hypotheses.size() ENDL;
		assert(hypotheses.size() > 0 && "*** Somehow we don't have any hypotheses!");
	
		// store the best
		TopN<MyGrammarHypothesis> topMAP(1); // keeps track of the map
	
		// the MCMC chain we run over grammars
		auto thechain = MCMCChain<MyGrammarHypothesis>(MyGrammarHypothesis::sample(hypotheses, &human_data), &human_data);
		
		// Main MCMC running loop!
		size_t nloops = 0;
		for(const auto& h : thechain.run(Control()) | topMAP | print(FleetArgs::print) | thin(FleetArgs::thin) ) {
			
			{
				std::ofstream outsamples(FleetArgs::output_path+"/samples.txt", std::ofstream::app);
				outsamples << h.string(str(thechain.samples)+"\t") ENDL;
				outsamples.close();
			}
			
			// Every this many steps (AFTER thinning) we print out the model predictions
			if(nloops++ % 100 == 0) {
				
				// Now when we're done, show the model predicted outputs on the data
				// We'll use the MAP grammar hypothesis for this
				auto& MAP = topMAP.best();
				const Matrix hposterior = MAP.compute_normalized_posterior(); 
				
				//PRINTN("#", h.posterior ); 
				
				std::ofstream outMAP(FleetArgs::output_path+"/MAP-strings.txt");
				std::ofstream outtop(FleetArgs::output_path+"/top-H.txt");
				
				#pragma omp parallel for
				for(size_t i=0;i<human_data.size();i++) {
					auto& hd = human_data[i];
					
					// compute model predictions on each data point
					auto model_predictions = MAP.compute_model_predictions(i, hposterior);		
					
					// now figure out the full set of strings
					#pragma omp critical
					{
						// find the MAP model hypothesis for this data point
						double max_post = 0.0; 
						size_t max_hi = 0;
						double lse = -infinity;
						for(auto hi=0;hi<hposterior.rows();hi++) {
							lse = logplusexp(lse, (double)log(hposterior(hi,i)));
							if(hposterior(hi,i) > max_post) { 
								max_post = hposterior(hi,i); 
								max_hi = hi;
							}
						}					
						
						// store the top hypothesis found
						auto mapH = hypotheses[max_hi];
						auto cll = mapH.call("");
						OUTPUTN(outtop, "#", cll.string());
						OUTPUTN(outtop, i, max_hi, max_post, lse, QQ(mapH.string()), QQ(str(slice(*hd.data, 0, hd.ndata))));
						
						// get all of the strings output by people or the model
						std::set<std::string> all_strings;
						for(const auto& [s,p] : model_predictions) {
							all_strings.insert(s);
							UNUSED(p);
						}
						
						std::map<std::string, size_t> m; // make a map of outputs so we can index easily below
						size_t N = 0; // total number
						for(const auto& [s,c] : hd.responses)  {
							all_strings.insert(s);
							N += c;
							m[s] = c;
						}
					
						for(const auto& s : all_strings) {
							OUTPUTN(outMAP, i, Q(s), get(model_predictions, s, 0), get(m, s, 0), N);
						}
					}
					
				}
				outMAP.close();
						
			}
			
		}		
		
		
	}
	
}