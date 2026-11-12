#pragma once 

#include <exception> 
#include <stdexcept>
#include <string>

class NotImplementedError : public std::logic_error {
public:

	NotImplementedError() : std::logic_error("*** Function not yet implemented.") { }
	NotImplementedError(std::string s) : std::logic_error(s) { }

	// NOTE: what() is inherited from std::logic_error and returns the message given above. It used to be 
	// overridden here to return a fixed string, which threw away every specific message (e.g.
	// "*** You must define compute_single_likelihood"). 
};

class YouShouldNotBeHereError : public std::logic_error {
public:

	YouShouldNotBeHereError() : std::logic_error("*** You got a YouShouldNotBeHereError.") { }
	
	YouShouldNotBeHereError(std::string s) : std::logic_error(s) { }
	
	// (what() is std::logic_error's, which returns the message -- see NotImplementedError)
};

