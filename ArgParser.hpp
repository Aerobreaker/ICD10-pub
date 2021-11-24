#pragma once

#include <string>
#include <unordered_map>
#include <unordered_set>

class ArgParser {
public: // API methods and constructors should be public
	ArgParser(const std::vector<std::pair<std::string, std::string>> &tokens);
	ArgParser(const std::vector<std::string> &tokens_short);
	ArgParser(const std::vector<std::string> &tokens_short, const std::vector<std::string> &tokens_long);
	bool parse(int argc, char *argv[]);
	bool found(const std::string& token) const;
	std::string get_value(const std::string &token) const;
	bool add_token(std::string short_token, std::string long_token, bool has_value = true, bool positional = true);
protected: // Children are going to need access to these, but the API doesn't need to reveal them
	std::unordered_map<std::string, std::string> keys_trans;
	std::unordered_map<std::string, std::string> values;
	std::unordered_map<std::string, bool> keys_found;
	std::vector<std::string> key_order;
	std::unordered_set<std::string> short_keys;
	std::unordered_set<std::string> long_keys;
private: // Children can init by accessing the constructor
	void init(const std::vector<std::string> &tokens_short, const std::vector<std::string> &tokens_long);
};
