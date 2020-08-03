

// TODO: Might be easier to have responses be to the entire list of objects?

#include <assert.h>
#include <set>
#include <regex>
#include <vector>
#include <tuple>
#include <functional>
#include <cmath>

#include "EigenLib.h"

#include "Object.h"

enum class Shape { rectangle, triangle, circle};
enum class Color { yellow, green, blue};
enum class Size  { size1, size2, size3};

// Define a kind of object with these features
typedef Object<Shape,Color,Size>  MyObject;

// An input here is a pair of a set and an object
typedef std::pair<std::set<MyObject>, MyObject> MyInput;

//typedef struct LearnerDatum {
//	// What the learner takes as input
//	MyObject x;
//	bool correctAnswer;
//	double alpha;
//	std::set<MyObject>* set;// a pointer makes it easy to modify as we read in the file
//	size_t setNumber;
//	size_t responseInSet;
//	
//	bool operator==(const LearnerDatum& o) const { 
//		return (*set) == (*o.set) and x == o.x;
//	}
//} LearnerDatum; 


const double alpha = 0.9; // fixed for the learning part of the model


///~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

#include "Primitives.h"
#include "Builtins.h"

const double FEATURE_WEIGHT = 5.0; // in our prior, what's the weight on features?

std::tuple PRIMITIVES = {
	Primitive("and(%s,%s)",     +[](bool a, bool b) -> bool { return (a and b); }), // optional specification of prior weight (default=1.0)
	Primitive("or(%s,%s)",      +[](bool a, bool b) -> bool { return (a or b); }),
	Primitive("not(%s)",        +[](bool a)         -> bool { return (not a); }),
	Primitive("iff(%s,%s)",     +[](bool a, bool b) -> bool { return (a == b); }),
	Primitive("implies(%s,%s)", +[](bool a, bool b) -> bool { return ((not a) or (a and b)); }),
	Primitive("xor(%s,%s)",     +[](bool a, bool b) -> bool { return (a xor b); }),
	Primitive("nand(%s,%s)",    +[](bool a, bool b) -> bool { return not (a and b); }),
	Primitive("nor(%s,%s)",     +[](bool a, bool b) -> bool { return not (a or b); }),
	// that + is really insane, but is needed to convert a lambda to a function pointer

	Primitive("yellow(%s)",    +[](MyObject x)       -> bool { return x.is(Color::yellow); }, FEATURE_WEIGHT),
	Primitive("green(%s)",     +[](MyObject x)       -> bool { return x.is(Color::green); }, FEATURE_WEIGHT),
	Primitive("blue(%s)",      +[](MyObject x)       -> bool { return x.is(Color::blue); }, FEATURE_WEIGHT),

	Primitive("rectangle(%s)", +[](MyObject x)       -> bool { return x.is(Shape::rectangle); }, FEATURE_WEIGHT),
	Primitive("triangle(%s)",  +[](MyObject x)       -> bool { return x.is(Shape::triangle); }, FEATURE_WEIGHT),
	Primitive("circle(%s)",    +[](MyObject x)       -> bool { return x.is(Shape::circle); }, FEATURE_WEIGHT),
	
	Primitive("size1(%s)",     +[](MyObject x)       -> bool { return x.is(Size::size1); }, FEATURE_WEIGHT),
	Primitive("size2(%s)",     +[](MyObject x)       -> bool { return x.is(Size::size2); }, FEATURE_WEIGHT),
	Primitive("size3(%s)",     +[](MyObject x)       -> bool { return x.is(Size::size3); }, FEATURE_WEIGHT),
	
	Primitive("%.arg",         +[](MyInput x)        -> MyObject { return x.second; }),
		
	// but we also have to add a rule for the BuiltinOp that access x, our argument
	Builtin::X<MyObject>("x", 10.0)
};

///~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

#include "Grammar.h"

class MyGrammar : public Grammar<bool,MyObject> {
	using Super=Grammar<bool,MyObject>;
	using Super::Super;
};

///~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// Includes critical files. Also defines some variables (mcts_steps, explore, etc.) that get processed from argv 

#include "GrammarHypothesis.h" 
#include "LOTHypothesis.h"


class MyHypothesis final : public LOTHypothesis<MyHypothesis,MyInput,bool,MyGrammar> {
public:
	// This is going to assume that all variables other than x are universally quantified over. 
	using Super = LOTHypothesis<MyHypothesis,MyInput,bool,MyGrammar>;
	using Super::Super;
	
	double compute_single_likelihood(const datum_t& di) override {
		// TODO: For now we are just ignoring the set, althoug that should change!
		bool out = callOne(di.input, false);
		return (out == di.output ? log(di.reliability + (1.0-di.reliability)/2.0) : log((1.0-di.reliability)/2.0));
	}
};

