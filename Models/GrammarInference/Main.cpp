#include <assert.h>

// A simple example of a version of the RationalRules model. 
// This is primarily used as an example and for debugging MCMC
// My laptop gets around 200-300k samples per second

// We require an enum to define our custom operations as a string before we import Fleet
// These are the internal symbols that are used to represent each operation. The grammar
// generates nodes with them, and then dispatch down below gets called with a switch
// statement to see how to execute aech of them. 
enum class CustomOp {
	op_And, op_Or, op_Not, op_Xor, op_Iff, op_Implies,
	op_Yellow, op_Green, op_Blue, 
	op_Rectangle, op_Triangle, op_Circle,
	op_Size1, op_Size2, op_Size3	
};

enum class Shape { Rectangle, Triangle, Circle};
enum class Color { Yellow, Green, Blue};
enum class Size  { Size1, Size2, Size3 };

typedef struct Object {
	Color color;
	Shape shape;
	Size  size;
	
	// we must define this to compile because memoization requires sorting
	// but if we don't call op_MEM then we never will need it
	bool operator<(const Object& o) const { assert(false); }
	bool operator==(const Object& o) const { 
		return color==o.color and shape==o.shape and size==o.size;
	}
} Object;

// Define our types. 
#define NT_TYPES bool, Object, int
#define NT_NAMES nt_bool,nt_object, nt_value

// Includes critical files. Also defines some variables (mcts_steps, explore, etc.) that get processed from argv 
#include "Fleet.h" 
#include "GrammarInference/GrammarMCMC.h"

// Define a grammar
class MyGrammar : public Grammar { 
public:
	MyGrammar() : Grammar() {
		add( Rule(nt_object, BuiltinOp::op_X,        "x",            {},                               1.0) );		
		add( Rule(nt_bool,   CustomOp::op_Yellow,    "yellow(%s)",        {nt_object},               1.0) );		
		add( Rule(nt_bool,   CustomOp::op_Green,     "green(%s)",         {nt_object},             1.0) );
		add( Rule(nt_bool,   CustomOp::op_Blue,      "blue(%s)",          {nt_object},              1.0) );
		
		add( Rule(nt_bool,   CustomOp::op_Rectangle, "rectangle(%s)",       {nt_object},             1.0) );		
		add( Rule(nt_bool,   CustomOp::op_Triangle,  "triangle(%s)",        {nt_object},             1.0) );
		add( Rule(nt_bool,   CustomOp::op_Circle,    "circle(%s)",          {nt_object},             1.0) );
		
		add( Rule(nt_bool,   CustomOp::op_Size1,     "size1(%s)",          {nt_object},             1.0) );		
		add( Rule(nt_bool,   CustomOp::op_Size2,     "size2(%s)",          {nt_object},             1.0) );
		add( Rule(nt_bool,   CustomOp::op_Size3,     "size3(%s)",          {nt_object},             1.0) );
		
		add( Rule(nt_bool, CustomOp::op_And,         "and(%s,%s)",  {nt_bool, nt_bool},            1.0/3.) );
		add( Rule(nt_bool, CustomOp::op_Or,          "or(%s,%s)",   {nt_bool, nt_bool},            1.0/3.) );
		add( Rule(nt_bool, CustomOp::op_Not,         "not(%s)",      {nt_bool},            1.0/3.) );
		add( Rule(nt_bool, CustomOp::op_Xor,         "xor(%s,%s)",  {nt_bool, nt_bool},            1.0/3.) );
		add( Rule(nt_bool, CustomOp::op_Iff,         "iff(%s,%s)",  {nt_bool, nt_bool},            1.0/3.) );
		add( Rule(nt_bool, CustomOp::op_Implies,     "implies(%s,%s)", {nt_bool, nt_bool},            1.0/3.) );
	}
};


/* Define a class for handling my specific hypotheses and data. Everything is defaulty a PCFG prior and 
 * regeneration proposals, but I have to define a likelihood */
class MyHypothesis : public LOTHypothesis<MyHypothesis,Node,nt_bool,Object,bool> {
public:

	// I must implement all of these constructors
	MyHypothesis()                      : LOTHypothesis<MyHypothesis,Node,nt_bool,Object,bool>() {}
	MyHypothesis(Grammar* g)            : LOTHypothesis<MyHypothesis,Node,nt_bool,Object,bool>(g) {}
	MyHypothesis(Grammar* g, Node v)   : LOTHypothesis<MyHypothesis,Node,nt_bool,Object,bool>(g,v) {}
	
	double compute_single_likelihood(const t_datum& x) {
		bool out = callOne(x.input, false);
		return out == x.output ? log(x.reliability + (1.0-x.reliability)/2.0) : log((1.0-x.reliability)/2.0);
	}
	
