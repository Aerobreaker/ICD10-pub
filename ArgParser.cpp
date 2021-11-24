#include "ArgParser.hpp"

#include <stdexcept>
#include <deque>

using namespace std;

ArgParser::ArgParser(const vector<pair<string, string>> &tokens) {
	size_t num_tokens = tokens.size();
	vector<string> tokens_short, tokens_long;
	tokens_short.reserve(num_tokens);
	tokens_long.reserve(num_tokens);
	for (const pair<string, string> &it : tokens) {
		tokens_short.push_back(it.first);
		tokens_long.push_back(it.second);
	}
	init(tokens_short, tokens_long);
}

ArgParser::ArgParser(const vector<string> &tokens_short) {
	// If only one set of strings is received, assume they're short tokens
	// Only long tokens can be specified using one of the other constructors
	vector<string> tokens_long(tokens_short.size());
	init(tokens_short, tokens_long);
}

ArgParser::ArgParser(const vector<string> &tokens_short, const vector<string> &tokens_long) {
	init(tokens_short, tokens_long);
}

void ArgParser::init(const vector<string> &tokens_short, const vector<string> &tokens_long) {
	size_t num_keys = tokens_short.size();
	if (num_keys != tokens_long.size()) {
		throw invalid_argument("Token list sizes do not match!");
	}
	values = unordered_map<string, string>(num_keys);
	keys_found = unordered_map<string, bool>(num_keys);
	key_order = vector<string>{};
	key_order.reserve(num_keys);
	// These 3 don't get space reserved because they can't be shrunk again
	// Possibly set a large max_load_factor and then at the end drop it to 1 and rehash(0)?
	keys_trans = unordered_map<string, string>{};
	short_keys = unordered_set<string>{};
	long_keys = unordered_set<string>{};
	for (size_t i = 0; i < num_keys; i++) {
		string short_token = tokens_short[i];
		string long_token = tokens_long[i];
		if (!long_token.empty() && !short_token.empty()) {
			keys_trans.insert({long_token, short_token});
			long_keys.insert(long_token);
		}
		if (!short_token.empty()) {
			values.insert({short_token, ""});
			keys_found.insert({short_token, false});
			key_order.push_back(short_token);
			short_keys.insert(short_token);
		} else if (!long_token.empty()) {
			values.insert({long_token, ""});
			keys_found.insert({long_token, false});
			key_order.push_back(long_token);
			long_keys.insert(long_token);
		}
	}
}

bool ArgParser::found(const string &token) const {
	string key = token;
	if (keys_trans.contains(key)) key = keys_trans.at(key);
	if (keys_found.contains(key)) return keys_found.at(key);
	return false;
}

string ArgParser::get_value(const string &token) const {
	string key = token;
	if (keys_trans.contains(key)) key = keys_trans.at(key);
	return (values.contains(key) && keys_found.at(key)) ? values.at(key) : "";
}

bool ArgParser::add_token(string short_token, string long_token, bool has_value, bool positional) {
	if (short_token.empty() && long_token.empty()) return false;
	if (!long_token.empty() && !short_token.empty()) {
		keys_trans.insert({long_token, short_token});
		long_keys.insert(long_token);
	}
	if (!short_token.empty()) {
		if (has_value) values.insert({short_token, ""});
		keys_found.insert({short_token, false});
		if (has_value && positional) key_order.push_back(short_token);
		short_keys.insert(short_token);
	} else if (!long_token.empty()) {
		if (has_value) values.insert({long_token, ""});
		keys_found.insert({long_token, false});
		if (has_value && positional) key_order.push_back(long_token);
		long_keys.insert(long_token);
	}
	return true;
}

bool ArgParser::parse(int argc, char *argv[]) {
	// Use a deque instead of a vector so we can pop off the front quickly
	deque<string>extras {};
	int last_kwd = argc - 1;
	for (int i = 1; i < argc; i++) {
		string arg;
		if (argv[i][0] == '-' || argv[i][0] == '/') {
			if (argv[i][0] == '-' && argv[i][1] == '-') {
				// Add 2 to the pointer to get a pointer that starts 2 characters in, skipping the first two characters
				arg = argv[i] + 2;
				if (!long_keys.contains(arg)) arg = argv[i];
			} else {
				// Add 1 to the pointer to get a pointer that starts 1 characters in, skipping the first character
				arg = argv[i] + 1;
				if (!short_keys.contains(arg)) arg = argv[i];
			}
			if (keys_trans.contains(arg)) arg = keys_trans.at(arg);
			if (keys_found.contains(arg)) {
				keys_found[arg] = true;
				// Check to see if i is less than the final index because it's going to be incremented
				if (i < last_kwd && values.contains(arg)) values[arg] = argv[++i];
			} else {
				// If the argument isn't associated with a key, store it for later assignment
				extras.push_back(argv[i]);
			}
		} else {
			// If the argument doesn't start with - or /, it's not a key
			// The ternary operator moves the address that arg starts reading at up by 1, effectively
			// skipping the first character, if said first character is a quote
			arg = argv[i] + (argv[i][0] == '"' ? 1 : 0);
			if (arg.ends_with('"')) arg.pop_back();
			extras.push_back(arg);
		}
	}
	// Go through the keys in order and for any that isn't assigned, pull from the extras
	for (const string &it : key_order) {
		if (extras.empty()) break;
		// Don't need to check if the key exists because if must if it's in key_order
		if (!keys_found.at(it) && values.contains(it)) {
			values[it] = move(extras.front());
			keys_found[it] = true;
			extras.pop_front();
		}
	}
	return true;
}
