#pragma once 

///~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
/// Time is a goddamn nightmare, we are going to define our own time type
/// In Fleet, EVERYTHING is going to be stored as an unsigned long for ms
///~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

#include <chrono>

typedef unsigned long time_ms;
typedef std::chrono::high_resolution_clock::time_point timept;

timept now() { 
	/**
	 * @brief Give a timepoint for the current time. 
	 * @return 
	 */
	
	return std::chrono::high_resolution_clock::now();
}

time_ms time_since(timept x) {
	/**
	 * @brief Measure time in ms since the the timepoint x
	 * @param x
	 * @return 
	 */	
	return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::high_resolution_clock::now()-x).count();
}


// this is just handy for timing stuff -- put blocks of code between tic() and tic()
timept tic_start;
time_ms tic_elapsed;
void tic() {
	/**
	 * @brief record the amount of time since the last tic()
	 */

	tic_elapsed =  time_since(tic_start);
	tic_start = now();
}
double elapsed_seconds() { 
	/**
	 * @brief How long (in seconds) has elapsed between tic() calls
	 * @return 
	 */
	
	return tic_elapsed / 1000.0; 
}


std::string datestring() {
	/**
	 * @brief A friendly date string
	 * @return 
	 */
	
	//https://stackoverflow.com/questions/40100507/how-do-i-get-the-current-date-in-c
	
	time_t rawtime;
	struct tm * timeinfo;
	char buffer[80];

	time (&rawtime);
	timeinfo = localtime(&rawtime);

	strftime(buffer,80,"%Y-%m-%d %I:%M:%S",timeinfo);
	return std::string(buffer);
}



///~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
/// Time conversions for fleet
///~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

time_t convert_time(std::string& s) {
	/**
	 * @brief Converts our own time format to ms, which is what Fleet's time utilities use
	 * 		  The time format we accept is #+(.#+)[smhd] where shmd specifies seconds, minutes, hours days 
	 * @param s
	 * @return 
	 */
	
	// specila case of s="0" will be allowed
	if(s == "0") return 0;
		
	// else we must specify a unit	
	double multiplier; // for default multiplier of 1 is seconds
	switch(s.at(s.length()-1)) {
		case 's': multiplier = 1000; break; 
		case 'm': multiplier = 60*1000; break;
		case 'h': multiplier = 60*60*1000; break;
		case 'd': multiplier = 60*60*24*1000; break;
		default: 
			std::cout << "*** Unknown time specifier: " << s.at(s.length()-1) << " in " << s << ". Did you forget a unit?" << std::endl;
			assert(0);
	}
	
	double t = std::stod(s.substr(0,s.length()-1)); // all but the last character
		
	return (unsigned long)(t*multiplier); // note this effectively rounds to the nearest second 
	
}
