#pragma once 

template<typename T>
class Serializable { 
public:
	virtual std::string serialize() const = 0;
	virtual T deserialize(std::string& s) = 0;
};