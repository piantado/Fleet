


std::vector<MyHumanDatum> load_human_data(std::string&& path, std::vector<MyHypothesis::data_t*>* mcmc_data) {
	
	std::vector<MyHumanDatum> out; 
	
	for(auto [row, stimulus, all_responses] : read_csv<3>(path, '\t')) {
		
		// split up stimulus into components
		auto data = new MyHypothesis::data_t();
		auto decay_pos = new std::vector<int>(); 
		int i=0;
		for(auto& s : string_to<std::vector<S>>(stimulus)) { // convert to a string vector and then to data
			data->push_back(MyHypothesis::datum_t{.input=EMPTY_STRING, .output=s});
			decay_pos->push_back(i++);
			
			// add a check that we're using the right alphabet here
			check_alphabet(s, alphabet);
		}
		size_t ndata = data->size(); // note that below we might change data's pointer, but we still need this length
			
		// process all_responses into a map from strings to counts
		auto m = string_to<std::vector<std::pair<std::string,unsigned long>>>(all_responses);

		// if we are also storing mcmcdata
		if(mcmc_data != nullptr) {
			// This glom thing will use anything overlapping in mcmc_data, and give
			// us a new pointer if it can. This decreases the amount we need to run MCMC search
			// on and saves memory; NOTE: This (often) changes data
			glom(*mcmc_data, data);
		}

		// now just put into the data
		out.push_back(MyHumanDatum{.data=data, 
								   .ndata=ndata, 
								   .predict=nullptr, // const_cast<S*>(&EMPTY_STRING), 
								   .responses=m,
								   .chance=NaN, // should not be used via human_chance_lp above
								   .decay_position=decay_pos,
								   .decay_index=i
								});
	}
	
	return out; 
	
}