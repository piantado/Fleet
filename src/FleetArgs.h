#pragma once

#include <string>

/* This namespace is set by command line and provides a bunch of defaults for controlling running, top, io, etc. 
 * These global variables are set by Fleet::initialize and then used (assuming they were set first) by many other
 * classes throughout Fleet. This means the e.g. default size of TopN is set to be ntop here, which is whatever
 * was specified on the command line. This prevents us from having to pass these all around. 
 * */

namespace FleetArgs {
	
	unsigned long steps         = 0;
	unsigned long inner_steps   = 0;
	unsigned long burn          = 0;
	unsigned long ntop          = 100;
	
	int           print_header  = 1; 

	double        explore         = 1.0; // we want to exploit the string prefixes we find
	size_t        nthreads        = 1;
	size_t        nchains         = 1;
	size_t        partition_depth = 3;

	unsigned long runtime          = 0; // in ms
	unsigned long inner_runtime    = 0; // in ms
	unsigned long inner_restart    = 0;
	std::string   timestring       = "0s";
	std::string   inner_timestring = "0s";

	unsigned long restart = 0;
	unsigned long thin = 0;
	unsigned long print = 0;
	
	unsigned long top_print_best = 0; // default for printing top's best
	
	std::string   input_path   = "input.txt";
	std::string   tree_path    = "tree.txt";
	std::string   output_path  = "output";
}
