/*
 * JsonObject.h
 *
 *  Created on: Apr 2, 2018
 *      Author: Ed
 */
#ifndef JSONOBJECT_H_
#define JSONOBJECT_H_

#include <list>
#include <deque>
#include <sstream>
#include <iostream>
#include <unordered_map>
//#include "Common.h"
#include "base.h"

class BuildJson {
public:
	BuildJson(void);
	virtual ~BuildJson();
	BuildJson& OStart(void);
	BuildJson& AStart(void);
	BuildJson& End(void);
	BuildJson& Name(std::string n);
	BuildJson& AName(std::string n);
	BuildJson& OName(std::string n);
	BuildJson& AValue(void);
	BuildJson& AValue(std::string a);
	BuildJson& AValue(const char* a);
	BuildJson& AValue(bool a);
	BuildJson& KeyValue(std::string n);
	std::string str(void);

	template<typename Kv>
	BuildJson& KeyValue(std::string n, Kv a) {
		buffer << (comma ? "," : "");
		Name(n).AValue(a);
		return *this;
	};

	template<typename T, typename ... Rest>
	BuildJson& AValue(T t, Rest ... rest) {
		AValue(t);
		return AValue(rest...);
	};

	template<class T>
	BuildJson& AValue(T a) {
		buffer << (comma ? "," : "");
		buffer << a;
		comma = true;
		return *this;
	};

private:
	char q = '"';
	std::list<char> cl;
	bool comma = false;
	std::stringstream buffer;
};

class StreamProxy {
    std::stringstream ss;
public:
    StreamProxy(const std::string& input) : ss(input) {}
    template <typename T>  // Template conversion operator: T is deduced from the target context
    operator T() const {
        T t;
        // Create a copy of the stream to allow multiple conversions or error checking
        std::stringstream temp_ss(ss.str()); 
        if (!(temp_ss >> t)) {
			temp_ss.clear(); // Clear error state
			temp_ss.str("0"); // Reset stream content to return safe default value on failure
			if (!(temp_ss >> t))
            	throw std::runtime_error("Conversion failed");
        }
        return t;
    }
};

class ParseJson {
private:
	ParseJson();
	int level = 0;
	std::string key;
	std::string a;
	std::deque<std::string> v;
	std::unordered_map<int, std::string> array_level; 
	std::unordered_map<int, std::string> object_level;
	std::unordered_map<std::string, std::deque<std::string>> Jmap;
	void Parse(std::stringstream& ss,std::unordered_map<std::string, std::deque<std::string>> &rJmap);
	
public:
	ParseJson(std::stringstream& ss);
	ParseJson(const char* pcc, int len=-1);
	ParseJson(const char* pcc,std::unordered_map<std::string, std::deque<std::string>> &rJmap);
	~ParseJson(){};
    std::unordered_map<std::string,std::deque<std::string>> const &Map(void);
	
StreamProxy get_value(std::string s, int *index = nullptr) { 
		if (!index) index = new int(0);
		auto it = Jmap.find(s);
		if (*index < 0 ) *index = it->second.size() -1;
		if (it == Jmap.end() || (*index >= it->second.size())) {
			*index = -1; // caller can check for -1 to detect error
			return StreamProxy("0");
		}
		if (it->second[*index] == "true") return StreamProxy("1");
		if (it->second[*index] == "false") return StreamProxy("0");
		return StreamProxy(it->second[*index]);
	}

	decltype(auto) operator[]( std::string idx) { return Jmap[idx][0]; }
    decltype(auto) get(std::string s,int i=0){return atoi(Jmap[s][i].c_str());};
	void PrintToCout(void);
};

#endif /* JSONOBJECT_H_ */
