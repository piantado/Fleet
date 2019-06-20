//  TODO: Update ANS= to be something reasonable!


// We require a macro to define our ops as a string BEFORE we import Fleet
// these get incorporated into the op_t type
enum CustomOp {
		op_Object, op_Word, op_U, op_Magnitude,\
		op_Next,op_Prev,op_Xset,op_Xtype,op_MakeX,\
		op_Union,op_Intersection,op_Difference,op_Select,op_SelectObj,op_Filter,\
		op_And,op_Or,op_Not,\
		op_Match1to1,op_WM0,op_WM1,op_WM2,op_WM3,\
		op_ApproxEq_S_S,op_ApproxEq_S_M,op_ApproxEq_S_W,\
		op_ApproxLt_S_S,op_ApproxLt_S_M,op_ApproxLt_S_W
};
		

#include "Primitives.h"

// Defie our types. 
#define NT_TYPES bool,    Model::set,    Model::objtype, Model::word,  Model::X, Model::wmset, Model::magnitude,  double
#define NT_NAMES nt_bool, nt_set,         nt_type,       nt_word,       nt_X,    nt_wmset,     nt_magnitude,      nt_double
///               0         1                2              3             4          5               6               7

#include <vector>

std::vector<Model::objtype>      OBJECTS = {'a', 'b', 'c', 'd', 'e', 'f', 'g', 'h', 'i', 'j'};
std::vector<std::string>           WORDS = {"one", "two", "three", "four", "five", "six", "seven", "eight", "nine", "ten"};
std::vector<Model::magnitude> MAGNITUDES = {1,2,3,4,5,6,7,8,9,10};

// TODO: UPDATE WITH REAL DATA  -- From Gunderson & Levine?
#include <random>
std::discrete_distribution<> number_distribution({0, 7187, 1484, 593, 334, 297, 165, 151, 86, 105, 112}); // 0-indexed
	
std::vector<int> data_amounts = {1, 2, 3, 4, 5, 10, 15, 20, 25, 30, 40, 50, 60, 70, 80, 90, 100, 125, 150, 200, 250, 300, 350, 400, 500, 600, 700, 800, 900, 1000};

const size_t MAX_NODES = 50;
double recursion_penalty = -20.0;

// Includes critical files. Also defines some variables (mcts_steps, explore, etc.) that get processed from argv 
#include "Fleet.h" 

