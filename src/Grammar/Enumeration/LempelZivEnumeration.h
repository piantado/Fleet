
/*
	Node lempel_ziv_full_expand(nonterminal_t nt, enumerationidx_t z, Node* root=nullptr) const {
		IntegerizedStack is(z);
		return lempel_ziv_full_expand(nt, is, root);
	}
		
	Node lempel_ziv_full_expand(nonterminal_t nt, IntegerizedStack& is, Node* root=nullptr) const {
		// This implements "lempel-ziv" decoding on trees, where each integer
		// can be a reference to prior *full* subtree
//#ifdef BLAHBLAH		
		// First, lowest numbers will reference prior complete subtrees
		// that are not terminals
//		std::function is_full_tree = [&](const Node& ni) -> int {
//			return 1*(ni.rule->nt == nt and (not ni.is_terminal()) and ni.is_complete() );
//		};
//		
//		// might be a reference to a prior full tree
//		int t = (root == nullptr ? 0 : root->sum(is_full_tree));
//		if(is.get_value() < (size_t)t) {
//			return *root->get_nth(is.get_value(), is_full_tree); 
//		}
//		is -= t;
//
//		// next numbers are terminals first integers encode terminals
//		enumerationidx_t numterm = count_terminals(nt);
//		if(is.get_value() < numterm) {
//			return makeNode(this->get_rule(nt, is.get_value() ));	// whatever terminal we wanted
//		}
//		is -= numterm;
//
//		// now we choose whether we have encoded children directly
//		// or a reference to prior *subtree* 
//		if(root != nullptr and is.pop(2) == 0) {
//			
//			// count up the number
//			size_t T=0;
//			for(auto& n : *root) {
//				if(n.nt() == nt) {
//					T += count_connected_partial_subtrees(n)-1; // I could code any of these subtrees, but not their roots (so -1)
//				}
//			}
//			
//			// which subtree did I get?
//			int t = is.pop(T);
//
//			// ugh go back and find it
//			//Node out;
//			for(auto& n : *root) {
//				if(n.nt() == nt) {
//					T += count_connected_partial_subtrees(n)-1; // I could code any of these subtrees, but not their roots (so -1)
//				}
//			}
//			
//			auto out = copy_connected_partial_subtree(*root, t); // get this partial tree
//			
//			// TODO: Make it so that a subtree we reference cannot be ONE node (so therefore
//			//       the node is not null)
//			
//			// and now fill it in
//			if(out.is_null()) {
//				out = lempel_ziv_full_expand(out.nt(), is.get_value(), root);
//			}
//			else {
//				
//				std::function is_null = +[](const Node& ni) -> size_t { return 1*ni.is_null(); };
//				auto b = out.sum(is_null);
//
//				for(auto& ni : out) {
//					if(ni.is_null()) {
//						auto p = ni.parent; // need to set the parent to the new value
//						auto pi = ni.pi;
//						--b;
//						// TODO: NOt right because the parent links are now broken
//						p->set_child(pi, lempel_ziv_full_expand(p->rule->type(pi), (b==0 ? is.get_value() : is.pop()), root));
//					}
//				}
//			}
//			
//			return out;
//		}
//		else { 
//			// we must have encoded a normal tree expansion
//
//			// otherwise just a direct coding
//			Rule* r = this->get_rule(nt, is.pop(count_nonterminals(nt))+numterm); // shift index from terminals (because they are first!)
//			
//			// we need to fill in out's children with null for it to work
//			Node out = makeNode(r);
//			out.fill(); // retuired below because we'll be iterating		
//			if(root == nullptr) 
//				root = &out; // if not defined, out is our root
//			
//			for(size_t i=0;i<r->N;i++) {
//				out.set_child(i, lempel_ziv_full_expand(r->type(i), i==r->N-1 ? is.get_value() : is.pop(), root));
//			}
//			return out;		
//		}
////#endif
		
		
		return Node(); // just to avoid warning
		
		
	}


//	Node lempel_ziv_full_expand(nonterminal_t nt, enumerationidx_t z, Node* root=nullptr) const {
//		IntegerizedStack is(z);
//		return lempel_ziv_full_expand(nt, is, root);
//	}
//		
//	Node lempel_ziv_full_expand(nonterminal_t nt, IntegerizedStack& is, Node* root=nullptr) const {
//		// This implements "lempel-ziv" decoding on trees, where each integer
//		// can be a reference to prior *full* subtree
//		
//		// First, lowest numbers will reference prior complete subtrees
//		// that are not terminals
//		std::function is_full_tree = [&](const Node& ni) -> int {
//			return 1*(ni.rule->nt == nt and (not ni.is_terminal()) and ni.is_complete() );
//		};
//		
//		std::function is_partial_tree = [&](const Node& ni) -> int {
//			return 1*(ni.rule->nt == nt and (not ni.is_terminal()) and (not ni.is_complete()) );
//		};
//			
//		int t = 0;
//		if(root != nullptr) 
//			t = root->sum(is_full_tree);
//		
//		if(is.get_value() < (size_t)t) {
//			return *root->get_nth(is.get_value(), is_full_tree); 
//		}
//		is -= t;
//		
//		// next numbers are terminals first integers encode terminals
//		enumerationidx_t numterm = count_terminals(nt);
//		if(is.get_value() < numterm) {
//			return makeNode(this->get_rule(nt, is.get_value() ));	// whatever terminal we wanted
//		}
//		is -= numterm;
//					
//		
//		// otherwise just a direct coding
//		Rule* r = this->get_rule(nt, is.pop(count_nonterminals(nt))+numterm); // shift index from terminals (because they are first!)
//		
//		// we need to fill in out's children with null for it to work
//		Node out = makeNode(r);
//		out.fill(); // retuired below because we'll be iterating		
//		if(root == nullptr) 
//			root = &out; // if not defined, out is our root
//		
//		for(size_t i=0;i<r->N;i++) {
//			out.set_child(i, lempel_ziv_full_expand(r->type(i), i==r->N-1 ? is.get_value() : is.pop(), root));
//		}
//		return out;		
//		
//	}


//	Node lempel_ziv_partial_expand(nonterminal_t nt, enumerationidx_t z, Node* root=nullptr) const {
//		IntegerizedStack is(z);
//		return lempel_ziv_partial_expand(nt, is, root);
//	}
//	Node lempel_ziv_partial_expand(nonterminal_t nt, IntegerizedStack& is, Node* root=nullptr) const {
//		// coding where we expand to a partial tree -- this is easier because we can more easily reference prior
//		// partial trees
//		
//		// our first integers encode terminals
//		enumerationidx_t numterm = count_terminals(nt);
//		if(is.get_value() < numterm) {
//			return makeNode(this->get_rule(nt, is.get_value() ));	// whatever terminal we wanted
//		}
//		
//		is -= numterm; // we weren't any of those 
//		
//		// then the next encode some subtrees
//		if(root != nullptr) {
//			
//			auto t = count_partial_subtrees(*root);
//			if(is.get_value() < t) {
//				return copy_partial_subtree(*root, is.get_value() ); // just get the z'th tree
//			}
//			
//			is -= t;
//		}
//		
//		// now just decode		
//		auto ri = is.pop(count_nonterminals(nt)); // mod pop		
//		Rule* r = this->get_rule(nt, ri+numterm); // shift index from terminals (because they are first!)
//		
//		// we need to fill in out's children with null for it to work
//		Node out = makeNode(r);
//		out.fill(); // returned below because we'll be iterating		
//		
//		if(root == nullptr) 
//			root = &out; // if not defined, out is our root
//		
//		for(size_t i=0;i<r->N;i++) {
//			out.set_child(i, lempel_ziv_partial_expand(r->type(i), is.get_value() == r->N-1 ? is.get_value() : is.pop(), root)); // since we are by reference, this should work right
//		}
//		return out;		
//		
//	}
	