#pragma once 

///~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
/// Time is a goddamn nightmare, we are going to define our own time type
/// In Fleet, EVERYTHING is going to be stored as an unsigned long for ms
///~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
#include <chrono>

typedef unsigned long time_ms;
typedef std::chrono::steady_clock::time_point timept;

timept now() { 
	return std::chrono::steady_clock::now();
}

time_ms time_since(timept x) {
	return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now()-x).count();
}


// this is just handy for timing stuff -- put blocks of code between tic() and tic()
timept tic_start;
time_ms tic_elapsed;
void tic() {
	// record the amount of time since the last tic()
	tic_elapsed =  time_since(tic_start);
	tic_start = now();
}
double elapsed_seconds() { return tic_elapsed / 1000.0; }


std::string datestring() {
	//https://stackoverflow.com/questions/40100507/how-do-i-get-the-current-date-in-c
	
	time_t rawtime;
	struct tm * timeinfo;
	char buffer[80];

	time (&rawtime);
	timeinfo = localtime(&rawtime);

	strftime(buffer,80,"%Y-%m-%d %I:%M:%S",timeinfo);
	return std::string(buffer);
}
