#pragma once 

#include <string>
#include "Serialization.h"

// NOTE: deserialize is static, so this interface cannot require it of derived classes: a class that
// overrides serialize() (e.g. to store extra state) must also define its own static deserialize(),
// or it will silently inherit one that ignores that state.

template<typename T>
class Serializable { 
public:
	virtual std::string serialize() const = 0;
	static T deserialize(const std::string&);
};
