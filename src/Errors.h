#pragma once 

#include <exception> 
#include <stdexcept>
#include <string>

class NotImplementedError : public std::logic_error {
public:

	NotImplementedError() : std::logic_error("*** Function not yet implemented.") { }
	NotImplementedError(std::string s) : std::logic_error(s) { }

    virtual char const* what() const noexcept override { 
		return "*** Function not yet implemented."; 
	}
};

class YouShouldNotBeHereError : public std::logic_error {
public:

	YouShouldNotBeHereError() : std::logic_error("*** You got a YouShouldNotBeHereError.") { }
	
	YouShouldNotBeHereError(std::string s) : std::logic_error(s) { }
	

    virtual char const* what() const noexcept override { 
		return "*** It was a mistake to call this."; 
	}
};