typedef HumanDatum<MyHypothesis> MyHumanDatum;

LL_t my_compute_incremental_likelihood(std::vector<MyHypothesis>& hypotheses, std::vector<MyHumanDatum>& human_data) {
	// special case of incremental likelihood since we are accumulating data (by sets)
	// So we have written a special case here
	
	// likelihood out will now be a mapping from hypothesis, data_element to prior likelihoods of individual elements
	LL_t out; 
	
	for(size_t h=0;h<hypotheses.size() and !CTRL_C;h++) {
		
		// This will assume that as things are sorted in human_data, they will tend to use
		// the same human_data.data pointer (e.g. they are sorted this way) so that
		// we can re-use prior likelihood items for them
		std::vector<double> data_lls(1, 0.0);
		for(size_t di=0;di<human_data.size() and !CTRL_C;di++) {
			
			auto idx = std::make_pair(h, di);
			
			// if we can reuse the previous data
			if(di > 0 and human_data[di].data == human_data[di-1].data) {
				data_lls.
			}
			
			std::vector<double> outval; // needed to keep parallel stuff simple
			
				// or we can copy previous data
				assert(di>0);
				size_t prevsetno = human_data[di-1].data.empty() ? 0 : human_data[di-1].data.back().setNumber; // what was the previous set number?
				
				if(human_data[di].data.back().setNumber == prevsetno) {
					// same likelihood since we presented a set at a time
					// NOTE: We don't need to check concept/list since rows are sorted, so if the condition is met, we will be the same concept/list
					outval = out[std::make_pair(h,di-1)]; 
				}
				else if(human_data[di].data.back().setNumber == prevsetno+1) {
					// we just have to add the new element, and copy the rest 
					
					// add up all of the new set (which hasn't been included)
					outval = out[std::make_pair(h,di-1)]; 			
					double next_set_ll = 0.0; // we add up the ll for the next set, so we just have one number
					for(auto& d : human_data[di].data) {
						if(d.setNumber == prevsetno+1) { // since we know the other stuff has already been included 
							next_set_ll += hypotheses[h].compute_single_likelihood(d);
						}
					}
					outval.push_back(next_set_ll);
				}
				else { 
					assert(0); // should not get here
				}
			}
			
			
			#pragma omp critical
			out[idx] = outval;		
			
		}	
	}
	
	return out;
}



size_t grammar_callback_count = 0;
void gcallback(GrammarHypothesis<MyGrammar,MyHumanDatum>& h) {
	if(++grammar_callback_count % 100 == 0) {
		COUT grammar_callback_count TAB "posterior" TAB h.posterior ENDL;
		COUT grammar_callback_count TAB "baseline" TAB h.get_baseline() ENDL;
		COUT grammar_callback_count TAB "forwardalpha" TAB h.get_forwardalpha() ENDL;
		COUT grammar_callback_count TAB  "llt" TAB h.get_decay() ENDL;
		size_t xi=0;
		for(size_t nt=0;nt<h.grammar->count_nonterminals();nt++) {
			for(size_t i=0;i<h.grammar->count_rules( (nonterminal_t) nt);i++) {
				std::string rs = h.grammar->get_rule((nonterminal_t) nt,i)->format;
		 		rs = std::regex_replace(rs, std::regex("\\%s"), "X");
				COUT grammar_callback_count TAB QQ(rs) TAB h.logA(xi) ENDL;
				xi++;
			}
		}
	}
}

////////////////////////////////////////////////////////////////////////////////////////////
#include "Top.h"
#include "Fleet.h"
#include "MCMCChain.h"

