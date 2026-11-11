/*
 * JsonObject.cpp
 *
 *  Created on: Apr 2, 2018
 *  Updated on: Jun 22, 2026
 *      Author: Ed
 */
#define OUT ; //cout<< __LINE__ <<"- next: "<<next<< " Level:" <<level<<" key:"<<key<<"a: "<<a<<std::endl;
#include "JsonObject.h"
#include <functional>
#include <unordered_map>
#include <memory>

///***************** BuildJson class implementation *****************/
BuildJson::BuildJson() { buffer << "{";	 cl.emplace_back('}');}
BuildJson::~BuildJson() {}
BuildJson& BuildJson::OStart(void){buffer << (comma ?",{":"{");cl.emplace_back('}'); comma = false; return *this; }
BuildJson& BuildJson::AStart(void){buffer << (comma ?",[":"[");cl.emplace_back(']'); comma = false; return *this; }
BuildJson& BuildJson::End(void){if(cl.size()>0){buffer << cl.back();cl.pop_back(); comma = true;} return *this; }
BuildJson& BuildJson::Name(std::string n) {buffer << q << n << q <<":"; comma = false; return *this;}
BuildJson& BuildJson::AName(std::string n) {buffer << (comma ? ",":""); Name(n).AStart(); return *this;}
BuildJson& BuildJson::OName(std::string n){buffer << (comma ? ",":""); Name(n).OStart(); return *this;}
BuildJson& BuildJson::AValue(void) {buffer <<(comma ?",":"")<< "null"; comma = true; return *this;}
BuildJson& BuildJson::AValue(std::string a) {buffer <<(comma ?",":"")<< q<<a<<q; comma = true; return *this;}
BuildJson& BuildJson::AValue(const char* a) {buffer <<(comma ?",":"")<< q<<a<<q; comma = true; return *this;}
BuildJson& BuildJson::AValue(bool a) {buffer <<(comma ?",":"")<< std::boolalpha <<a; comma = true; return *this;}
BuildJson& BuildJson::KeyValue(std::string n) {buffer << (comma ?",":""); Name(n).AValue(); return *this;}
std::string BuildJson::str(void){while (cl.size()>0) End(); /*cout<<buffer.str().c_str()<<std::endl;*/return buffer.str();}

///***************** ParseJson class implementation *****************/
ParseJson::ParseJson(std::stringstream& ss) {Parse(ss, Jmap);} //Parse a JSON std::string from a std::stringstream and populate the Jmap with key-value pairs. The keys are the JSON keys and the values are stored in a deque to handle multiple values for the same key (e.g., in arrays).

ParseJson::ParseJson(const char* pcc, int len){ //Parse a JSON std::string from a NULL terminated C-style std::string and populate the Jmap with key-value pairs. This constructor creates a std::stringstream from the input C-std::string and then calls the Parse method to process it.
	std::stringstream ss;
	if (len < 0) ss.str(pcc);
	else {std::stringstream ss; ss.write(pcc, len);}
	Parse(ss, Jmap);
}

ParseJson::ParseJson(const char* pcc,std::unordered_map<std::string, std::deque<std::string>> &J){// parse to provided map
	std::stringstream ss(pcc);
	Parse(ss, J);
}

std::unordered_map<std::string,std::deque<std::string>> const &ParseJson::Map(void){return Jmap;}

void ParseJson::Parse(std::stringstream& ss,std::unordered_map<std::string, std::deque<std::string>> &rJmap){
	int alevel = -1;
	int olevel = -1;
	bool in_quotes = false;
		std::unordered_map<char,std::function<void()>> pmap{
		{'[',[&](){alevel++;array_level[alevel]  = key+(alevel?".":"");a="";}},
		{'{',[&](){olevel++;object_level[olevel] = key+(olevel?".":"");a="";}},
		{':',[&](){key=a;a="";}},
		{',',[&](){if (a.size() > 0){rJmap[object_level[olevel]+key].emplace_back(a);}a = "";key="";}},
		{'}',[&](){if (a.size() > 0){rJmap[object_level[olevel]+key].emplace_back(a);}a = "";key="";olevel--;}},
		{']',[&](){alevel--;key="";}},
		{'"',[&](){in_quotes = !in_quotes;}},
		{' ',[]{}}
	};
	auto done = [&](char c){a+=c;};
	for (auto& c : ss.str()) { // process each character in the input std::string
		if (c<32 || c>126) continue; // skip non-printable characters
		pmap.find(c)!=pmap.end()&&(!in_quotes || c == '"')?pmap[c]():done(c);
		
	}
	for (auto it = rJmap.cbegin(); it != rJmap.cend();){// remove empty entries
		if (it->second.size() == 0)
			rJmap.erase(it++);
		else
			++it;
	}
}

void ParseJson::PrintToCout(void) {
	std::ostringstream os;
	for (auto& [key,val] : Jmap) {
		os << key << (val.size() > 1 ? "[" : " ");
		for (auto& vit : val)
			os << vit << (vit == val.back() ? "" : ", ");
		os << (val.size() > 1 ? "]" : "") << std::endl;
	}
	std::cout << os.str();
}
