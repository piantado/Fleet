
// TODO: We can use this to clean up enums below in reading the file
//#include "dependencies/magic_enum.hpp"

#include <assert.h>
#include <set>
#include <regex>
#include <vector>
#include <tuple>
#include <functional>
#include <Eigen/Dense>
#include <cmath>
#include <unsupported/Eigen/SpecialFunctions>

enum class Shape { rectangle, triangle, circle};
enum class Color { yellow, green, blue};
enum class Size  { size1, size2, size3};

typedef struct Object {
	Color color;
	Shape shape;
	Size  size;
	
	bool operator<(const Object& o) const { 
		if(color != o.color) return color < o.color;
		if(shape != o.shape) return shape < o.shape;
		if(size  != o.size)  return size  < o.size;
		return false;
	}
	bool operator==(const Object& o) const { 
		return color==o.color and shape==o.shape and size==o.size;
	}
} Object;

typedef struct LearnerDatum {
	// What the learner takes as input
	Object x;
	bool correctAnswer;
	double alpha;
	std::set<Object>* set;// a pointer makes it easy to modify as we read in the file
	size_t setNumber;
	size_t responseInSet;
	
	bool operator==(const LearnerDatum& o) const { 
		return (*set) == (*o.set) and x == o.x;
	}
} LearnerDatum; 

const double alpha = 0.9; // fixed for the learning part of the model

///~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
///~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

#define FLEET_GRAMMAR_TYPES bool,Object

///~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
///~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

#include "Primitives.h"
#include "Builtins.h"

std::tuple PRIMITIVES = {
	Primitive("and(%s,%s)",     +[](bool a, bool b) -> bool { return (a and b); }, 2.0), // optional specification of prior weight (default=1.0)
	Primitive("or(%s,%s)",      +[](bool a, bool b) -> bool { return (a or b); }),
	Primitive("iff(%s,%s)",     +[](bool a, bool b) -> bool { return (a == b); }),
	Primitive("xor(%s,%s)",     +[](bool a, bool b) -> bool { return (a xor b); }),
	Primitive("implies(%s,%s)", +[](bool a, bool b) -> bool { return ((not a) or (a and b)); }),
	Primitive("not(%s)",        +[](bool a)         -> bool { return (not a); }),
	// that + is really insane, but is needed to convert a lambda to a function pointer

	Primitive("yellow(%s)",    +[](Object x)       -> bool { return x.color == Color::yellow; }),
	Primitive("green(%s)",     +[](Object x)       -> bool { return x.color == Color::green; }),
	Primitive("blue(%s)",      +[](Object x)       -> bool { return x.color == Color::blue; }),

	Primitive("rectangle(%s)", +[](Object x)       -> bool { return x.shape == Shape::rectangle; }),
	Primitive("triangle(%s)",  +[](Object x)       -> bool { return x.shape == Shape::triangle; }),
	Primitive("circle(%s)",    +[](Object x)       -> bool { return x.shape == Shape::circle; }),
	
	Primitive("size1(%s)",     +[](Object x)       -> bool { return x.size == Size::size1; }),
	Primitive("size2(%s)",     +[](Object x)       -> bool { return x.size == Size::size2; }),
	Primitive("size3(%s)",     +[](Object x)       -> bool { return x.size == Size::size3; }),
		
	// but we also have to add a rule for the BuiltinOp that access x, our argument
	Builtin::X<Object>("x", 10.0)
};

///~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
///~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

// Includes critical files. Also defines some variables (mcts_steps, explore, etc.) that get processed from argv 
#include "Fleet.h" 
#include "EigenNumerics.h"

// Grammar hypothesis is not automatically included because it depends on Eigen
#include "GrammarHypothesis.h" 

typedef HumanDatum<LearnerDatum> MyHumanDatum;

class MyHypothesis : public LOTHypothesis<MyHypothesis,Node,Object,bool,LearnerDatum> {
public:
	// This is going to assume that all variables other than x are universally quantified over. 
	using Super = LOTHypothesis<MyHypothesis,Node,Object,bool,LearnerDatum>;
	using Super::Super;
	
	double compute_single_likelihood(const t_datum& di) {
		bool out = callOne(di.x, false);
		return (out == di.correctAnswer ? log(di.alpha + (1.0-di.alpha)/2.0) : log((1.0-di.alpha)/2.0));
	}
};


Matrix my_compute_incremental_likelihood(std::vector<MyHypothesis>& hypotheses, std::vector<MyHumanDatum>& human_data) {
	// special case of incremental likelihood since we are accumulating data (by sets)
	// So we have written a special case here
	
	Matrix out = Matrix::Zero(hypotheses.size(), human_data.size()); 
	
	for(size_t h=0;h<hypotheses.size() and !CTRL_C;h++) {
		for(size_t di=0;di<human_data.size() and !CTRL_C;di++) {
			if(human_data[di].given_data.size() == 0) {
				// should catch di=0
				out(h,di) = 0.0;
				continue;
			}
			else {
				assert(di>0);
				size_t prevsetno = human_data[di-1].given_data.empty() ? 0 : human_data[di-1].given_data.back().setNumber; // what was the previous set number?
			
				if(human_data[di].given_data.back().setNumber == prevsetno) {
					// same likelihood since we presented a set at a time
					// NOTE: We don't need to check concept/list since rows are sorted, so if the condition is met, we will be the same concept/list
					out(h,di) = out(h,di-1);
				}
				else if(human_data[di].given_data.back().setNumber == prevsetno+1) {
					// add up all of the new set (which hasn't been included)
					out(h,di) = out(h,di-1); 				
					for(auto& d : human_data[di].given_data) {
						if(d.setNumber == prevsetno+1) {
							out(h,di) += hypotheses[h].compute_single_likelihood(d);
						}
					}
				}
				else { // recompute the whole damn thing
					out(h,di) =  hypotheses[h].compute_likelihood(human_data[di].given_data);			
				}
			}
			
		}	
	}
	
	return out;
}