class MyGrammar : public Grammar { 
public:
	MyGrammar() : Grammar() {
		
		for(size_t i=0;i<OBJECTS.size();i++)
			add( new Rule(nt_type, op_Object,        std::string(1,OBJECTS[i]),        {},                1.0/OBJECTS.size(), i) );		
		
		for(size_t i=0;i<WORDS.size();i++)
			add( new Rule(nt_word, op_Word,             WORDS[i],             {},                         1.0/WORDS.size(), i+1) );		
		
		add( new Rule(nt_word, op_U,              "undef",         {},                      			   5.0) );	
		add( new Rule(nt_word, op_Next,           "next(%s)",      {nt_word},                      		   1.0) );	
		add( new Rule(nt_word, op_Prev,           "prev(%s)",      {nt_word},                      	       1.0) );	
		
		// extracting from x
		add( new Rule(nt_set,    op_Xset,        "set(%s)",                {nt_X},                         10.0) );		
		add( new Rule(nt_type,   op_Xtype,       "type(%s)",               {nt_X},                         10.0) );		
		add( new Rule(nt_X,      BuiltinOp::op_X,"X",                      {},                             10.0) );		
		
		add( new Rule(nt_X,      op_MakeX,        "<%s,%s>",              {nt_set, nt_type},              1.0) );		
		add( new Rule(nt_word,   BuiltinOp::op_RECURSE,      "recurse(%s)",          {nt_X},              5.0) );		

		add( new Rule(nt_set,    op_Union,        "union(%s,%s)",         {nt_set,nt_set},            1.0/3.0) );
		add( new Rule(nt_set,    op_Intersection, "intersection(%s,%s)",  {nt_set,nt_set},            1.0/3.0) );
		add( new Rule(nt_set,    op_Difference,   "difference(%s,%s)",    {nt_set,nt_set},            1.0/3.0) );
		add( new Rule(nt_set,    op_Filter,       "filter(%s,%s)",        {nt_type,nt_set},           1.0) );
		
		add( new Rule(nt_set,    op_Select,       "select(%s)",           {nt_set},                   1.0/2.0) );
		add( new Rule(nt_set,    op_SelectObj,    "selectO(%s,%s)",       {nt_set,nt_type},           1.0/2.0) );

		
		add( new Rule(nt_set,    BuiltinOp::op_IF,           "ifS(%s,%s,%s)", {nt_bool, nt_set,  nt_set},       1.0/2.0) );
		add( new Rule(nt_word,   BuiltinOp::op_IF,           "ifW(%s,%s,%s)", {nt_bool, nt_word, nt_word},      1.0/2.0) );
		
		
		add( new Rule(nt_bool,   BuiltinOp::op_FLIPP,       "flip(%s)",     {nt_double},               5.0) );
		add( new Rule(nt_bool,   op_And,         "and(%s,%s)",   {nt_bool, nt_bool},               1.0) );
		add( new Rule(nt_bool,   op_Or,          "or(%s,%s)",    {nt_bool, nt_bool},               1.0) );
		add( new Rule(nt_bool,   op_Not,         "not(%s)",   {nt_bool},                        1.0) );
		
		// Working memory model		
		add( new Rule(nt_bool,   op_Match1to1,   "match1to1(%s,%s)", {nt_wmset, nt_set},            5.0) );
		add( new Rule(nt_wmset,  op_WM0,   "{}",                     {},                1.0) );
		add( new Rule(nt_wmset,  op_WM1,   "{o}",                    {},                1.0) );
		add( new Rule(nt_wmset,  op_WM2,   "{o,o}",                  {},              1.0) );
		add( new Rule(nt_wmset,  op_WM3,   "{o,o,o}",                {},            1.0) );		

		// approximate model
		add( new Rule(nt_double,   op_ApproxEq_S_S,  "ANS=(%s,%s)",  {nt_set,   nt_set},            1.0/3.0) );
		add( new Rule(nt_double,   op_ApproxEq_S_W,  "ANS=(%s,%s)",  {nt_set,   nt_wmset},          1.0/3.0) );
		add( new Rule(nt_double,   op_ApproxEq_S_M,  "ANS=(%s,%s)",  {nt_set,   nt_magnitude},      1.0/3.0) );
		add( new Rule(nt_double,   op_ApproxLt_S_S,  "ANS<(%s,%s)",  {nt_set,   nt_set},            1.0/3.0) );
		add( new Rule(nt_double,   op_ApproxLt_S_W,  "ANS<(%s,%s)",  {nt_set,   nt_wmset},          1.0/3.0) );
		add( new Rule(nt_double,   op_ApproxLt_S_M,  "ANS<(%s,%s)",  {nt_set,   nt_magnitude},      1.0/3.0) );
	
		for(size_t i=0;i<MAGNITUDES.size();i++)
			add( new Rule(nt_magnitude,  op_Magnitude,   std::to_string(MAGNITUDES[i]).substr(0,3), {},           10.0/MAGNITUDES.size(), i)); // magnitudes that only get used in ANS comparisons
		
		
	}
};


/* Define a class for handling my specific hypotheses and data. Everything is defaulty a PCFG prior and 
 * regeneration proposals, but I have to define a likelihood */
class MyHypothesis : public LOTHypothesis<MyHypothesis,Node,nt_word,Model::X,Model::word> {
public:
	
	// I must implement all of these constructors
	MyHypothesis(Grammar* g)            : LOTHypothesis<MyHypothesis,Node,nt_word,Model::X,Model::word>(g) {}
	MyHypothesis(Grammar* g, Node* v)   : LOTHypothesis<MyHypothesis,Node,nt_word,Model::X,Model::word>(g,v) {}
	
	size_t recursion_count() {
		// how many times do I use recursion?
		std::function<size_t(const Node*)> f = [](const Node* n) { 
			return (size_t)1*(n->rule->instr.is_a(BuiltinOp::op_RECURSE)); 
		};
		return value->sum<size_t>(f);
	}
	
	
	double compute_prior() {
		if(value->count() > MAX_NODES) 
			prior = -infinity;
		else 
			prior = LOTHypothesis<MyHypothesis,Node,nt_word,Model::X,Model::word>::compute_prior();
			
		// include recusion penalty
		prior += ( recursion_count() > 0 ? recursion_penalty : log(1.0-exp(recursion_penalty))); 
		
		return prior;
	}
	
	double compute_single_likelihood(const t_datum& d) {
		auto v = call(d.input, Model::U, this, 64, 64); // something of the type
		
		double pU      = (v.count(Model::U) ? exp(v[Model::U]) : 0.0); // non-log probs
		double pTarget = (v.count(d.output) ? exp(v[d.output]) : 0.0);
		
		// average likelihood when sampling from this posterior
		return log( pU*(1.0/10.0) + (1.0-d.reliability)/10.0 + d.reliability*pTarget );
	}	