int main(int argc, char** argv){ 
	//Eigen::initParallel(); // needed to use parallel eigen
	//Eigen::setNbThreads(10);
	
	using S = std::string;
	
	Fleet fleet("An example of grammar inference for boolean concepts");
	fleet.initialize(argc, argv);
	
	MyGrammar grammar(PRIMITIVES);
	
	//------------------
	// set up the data
	//------------------
	
	std::map<std::string, MyHypothesis::data_t> learner_data;
	std::vector<MyHumanDatum> human_data;	// map conceptlist, setnumber, responsnumber to cntyes,cntno
	
	std::ifstream infile("preprocessing/data.txt");
	
	S line;
	auto theset = new std::set<MyObject>(); 
	int setidx = -1;
	while(std::getline(infile, line)) {
		auto parts = split(line, ' ');
		S conceptlist         = parts[0] + "__" + parts[1]; // concept/list pair
		size_t setNumber      = stoi(parts[2]);
		size_t responseInSet  = stoi(parts[3]);
		bool   correctAnswer  = (parts[4] == "True");
		
		// keep track of what set we're adding to
		if(setidx != -1) {
			if(setidx != (int)setNumber) {
				theset = new std::set<MyObject>(); // make a new set
			}
		}
		else {
			setidx = setNumber;
		}
		
		Shape  shape; // = magic_enum::enum_cast<Color>(parts[5]);
		if     (parts[5] == "triangle")  shape = Shape::triangle;
		else if(parts[5] == "rectangle") shape = Shape::rectangle;
		else if(parts[5] == "circle")    shape = Shape::circle;
		else assert(0);
		
		Color  color;
		if     (parts[6] == "blue")     color = Color::blue;
		else if(parts[6] == "yellow")   color = Color::yellow;
		else if(parts[6] == "green")    color = Color::green;
		else assert(0);
		
		Size  size;
		if     (parts[7] == "1")   size = Size::size1;
		else if(parts[7] == "2")   size = Size::size2;
		else if(parts[7] == "3")   size = Size::size3;
		else assert(0);

		size_t cntyes = stoi(parts[8]);
		size_t cntno  = stoi(parts[9]);

		// figure out our class
		MyObject o{shape, color, size};

		// add this to the set
		theset->insert(o);
		
		
		//
		//
		//
		//
		//
		//
		//
		//
		//
		//
		//
		//
		// TODO: THE SET IS NOT COMPUTED RIGHT HERE SINCE IT SHOULD BE EVERYTHING, NOT JUST PRECEEDING
		
		
		
		MyHypothesis::data_t di{std::make_pair(theset, o), correctAnswer, alpha};
		
		size_t ndata = learner_data[conceptlist].size(); // how many we condition on (before di is added)
		learner_data[conceptlist].push_back(di);
		
		human_data.emplace_back(&learner_data[conceptlist], &(*learner_data.rbegin()), ndata, cntyes, cntyes+cntno);
		
		
	}
	
	COUT "# Loaded data" ENDL;
	
	std::set<MyHypothesis> all;
	
	// get the keys out so we can do MCMC in parallel:
	std::vector<S> learner_data_keys;
	for(auto& v : learner_data) {
		learner_data_keys.push_back(v.first);
	}
	
	
	#pragma omp parallel for
	for(size_t vi=0; vi<learner_data.size();vi++) {
		if(!CTRL_C) {  // needed for openmp
			auto& v = learner_data[learner_data_keys[vi]];
		
			COUT "# Running " TAB learner_data_keys[vi] ENDL;
			
			for(size_t i=0;i<v.size() and !CTRL_C;i++) {
				
				TopN<MyHypothesis> top(ntop);
				
				auto di = v[i];
				MyHypothesis h0(&grammar);
				h0 = h0.restart();
				auto givendata = slice(v, 0, (di.setNumber-1)-1);
				MCMCChain chain(h0, &givendata, top);
				chain.run(Control(inner_mcmc_steps, inner_runtime)); // run it super fast
			
				#pragma omp critical
				for(auto h : top.values()) {
					h.clear_bayes(); // zero and insert
					all.insert(h);
				}
			}
		}
	}
	
	// reset control-C
	CTRL_C = 0; 

	COUT "# Done running MCMC to find hypotheses" ENDL;
	
	// Show the best we've found
	for(auto& h : all) { 
		COUT "# Hypothesis " TAB h.string() ENDL;
	}
	
	// Now let's look a bit
	std::vector<MyHypothesis> hypotheses;
	for(auto& h : all) hypotheses.push_back(h);
	COUT "# Found " TAB hypotheses.size() TAB "hypotheses" ENDL;
	
	Matrix C  = counts(hypotheses);
	COUT "# Done computing prior counts" ENDL;
	
	LL_t LL = my_compute_incremental_likelihood(hypotheses, human_data); // using MY version
	COUT "# Done computing incremental likelihoods " ENDL;
		
	auto P = model_predictions(hypotheses, human_data);
	COUT "# Done computing model predictions" ENDL;

	GrammarHypothesis<MyGrammar,MyHumanDatum> gh0(&grammar, &C, &LL, &P);
	gh0 = gh0.restart();
	
	tic();
	auto thechain = MCMCChain(gh0, &human_data, &gcallback);
	thechain.run(Control(mcmc_steps, runtime));
	tic();
	
	COUT "# Global sample count:" TAB FleetStatistics::global_sample_count ENDL;
	COUT "# Elapsed time:" TAB elapsed_seconds() << " seconds " ENDL;
	COUT "# Samples per second:" TAB FleetStatistics::global_sample_count/elapsed_seconds() ENDL;
}