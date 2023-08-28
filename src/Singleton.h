#pragma once 

#include <assert.h>

template<typename T>
class Singleton {
	static bool exists;
public:
	Singleton() {
		assert( (not exists) && "*** You have tried to create more than one instance of a Singleton class.");
		exists = true;
	}
	
};

template<typename T> bool Singleton<T>::exists = false;