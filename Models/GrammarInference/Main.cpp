

// TODO: Might be easier to have responses be to the entire list of objects?

#include <assert.h>
#include <set>
#include <string>
#include <tuple>
#include <regex>
#include <vector>
#include <tuple>
#include <utility>
#include <functional>
#include <cmath>

#include "EigenLib.h"

#include "Object.h"

using S=std::string;

const double alpha = 0.9; // fixed for the learning part of the model

enum class Shape { rectangle, triangle, circle};
enum class Color { yellow, green, blue};
enum class Size  { size1, size2, size3};

// Define a kind of object with these features
// this is just a Fleet::Object, but with string constructors for simplicity
struct MyObject : public Object<Shape,Color,Size>  {
	
	MyObject() {}
	
	MyObject(S _shape, S _color, S _size) {
		// NOT: we could use magic_enum, but haven't here to avoid a dependency
		if     (_shape == "triangle")   this->set(Shape::triangle);
		else if(_shape == "rectangle")  this->set(Shape::rectangle);
		else if(_shape == "circle")     this->set(Shape::circle);
		else assert(0);
		
		if     (_color == "blue")     this->set(Color::blue);
		else if(_color == "yellow")   this->set(Color::yellow);
		else if(_color == "green")    this->set(Color::green);
		else assert(0);
		
		if     (_size == "1")        this->set(Size::size1);
		else if(_size == "2")        this->set(Size::size2);
		else if(_size == "3")        this->set(Size::size3);
		else assert(0);
	}
	
};

// An input here is a set of objects and responses
typedef std::pair<std::vector<MyObject>, MyObject> MyInput;
typedef bool                                       MyOutput;

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
	
	Primitive("%s.arg",         +[](MyInput x)       -> MyObject { return x.second; }),
		
	// but we also have to add a rule for the BuiltinOp that access x, our argument
	Builtin::X<MyInput>("x", 10.0)
};

///~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

#include "Grammar.h"

class MyGrammar : public Grammar<bool,MyObject,MyInput> {
	using Super=Grammar<bool,MyObject,MyInput>;
	using Super::Super;
};

///~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// Includes critical files. Also defines some variables (mcts_steps, explore, etc.) that get processed from argv 

#include "GrammarHypothesis.h" 
#include "LOTHypothesis.h"


/// The type here is a little complex -- a LOTHypothesis here is type MyObject->bool even though
//  but the datum format we use is these sets, MyInput, MyOutput
class MyHypothesis final : public LOTHypothesis<MyHypothesis,MyInput,MyOutput,MyGrammar> {
public:
	// This is going to assume that all variables other than x are universally quantified over. 
	using Super = LOTHypothesis<MyHypothesis,MyInput,MyOutput,MyGrammar>;
	using Super::Super;
	using output_t = MyOutput; // needed since inheritance doesn't get these?
	
	double compute_single_likelihood(const datum_t& di) override {
		bool o = callOne(di.input, false);
		return log( (1.0-di.reliability)/2.0 + (o == di.output)*di.reliability);
	}
};

