#pragma once 

#include <tuple>
#include <vector>
#include <iostream>

/**
 * @brief This functions reads our data file format and returns a vector with the human data by each set item. 
 * 			auto [objs, corrects, yeses, nos] = get_next_human_data(f)
 * @param fs 
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
	std::string conceptlist = ""; 
	
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
			auto parts           = split(line, ' ');
			auto new_conceptlist = S(parts[0]) + S(parts[1]);
			int  setNumber       = std::stoi(parts[2]);
			bool correctAnswer   = (parts[4] == "True");
			MyObject o{parts[5], parts[6], parts[7]};
			size_t cntyes = std::stoi(parts[8]);
			size_t cntno  = std::stoi(parts[9]);
			
			// if we are on a new set, then we seek back and return
			if(prev_setNumber != -1 and setNumber != prev_setNumber) {
				fs.clear(); // must clear the EOF bit
				fs.seekg(pos, std::ios_base::beg); // so next time this gets called, it starts at the right set
				return std::make_tuple(objs,corrects,yeses,nos,conceptlist);
			}
			else {
				// else we are still building this set
				conceptlist = new_conceptlist;
				objs->push_back(o);
				corrects->push_back(correctAnswer);
				yeses->push_back(cntyes);
				nos->push_back(cntno);						
			}
			
			
			prev_setNumber = setNumber;
		}
		
	}
			   
}
