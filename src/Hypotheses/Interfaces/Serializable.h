#pragma once 

template<typename T>
class Serializable { 
public:
	virtual std::string serialize() const = 0;
	
	// must define a deserialization with some number of arguments
	template<typename... Args>
	T deserialize(std::string&, Args...);
};


//std::string serialize_double(double d) {
//	
//	const int k = sizeof(double)/sizeof(char); 
//	std::string out(k, 0);
//	for(int i=0;i<k;i++) {
//		
//	}
//}