size_t grammar_callback_count = 0;
void gcallback(GrammarHypothesis<MyHypothesis>& h) {
	if(++grammar_callback_count % 100 == 0) {
		COUT grammar_callback_count TAB "posterior" TAB h.posterior ENDL;
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
#include "Miscellaneous.h"
#include "MCMCChain.h"

/**
 * @brief This functions reads our data file format and returns a vector with the human data by each set item. 
 * 			auto [objs, corrects, yeses, nos] = get_next_human_data(f)
 * @param f
 */
std::tuple<std::vector<MyObject>*,
		   std::vector<bool>*,
		   std::vector<size_t>*,
		   std::vector<size_t>*,
		   std::string> get_next_human_data(std::ifstream& fs) {
		
	auto objs     = new std::vector<MyObject>();
	auto corrects = new std::vector<bool>();
	auto yeses    = new std::vector<size_t>();
	auto nos      = new std::vector<size_t>();
	S    conceptlist;
	
	S line;
	int prev_setNumber = -1;
	while(true) { 
		
		auto pos = fs.tellg(); // remember where we were since when we get the next set, we seek back
		
		// get the line
		auto& b = std::getline(fs, line);

		if(not b) { // if end of file, return
			return std::make_tuple(objs,corrects,yeses,nos,conceptlist);
		}
		else {
			// else break up the line an dprocess
			auto parts          = split(line, ' ');
			conceptlist    = S(parts[0]) + S(parts[1]);
			int  setNumber      = std::stoi(parts[2]);
			bool correctAnswer  = (parts[4] == "True");
			MyObject o{parts[5], parts[6], parts[7]};
			size_t cntyes = std::stoi(parts[8]);
			size_t cntno  = std::stoi(parts[9]);
			
			// if we are on a new set, then we seek back and return
			if(prev_setNumber != -1 and setNumber != prev_setNumber) {
				fs.seekg(pos ,std::ios_base::beg); // so next time this gets called, it starts at the right set
				return std::make_tuple(objs,corrects,yeses,nos,conceptlist);
			}
			else {
				// else we are still building this set
				objs->push_back(o);
				corrects->push_back(correctAnswer);
				yeses->push_back(cntyes);
				nos->push_back(cntno);						
			}
			
			prev_setNumber = setNumber;
		}
		
	}
			   
}

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
	
	std::vector<HumanDatum<MyHypothesis>> human_data;
	
	// We need to read the data and collapse same setNumbers and concepts to the same "set", since this is the new
	// data format for the model. 
	std::ifstream infile("preprocessing/data.txt");
	
	S prev_conceptlist = ""; // what was the previous concept/list we saw? 
	MyHypothesis::data_t* learner_data = nullptr; // pointer to a vector of learner data
	
	// what data do I run mcmc on? not just human_data since that will have many reps
	// NOTE here we store only the pointers to sequences of data, and the loop below loops
	// over items. This lets us preserve good hypotheses across amounts of data
	// but may not be as great at using parallel cores. 
	std::vector<MyHypothesis::data_t*> mcmc_data; 
	
	size_t ndata = 0;
	
	while(! infile.eof() ) {
		if(CTRL_C) break;
		
		// use the helper above to read the next chunk of human data 
		// (and put the file pointer back to the start of the next chunk)
		auto [objs, corrects, yeses, nos, conceptlist] = get_next_human_data(infile);
		// could assert that the sizes of these are all the same
		
		// if we are on a new conceptlist, then make a new learner data (on stack)
		if(conceptlist != prev_conceptlist) {
			if(learner_data != nullptr) 
				mcmc_data.push_back(learner_data);
			
			// need to reserve enough here so that we don't have to move -- or else the pointers break
			learner_data = new MyHypothesis::data_t(512);		
			ndata = 0;
		}
		
		// now put the relevant stuff onto learner_data
		for(size_t i=0;i<objs->size();i++) {
			MyInput inp{*objs, (*objs)[i]};
			learner_data->emplace_back(inp, (*corrects)[i], alpha);
		}
				
		// now unpack this data into conceptdata, which requires mapping it to 
		// HumanDatum format
		for(size_t i=0;i<objs->size();i++) {
			// make a map of the responses
			std::map<bool,size_t> m; m[true] = (*yeses)[i]; m[false] = (*nos)[i];
			
			HumanDatum<MyHypothesis> hd{learner_data, ndata, &((*learner_data)[ndata+i]), std::move(m), 0.5};
		
			human_data.push_back(std::move(hd));
		}
		
		ndata += objs->size();
		prev_conceptlist = conceptlist;
	}
	//if(learner_data != nullptr) 
	//	mcmc_data.push_back(learner_data); // and add that last dataset
	
	COUT "# Loaded data" ENDL;
	
	
	std::set<MyHypothesis> all;	
	
	#pragma omp parallel for
	for(size_t vi=0; vi<5;vi++) {
	//	for(size_t vi=0; vi<mcmc_data.size();vi++) {
		if(!CTRL_C) {  // needed for openmp
		
			#pragma omp critical
			{
			COUT "# Running " TAB vi TAB " of " TAB mcmc_data.size() ENDL;
			}
			
			for(size_t i=0;i<mcmc_data[vi]->size() and !CTRL_C;i++) {
				
				TopN<MyHypothesis> top(ntop);
				
				MyHypothesis h0(&grammar);
				h0 = h0.restart();
				auto givendata = slice(*(mcmc_data[vi]), 0, i);
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
	
	LL_t LL = compute_incremental_likelihood(hypotheses, human_data); // using MY version
	COUT "# Done computing incremental likelihoods " ENDL;
		
	auto P = model_predictions(hypotheses, human_data);
	COUT "# Done computing model predictions" ENDL;

	GrammarHypothesis<MyHypothesis> gh0(&grammar, &C, &LL, &P);
	gh0 = gh0.restart();
	
	tic();
	auto thechain = MCMCChain(gh0, &human_data, &gcallback);
	thechain.run(Control(mcmc_steps, runtime));
	tic();
	
	COUT "# Global sample count:" TAB FleetStatistics::global_sample_count ENDL;
	COUT "# Elapsed time:" TAB elapsed_seconds() << " seconds " ENDL;
	COUT "# Samples per second:" TAB FleetStatistics::global_sample_count/elapsed_seconds() ENDL;
}