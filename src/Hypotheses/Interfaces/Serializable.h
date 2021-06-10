#pragma once 

template<typename T>
class Serializable { 
public:
	virtual std::string serialize() const = 0;
	static T deserialize(const std::string&);
};