	abort_t dispatch_rule(Instruction i,  VirtualMachinePool<Model::X, Model::word>* pool, VirtualMachineState<Model::X, Model::word>* vms, Dispatchable<Model::X, Model::word>* loader) {
		/* Dispatch the functions that I have defined. Returns true on success. 
		 * Note that errors might return from this 
		 * */
		switch(i.getCustom()) {
			CASE_FUNC0(op_Object,      Model::objtype, [i](){ return OBJECTS[i.arg]; })
			
			CASE_FUNC0(op_Word,        Model::word, [i](){ return i.arg; })
			CASE_FUNC0(op_U,           Model::word, [](){ return Model::U; })
			
			CASE_FUNC1(op_Next,        Model::word,  Model::word, Model::next )
			CASE_FUNC1(op_Prev,        Model::word,  Model::word, Model::prev )
			
			CASE_FUNC1(op_Xset,        Model::set,  Model::X, [](const Model::X x) { return std::get<0>(x); })
			CASE_FUNC1(op_Xtype,       Model::objtype, Model::X, [](const Model::X x) { return std::get<1>(x); })
			
			CASE_FUNC2(op_MakeX,         Model::X, Model::set, Model::objtype, [](const Model::set s, const Model::objtype t) { return std::make_tuple(s,t); })
			CASE_FUNC2(op_Union,         Model::set, Model::set, Model::set,     Model::myunion)
			CASE_FUNC2(op_Intersection,  Model::set, Model::set, Model::set,     Model::intersection)
			CASE_FUNC2(op_Difference,    Model::set, Model::set, Model::set,     Model::difference)
			CASE_FUNC1(op_Select,        Model::set, Model::set,                 Model::select)
			CASE_FUNC2(op_SelectObj,     Model::set, Model::set, Model::objtype, Model::selectObj)
			CASE_FUNC2(op_Filter,        Model::set, Model::set, Model::objtype, Model::filter)
			
			CASE_FUNC2(op_And,           bool, bool, bool, [](const bool x, const bool y) { return x&&y;} )
			CASE_FUNC2(op_Or,            bool, bool, bool, [](const bool x, const bool y) { return x||y;} )
			CASE_FUNC1(op_Not,           bool, bool,       [](const bool x) { return !x;} );
			
			CASE_FUNC2(op_Match1to1,     bool, Model::wmset, Model::set,  Model::match1to1)
			CASE_FUNC0(op_WM0,           Model::wmset, [](){ return 0; })
			CASE_FUNC0(op_WM1,           Model::wmset, [](){ return 1; })
			CASE_FUNC0(op_WM2,           Model::wmset, [](){ return 2; })
			CASE_FUNC0(op_WM3,           Model::wmset, [](){ return 3; })
			
			CASE_FUNC2(op_ApproxEq_S_S,  double, Model::set, Model::set,        Model::ANS_Eq)
			CASE_FUNC2(op_ApproxEq_S_W,  double, Model::set, Model::wmset,      Model::ANS_Eq)
			CASE_FUNC2(op_ApproxEq_S_M,  double, Model::set, Model::magnitude,  Model::ANS_Eq)
			CASE_FUNC2(op_ApproxLt_S_S,  double, Model::set, Model::set,        Model::ANS_Lt)
			CASE_FUNC2(op_ApproxLt_S_W,  double, Model::set, Model::wmset,      Model::ANS_Lt)
			CASE_FUNC2(op_ApproxLt_S_M,  double, Model::set, Model::magnitude,  Model::ANS_Lt)

			CASE_FUNC0(op_Magnitude,    Model::magnitude, [i](){ return MAGNITUDES[i.arg]; })

			default:
				assert(0); // should never get here
		}
		return abort_t::NO_ABORT;
	}
};

const double alpha = 0.9;

MyHypothesis::t_data mydata;
TopN<MyHypothesis> top;
TopN<MyHypothesis> all;
std::mutex output_mutex;

void print(MyHypothesis& h, std::string prefix) {
	std::lock_guard<std::mutex> mutex(output_mutex);
	
    COUT prefix << mydata.size() TAB top.count(h) TAB h.posterior TAB h.prior TAB h.likelihood << "\t";
	
    for (int j = 1; j <= 9; j++) {
        Model::set theset = "" + std::string(j,'Z');
		auto out = h.call(Model::X(theset,'Z'), Model::U);
		Model::word v = out.argmax();
		
        if(v < 0) COUT "U"; // must be undefined
        else COUT (int) v;
		if(j < 9) COUT ".";
    }
	COUT "\t";
    
    for (int j = 1; j <= 9; j++) {
        Model::set theset = "ABBCCCDDDDEEEEE" + std::string(j,'Z');        
        auto out = h.call(Model::X(theset,'Z'), Model::U);
        Model::word v = out.argmax();
		
		if(v < 0) COUT "U"; // must be undefined
        else COUT (int) v;
		if(j < 9) COUT ".";
    }
    
	COUT "\t" << h.recursion_count() TAB QQ(h.string()) ENDL;
}
void print(MyHypothesis& h) {
	print(h, std::string(""));
}

