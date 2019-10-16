#pragma once

/* Load the learning lists and human data */

#include <map>
#include <fstream>
#include <string>


std::map<std::string, MyHypothesis::t_data > load_SFL_data_file(std::string datapath) { // load data from the set function learning
	using S = std::string;
	
	std::ifstream infile(datapath.c_str());
	
	
	std::map<std::string, MyHypothesis::t_data > out;
	
	S line;
	while(std::getline(infile, line)) {
		
		auto parts = split(line, ' ');
		S concept = parts[0];
		S list    = parts[1];
		size_t setNumber = stoi(parts[2]);
		size_t responseNumber __attribute__((unused)) = stoi(parts[3]);
		bool   correctAnswer = parts[4] == "True";
		
		Shape  shape;
		if     (parts[5] == "triangle")  shape = Shape::Triangle;
		else if(parts[5] == "rectangle") shape = Shape::Rectangle;
		else if(parts[5] == "circle")    shape = Shape::Circle;
		else assert(0);
		
		Color  color;
		if     (parts[6] == "blue")     color = Color::Blue;
		else if(parts[6] == "yellow")   color = Color::Yellow;
		else if(parts[6] == "green")    color = Color::Green;
		else assert(0);
		
		Size  size;
		if     (parts[7] == "1")   size = Size::Size1;
		else if(parts[7] == "2")   size = Size::Size2;
		else if(parts[7] == "3")   size = Size::Size3;
		else assert(0);

		// figure out our class
		MyHypothesis::t_datum d = { (Object){color, shape, size}, correctAnswer,  0.75 };
		d.setnumber  = setNumber;
		d.cntyes = stoi(parts[8]);
		d.cntno  = stoi(parts[9]);
		
		out[concept + "__" + list].push_back(d);
	}
	
	return out;
}