	abort_t dispatch_rule(Instruction i, VirtualMachinePool<Object, bool>* pool, VirtualMachineState<Object,bool>& vms, Dispatchable<Object,bool>* loader ) {
		switch(i.getCustom()) {
			CASE_FUNC1(CustomOp::op_Yellow,         bool,  Object,    [](const Object& x){ return x.color == Color::Yellow; })
			CASE_FUNC1(CustomOp::op_Green,       bool,  Object,    [](const Object& x){ return x.color == Color::Green; })
			CASE_FUNC1(CustomOp::op_Blue,        bool,  Object,    [](const Object& x){ return x.color == Color::Blue; })
			
			CASE_FUNC1(CustomOp::op_Rectangle,      bool,  Object,    [](const Object& x){ return x.shape == Shape::Rectangle; })
			CASE_FUNC1(CustomOp::op_Triangle,    bool,  Object,    [](const Object& x){ return x.shape == Shape::Triangle; })
			CASE_FUNC1(CustomOp::op_Circle,      bool,  Object,    [](const Object& x){ return x.shape == Shape::Circle; })
			
			CASE_FUNC1(CustomOp::op_Size1,      bool,  Object,    [](const Object& x){ return x.size == Size::Size1; })
			CASE_FUNC1(CustomOp::op_Size2,      bool,  Object,    [](const Object& x){ return x.size == Size::Size2; })
			CASE_FUNC1(CustomOp::op_Size3,      bool,  Object,    [](const Object& x){ return x.size == Size::Size3; })
			
			CASE_FUNC2(CustomOp::op_And,         bool,bool,bool,    [](const bool a, bool b){ return a and b; })
			CASE_FUNC2(CustomOp::op_Or,          bool,bool,bool,    [](const bool a, bool b){ return a or b; })
			CASE_FUNC1(CustomOp::op_Not,         bool,bool,         [](const bool a){ return  not a; })
			CASE_FUNC2(CustomOp::op_Xor,         bool,bool,bool,    [](const bool a, bool b){ return (a xor b); })
			CASE_FUNC2(CustomOp::op_Iff,         bool,bool,bool,    [](const bool a, bool b){ return (a==b); })
			CASE_FUNC2(CustomOp::op_Implies,     bool,bool,bool,    [](const bool a, bool b){ return (not a) or (a and b); })
			
			default:
				assert(0 && " *** You used an invalid operation"); // should never get here
		}
		return abort_t::NO_ABORT;
	}
};


// mydata stores the data for the inference model
MyHypothesis::t_data mydata;
// top stores the top hypotheses we have found
TopN<MyHypothesis> top;
MyGrammar grammar;
	

// define some functions to print out a hypothesis
void print(MyHypothesis& h) {
	COUT top.count(h) TAB  h.posterior TAB h.prior TAB h.likelihood TAB QQ(h.string()) ENDL;
}

// This gets called on every sample -- here we add it to our best seen so far (top) and
// print it every thin samples unless thin=0
void callback(MyHypothesis& h) {
	top << h; // add to the top
}


#include "LoadData.h"

////////////////////////////////////////////////////////////////////////////////////////////

int main(int argc, char** argv){ 
	
	// default include to process a bunch of global variables: mcts_steps, mcc_steps, etc
	auto app = Fleet::DefaultArguments();
	CLI11_PARSE(app, argc, argv);
	
	top.set_size(ntop); // set by above macro

	Fleet_initialize(); // must happen afer args are processed since the alphabet is in the grammar
	
	//------------------
	// set up the data
	//------------------
	
	auto bigdata = load_SFL_data_file("preprocessing/data.txt");
	CERR "# Loaded data" ENDL;
	
	std::set<MyHypothesis> all;
	
	for(auto v : bigdata) {
		
		CERR "# Running " TAB v.first ENDL;
		
		
		
//		for(size_t i=0;i<v.second.size();i++) {
		for(size_t i=0;i<25;i++) {
			MyHypothesis::t_datum di = v.second[i];
			MyHypothesis h0(&grammar);
			h0 = h0.restart();
			auto givendata = slice(v.second, 0, (di.setnumber-1)-1);
			MCMCChain<MyHypothesis> chain(h0, &givendata, callback);
			chain.run(100, 0); // run it super fast
		
			for(auto h : top.values()) {
				h.clear_bayes(); // zero and insert
				all.insert(h);
			}
			
			top.clear();
		}
		
		
		
		
		
		
		
		
		
		
		
		
		
		
		
		
		
		
		
		
		
		
		
		
		
		
		
		break;
		
	}

	CERR "# Done running MCMC" ENDL;
	
	// Show the best we've found
	for(auto h : all) { 
		COUT "# Hypothesis ";
		print(h);
	}
	
	
	// Now let's look a bit
	std::vector<MyHypothesis> hypotheses;
	for(auto h : all) hypotheses.push_back(h);
	CERR "# Found " TAB hypotheses.size() TAB "hypotheses" ENDL;
	
	
	// Now we go through and vectorize everything
	std::vector<MyHypothesis::t_data>   given_data;
	std::vector<MyHypothesis::t_datum>  predict_data;
	std::vector<size_t> yes_responses;
    std::vector<size_t> no_responses;
	for(auto v : bigdata) {
		
//		for(size_t i=0;i<v.second.size();i++) {
		for(size_t i=0;i<25;i++) { // look at hte first few sets, that's all
			MyHypothesis::t_datum di = v.second[i];
			
			// the data we've seen is only to setnumber minus 1
			given_data.push_back(slice(v.second, 0, (di.setnumber-1)-1)); // setnumber is 1-indexed
			
			predict_data.push_back(di);
			yes_responses.push_back(di.cntyes);
			no_responses.push_back(di.cntno);
		}
	}
	CERR "# Done vectorizing" ENDL;
	
	
	simpleGrammarMCMC(hypotheses, given_data, predict_data, yes_responses, no_responses);
	
	
	
	COUT "# Global sample count:" TAB FleetStatistics::global_sample_count ENDL;
	COUT "# Elapsed time:" TAB elapsed_seconds() << " seconds " ENDL;
	COUT "# Samples per second:" TAB FleetStatistics::global_sample_count/elapsed_seconds() ENDL;
	
}