MyHypothesis::t_data mydata;
Fleet::Statistics::TopN<MyHypothesis> top;
Grammar grammar(PRIMITIVES);
	
// define some functions to print out a hypothesis
void print(MyHypothesis& h) {
	COUT top.count(h) TAB  h.posterior TAB h.prior TAB h.likelihood TAB QQ(h.string()) ENDL;
}

void gcallback(GrammarHypothesis<MyHypothesis,MyHumanDatum>& h) {
	
		COUT h.posterior TAB "baseline" TAB h.get_baseline() ENDL;
		COUT h.posterior TAB "forwardalpha" TAB h.get_forwardalpha() ENDL;
		size_t xi=0;
		for(size_t nt=0;nt<h.grammar->count_nonterminals();nt++) {
			for(size_t i=0;i<h.grammar->count_rules( (nonterminal_t) nt);i++) {
				std::string rs = h.grammar->get_rule((nonterminal_t)nt,i)->format;
		 		rs = std::regex_replace(rs, std::regex("\\%s"), "X");
				COUT h.posterior TAB QQ(rs) TAB h.getX()(xi) ENDL;
				xi++;
			}
		}
}

////////////////////////////////////////////////////////////////////////////////////////////

int main(int argc, char** argv){ 
	using S = std::string;
	
	// default include to process a bunch of global variables: mcts_steps, mcc_steps, etc
	auto app = Fleet::DefaultArguments("Simple grammar inference example");
	CLI11_PARSE(app, argc, argv);
	
	top.set_size(ntop); 

	Fleet_initialize(); // must happen afer args are processed since the alphabet is in the grammar
	
	//------------------
	// set up the data
	//------------------
	
	std::map<std::string, std::vector<LearnerDatum>> learner_data;
	std::vector<MyHumanDatum> human_data;	// map conceptlist, setnumber, responsnumber to cntyes,cntno
	
	std::ifstream infile("preprocessing/data.txt");
	
	S line;
	auto theset = new std::set<Object>(); 
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
				theset = new std::set<Object>(); // make a new set
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
		Object o{color, shape, size};

		// add this to the set
		theset->insert(o);
		
		LearnerDatum ld{o, correctAnswer, alpha, theset, setNumber, responseInSet};
		
		MyHumanDatum hd{cntyes, cntno, learner_data[conceptlist], ld}; // we copy learnerdata[cl] before we've loaded the next one (so it might be empty)
		human_data.push_back(hd);
		
		// add the next learning data
		learner_data[conceptlist].push_back(ld);
	}
	
	COUT "# Loaded data" ENDL;
	
	std::set<MyHypothesis> all;
	
	for(const auto& v : learner_data) {
		
		COUT "# Running " TAB v.first ENDL;
		
		for(size_t i=0;i<v.second.size();i++) {
			auto di = v.second[i];
			MyHypothesis h0(&grammar);
			h0 = h0.restart();
			auto givendata = slice(v.second, 0, (di.setNumber-1)-1);
			MCMCChain chain(h0, &givendata, top);
			chain.run(Control(100)); // run it super fast
		
			for(auto h : top.values()) {
				h.clear_bayes(); // zero and insert
				all.insert(h);
			}
			
			top.clear();
		}
	}

	COUT "# Done running MCMC" ENDL;
	
	// Show the best we've found
	for(auto h : all) { 
		COUT "# Hypothesis ";
		print(h);
	}
	
	// Now let's look a bit
	std::vector<MyHypothesis> hypotheses;
	for(auto h : all) hypotheses.push_back(h);
	COUT "# Found " TAB hypotheses.size() TAB "hypotheses" ENDL;
	
	Matrix C  = counts(hypotheses);
	COUT "# Done computing prior counts" ENDL;
	
	Matrix LL = my_compute_incremental_likelihood(hypotheses, human_data); // using MY version
	COUT "# Done computing incremental likelihoods " ENDL;
		
	auto P = model_predictions(hypotheses, human_data);
	COUT "# Done computing model predictions" ENDL;

		
	GrammarHypothesis<MyHypothesis,MyHumanDatum> gh0(&grammar, &C, &LL, &P);
	gh0 = gh0.restart();
	
	tic();
	auto thechain = MCMCChain(gh0, &human_data, &gcallback);
	thechain.run(Control(mcmc_steps, runtime));
	tic();
	
	COUT "# Global sample count:" TAB FleetStatistics::global_sample_count ENDL;
	COUT "# Elapsed time:" TAB elapsed_seconds() << " seconds " ENDL;
	COUT "# Samples per second:" TAB FleetStatistics::global_sample_count/elapsed_seconds() ENDL;
}