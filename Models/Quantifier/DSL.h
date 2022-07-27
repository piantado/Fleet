#pragma once 

namespace DSL { 
	
	// we define these functions so that they can be used in definitions and in the grammar
	const auto card_gt   = +[](Set x, Set y) -> bool { return x.size()  > y.size(); };
	const auto card_eq   = +[](Set x, Set y) -> bool { return x.size() == y.size(); };
	const auto empty     = +[](Set x) -> bool { return x.size() == 0; };	
	
	// some "generic" sets
	const auto singleton = +[](Set x) -> bool { return x.size() == 1; };
	const auto doubleton = +[](Set x) -> bool { return x.size() == 2; };
	const auto tripleton = +[](Set x) -> bool { return x.size() == 3; };
	
	const auto eq = +[](Set x, Set y) -> bool { return x==y; };
	
	const auto presup = +[](bool x, bool y) -> TruthValue { 
		if(x) return (y ? TruthValue::True : TruthValue::False);
		else return TruthValue::Undefined;
	};
	
	const auto context = +[](Utterance u) -> Set  { return u.context; };
	const auto shape   = +[](Utterance u) -> Set  { return u.shape; };
	const auto color   = +[](Utterance u) -> Set  { return u.color; };
	
	const auto emptyset = +[]() -> Set      { return Set(); };
	
	const auto subset = +[](Set x, Set y) -> bool { 
		return std::includes(y.begin(), y.end(), x.begin(), x.end()); // x \subset y
	};
	
	const auto intersection = +[](Set x, Set y) -> Set            { 
		Set out;
		std::set_intersection(x.begin(), x.end(), y.begin(), y.end(), std::inserter(out, out.begin()));
		return out; 
	};
	
	const auto myunion = +[](Set x, Set y) -> Set            { 
		Set out;
		std::set_union(x.begin(), x.end(), y.begin(), y.end(), std::inserter(out, out.begin()));
		return out; 
	};
	
	const auto complement = +[](Set x, Utterance u) -> Set            { 
		Set out;
		std::set_difference(u.context.begin(), u.context.end(), x.begin(), x.end(), std::inserter(out, out.begin()));
		return out; 
	};
	
	const auto difference = +[](Set x, Set y) -> Set            { 
		Set out;
		std::set_difference(x.begin(), x.end(), y.begin(), y.end(), std::inserter(out, out.begin()));
		return out; 
	};
	
	const auto filter_color = +[](MyColor c, Set x) -> Set            {
		Set out;
		for(auto& v : x) {
			if(v.get<MyColor>() == c) 
				out.insert(v);
		}
		return out; 
	};
	
	const auto filter_shape = +[](MyShape s, Set x) -> Set            {
		Set out;
		for(auto& v : x) {
			if(v.get<MyShape>() == s) 
				out.insert(v);
		}
		return out; 
	};
}
