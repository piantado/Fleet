#pragma once

/* Handy string functions */



std::string QQ(std::string x) {
	return std::string("\"") + x + std::string("\"");
}
std::string Q(std::string x) {
	return std::string("\'") + x + std::string("\'");
}

/* If x is a prefix of y */
bool is_prefix(const std::string& prefix, const std::string& x) {
	//https://stackoverflow.com/questions/7913835/check-if-one-string-is-a-prefix-of-another
	
	if(prefix.length() > x.length()) return false;
	
	return std::equal(prefix.begin(), prefix.end(), x.begin());
}


std::deque<std::string> split(const std::string& s, const char delimiter){
		
   std::deque<std::string> tokens;
   std::string token;
   std::istringstream ts(s);
   while (std::getline(ts, token, delimiter))
   {
      tokens.push_back(token);
   }
   return tokens;
}


// From https://en.wikibooks.org/wiki/Algorithm_Implementation/Strings/Levenshtein_distance#C++
unsigned int levenshtein_distance(const std::string& s1, const std::string& s2)
{
	const std::size_t len1 = s1.size(), len2 = s2.size();
	std::vector<std::vector<unsigned int>> d(len1 + 1, std::vector<unsigned int>(len2 + 1));

	d[0][0] = 0;
	for(unsigned int i = 1; i <= len1; ++i) d[i][0] = i;
	for(unsigned int i = 1; i <= len2; ++i) d[0][i] = i;

	for(unsigned int i = 1; i <= len1; ++i)
		for(unsigned int j = 1; j <= len2; ++j)
			  d[i][j] = std::min(d[i - 1][j] + 1, std::min(d[i][j - 1] + 1, d[i - 1][j - 1] + (s1[i - 1] == s2[j - 1] ? 0 : 1) ));
			  
	return d[len1][len2];
}


size_t count(const std::string& str, const std::string& sub) {
	// how many times does sub occur in str?
	// https://www.rosettacode.org/wiki/Count_occurrences_of_a_substring#C.2B.2B
	
    if (sub.length() == 0) return 0;
    
	size_t count = 0;
    for (size_t offset = str.find(sub); 
		 offset != std::string::npos;
		 offset = str.find(sub, offset + sub.length())) {
        ++count;
    }
    return count;
}