void callback(MyHypothesis* h) {
	
	// let's print each time we find a new best
	if(h->posterior > top.best_score())
		print(*h, "#TOP\t"); 
	
	top << *h; 
	
	FleetStatistics::global_sample_count++;
	
	// print out with thinning
	if(thin > 0 && FleetStatistics::global_sample_count % thin == 0) 
		print(*h);
}


double playouts(const MyHypothesis* h0) {
	if(h0->is_evaluable()) { // it has no gaps, so we don't need to do any structural search or anything
		auto h = h0->copy();
		h->compute_posterior(mydata);
		callback(h);
		double v = h->posterior;
		delete h;
		return v;
	}
	else { // we have to do some more search
		auto h = h0->copy_and_complete();
		auto q = MCMC(h, mydata, callback, mcmc_steps, mcmc_restart, true);
		double v = q->posterior;
		//std::cerr << v << "\t" << h0->string() << std::endl;
		delete q;
		return v;
	}
}


////////////////////////////////////////////////////////////////////////////////////////////


int main(int argc, char** argv){ 
	using namespace std;
	
	// default include to process a bunch of global variables: mcts_steps, mcc_steps, etc
	FLEET_DECLARE_GLOBAL_ARGS()
	CLI11_PARSE(app, argc, argv);
	top.set_size(ntop); // set by above macro
	
	Fleet_initialize();
	
	MyGrammar grammar;
	tic();
	
	// Set up the data
	for(auto ndata : data_amounts) {
		if(CTRL_C) break;
		
		mydata.clear();
		CERR "# Running on data " << ndata ENDL;
		
		// make some data here
		std::uniform_int_distribution<> random_ntypes(1,3); // how many objects present?
		std::uniform_int_distribution<> random_object(1,OBJECTS.size()-1);
		std::uniform_int_distribution<> random_word(1,10);
		for(int i=0;i<ndata;i++) {
			Model::set s = ""; Model::word w = Model::U; Model::objtype t;  // Set, word, and type (w initialized to U just to suppress warnings)
			
			int ntypes=0;
			int NT = random_ntypes(rng);
			for(int i=0;i<NT;i++) {
				int nx = number_distribution(rng);  // sample how many things we'll do
				Model::objtype tx;
				do { 
					tx = OBJECTS[random_object(rng)];
				} while(s.find(std::string(1,tx)) != std::string::npos); // keep trying until we find an unused one
				
				s.append(std::string(nx,tx));
				
				if(i==0) { // first time captures the intended type
					if(uniform(rng) < 1.0-alpha) w = static_cast<Model::word>(random_word(rng));  // are we noise?
					else       				     w = nx; // not noise

					t = tx; // the type is never considered to be noise here
				}
				
				ntypes++;     
			}
			std::random_shuffle( s.begin(), s.end() ); // randomize the order so select is not such a friendly strategy

			Model::X x(s, t);
			
			// make the data point
			mydata.push_back(MyHypothesis::t_datum({x, w, alpha}));
		}    		
		
		
//		Node* n = grammar.expand_from_names<Node>("ifW:match:{o}:filter:type:X:set:X:one:next:recurse:<%s,%s>:difference:set:X:selectO:set:X:type:X:type:X");
//		auto h0 = new MyHypothesis(&grammar, n);	
//		h0->compute_posterior(mydata);
//		callback(h0);
		
		auto h0 = new MyHypothesis(&grammar);
		ParallelTempering<MyHypothesis> samp(h0, &mydata, callback, 8, 1000.0, false);
		tic();
		samp.run(mcmc_steps, runtime, 200, 3000); //30000);		
		tic();

		// and save what we found
		all << top;
		
		top.clear();
	}

	// print out at the end
	for(auto h : all.values()) {
		h.compute_posterior(mydata); // run on the largest data amount
		print(h);
	}
	
	tic();
	
	COUT "# Global sample count:" TAB FleetStatistics::global_sample_count ENDL;
	COUT "# Elapsed time:" TAB elapsed_seconds() << " seconds " ENDL;
	COUT "# Samples per second:" TAB FleetStatistics::global_sample_count/elapsed_seconds() ENDL;

	all.clear(); // clear up 
	
}
