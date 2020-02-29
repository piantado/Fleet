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
