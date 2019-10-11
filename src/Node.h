#pragma once

#include <functional>
#include <stack>

#include "Hash.h"

class Node {
	
public:

	class NodeIterator;
	static NodeIterator EndNodeIterator;  // this by definition has nullptr as current

	Node* parent; 
	size_t pi; // what index am I in the parent?

	std::vector<Node> child;
	const Rule*  rule; // which rule did I use?
	double       lp; 
	bool         can_resample;
	
	
	Node(const Rule*, double, bool);
	Node(const Node& n);
	Node(Node&& n);
	void operator=(const Node& n);
	void operator=(Node&& n);
	
	Node::NodeIterator begin() const;
	Node::NodeIterator end()   const;
	
	Node* left_descend() const;
	

	
	void set_child(size_t i, Node& n);
	void set_child(size_t i, Node&& n);
	
	bool is_null() const;

	template<typename T>
	T sum(std::function<T(const Node&)>& f ) const;
	void map( const std::function<void(Node&)>& f);
	void mapconst( const std::function<void(const Node&)>& f) const;
	virtual size_t count() const;	
	virtual bool is_root() const;
	virtual bool is_evaluable() const;
		
	virtual Node* get_nth(int n, std::function<int(const Node&)>& f);
	virtual Node* get_nth(int& n);
	
	template<typename T>
	std::pair<Node*, double> sample(std::function<T(const Node&)>& f);

	
	virtual std::string string() const;	
	virtual std::string parseable(std::string delim) const;
	
	// For running programs
	
	virtual size_t program_size() const;
	virtual void linearize(Program &ops) const;
	
	virtual bool operator==(const Node& n) const;
	virtual size_t hash(size_t depth) const;
	
//	template<typename G> 
//	Node copy_resample(const G* g, bool f(const Node& n)) const;
//	virtual size_t neighbors(const Grammar* g) const;
//	virtual void expand_to_neighbor(const Grammar* g, int& which);
//	virtual void complete(const Grammar* g);
//	
	
};

