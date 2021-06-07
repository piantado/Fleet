#pragma once 

template<typename T, typename... Args> // args here are the arguments needed to deserialize -- maybe a grammar?
class Serializable { 
public:
	virtual std::string serialize() const = 0;
	
	// This must be static even though probably we really want it to be virtual
	static T deserialize(std::string&, Args...) { 
		assert(false && "*** Serializable::deserialize must be defined and accessed statically in your derived class");
	}
};