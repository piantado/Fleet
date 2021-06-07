#pragma once 

template<typename T, typename... Args> // args here are the arguments needed to deserialize -- maybe a grammar?
class Serializable { 
public:
	virtual std::string serialize() const = 0;
	
	static virtual T deserialize(std::string&, Args...) = 0;
};