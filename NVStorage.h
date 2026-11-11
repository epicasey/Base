/* NVStorage.h
 * 
 *  Created on: 
 *  Ported to ESP32 October 14, 2025
 */
#ifndef NVSTORAGE_H_
#define NVSTORAGE_H_

//#include "Common.h"
#include "base.h"
#include "freertos/task.h"
#include "nvs.h"
#include "esp_log.h"
#include <unordered_map>

#include <string>
#include <iterator>
#include <vector>
#include <any>
#include <charconv>
#include <functional>

#include <typeindex>
#include <typeinfo>

#define halt while(true){vTaskDelay(10000);}

/// @brief Non Volitile Storage Management Class
class NV{
    private:
        template<typename U>
        static std::decay_t<U> normalize_write_value(U v){
            return v;
        }

        static std::string normalize_write_value(const char* v){
            return v ? std::string(v) : std::string();
        }

        static std::string normalize_write_value(char* v){
            return v ? std::string(v) : std::string();
        }

        /// @brief Private: Class base constructor; be wary of pointers in definition -  they will bite you
        NV();

        /// @brief Private: Class destructor - nothing to do if there are no heap allocations
        ~NV();

        /// @brief Private: static pointer to last instance invoked. There will be issues if multiple inctances are created
        //static NV *S;
        std::unordered_map<std::string,int> commit_count;

        /// @brief Private: value = value for <key,value> map; changed = value had changed but not yet saved to NV Storage
        //struct NVRecord {vector<uint8_t> value; bool changed {false}; nvs_type_t T;};
        //template<typename T>
        struct NVRecord {std::any value; bool changed {false}; nvs_type_t t;};

        /// @brief Private: N = Namespace; M = unordered_map<key,value> of NVRecords; H = Handle; C = Count of load calls
        struct Storage{std::unordered_map<std::string,NVRecord> M;nvs_handle_t H; int C;};
        //struct Storage{string N;unordered_map<string,NVRecord> M;nvs_handle_t H; int C;};


        /// @brief Private: vector of namespaces in use; all functions default to namespace index 0
        //vector<Storage>SV;
        std::unordered_map<std::string,Storage>SV;

        /// @brief Used to initialize a an NVRecord and store it to a Storage vector
        /// @tparam T 
        /// @param svRef    Reference to the vector of "Storage" structures, in which to place the NVRecord 
        /// @param svName   Name of the nvs namespace that will contain the NVRecord
        /// @param key      Name of the key.
        /// @param changed  True if this record should be committed to NV storage
        /// @param nvsType  Type of storage in which to put the key-value pair.
        /// @param value    Value to be stored with key.
        template<typename T>
        void mk(std::unordered_map<std::string,Storage>&svRef,std::string svName, std::string key, bool changed, nvs_type_t nvsType,T value){
                NVRecord w {T(value),changed,nvsType};
                svRef[svName].M[key]=w;
        }

        /// @brief Private: Convert value associated with a Key from a pointer to a string
        /// @param key Key whose value is to be converted
        /// @param Ssid Index of vector SV that is in scope
        void tostr(std::string key,nvs_type_t t, std::string Ssid );

    public:
        NV(const NV&)= delete;
        static NV& This(void){static NV Instance; return Instance;}
        NV& operator=(const NV&)=delete;


        /// @brief Public: Initialize a namespace with default values; only allow to initialize once per namespace 
        /// @param Ssid Namespace and Map involved 
        void init(std::string Ssid = "Both");

        /// @brief Public: Get the value associated with a key
        /// @param key Key to find.
        /// @param Ssid Namespace and Map in scope
        /// @return charcter pointer to the value assocaited with the key        
        const char* cread(std::string key,std::string Ssid = "NVStorage");
        std::vector<uint8_t> vread(std::string key,std::string Ssid = "NVStorage");

        /// @brief Public: Template function to get the value associated with a key; this is more type safe than cread() but requires the caller to specify the type of the value being read
        /// @tparam T Type of the value being read; this must match the type of the value stored in the NVRecord, or be a numeric type that can be converted from a string if the value is stored as a string
        /// @param key Key to find.
        /// @param val Reference to the variable where the value will be stored.
        /// @param Ssid Namespace and Map in scope.
        /// @return True if the key was found and the value was successfully converted, false otherwise.
        template<typename T>
        bool read(std::string key,T &val,std::string Ssid = "NVStorage"){
            auto s = SV.find(Ssid);     // find, not operator[], so a read never creates a namespace
            if (s == SV.end()) return false;
            auto f = s->second.M.find(key);
            if (f == s->second.M.end()) return false;
            const std::any& a = f->second.value;

            if (auto p = std::any_cast<T>(&a)) { val = *p; return true; }  // exact type; pointer form doesn't throw

            // If stored as string and T is numeric, try conversion; the whole string must parse
            if constexpr (std::is_arithmetic_v<T> && !std::is_same_v<T, bool>) {
                if (auto sp = std::any_cast<std::string>(&a)) {
                    T tmp{};
                    auto [end, ec] = std::from_chars(sp->data(), sp->data() + sp->size(), tmp);
                    if (ec == std::errc() && end == sp->data() + sp->size()) { val = tmp; return true; }
                    ESP_LOGW("NVS", "%s: \"%s\" is not a valid %s", key.c_str(), sp->c_str(), typeid(T).name());
                    return false;
                }
            }
            ESP_LOGW("NVS", "%s: stored as %s, requested %s", key.c_str(), a.type().name(), typeid(T).name());
            return false;   // val is left unchanged, so callers' defaults survive
        }

        /// @brief Public: Add a key,value pair or change the value associated with an existing key
        /// @param key Key to find or add
        /// @param value New value to associate with the key
        /// @param Ssid Namespace and Map in scope where the key,value do or are to reside
        template<typename T>
        void write(std::string key,T value,std::string Ssid="NVStorage"){
            auto normalized_value = normalize_write_value(value);
            using StoredT = std::decay_t<decltype(normalized_value)>;

            auto S = SV[Ssid].M.find(key);
            auto f = nvs_type.find(std::type_index(typeid(StoredT)));
            if(f==nvs_type.end()) {std::cout<<typeid(T).name()<<" Not in map"<<std::endl; return;}
            if (S == SV[Ssid].M.end()){//|| value!=any_cast<T>(S.value)){
                mk(SV,Ssid,key,true, f->second,normalized_value);
            }else if(S->second.value.type()!=typeid(StoredT) || normalized_value!=std::any_cast<StoredT>(S->second.value)){
                    S->second.value   = normalized_value;
                    S->second.changed = true;
                    S->second.t       = f->second;
                }
        }

        /// @brief Public: Commit any key,value pair that is new or has changed since last load() to NV storage; this should be called after write() to save changes to NV storage
        /// @param Ssid Namespace and Map involved
        const std::unordered_map<std::type_index, nvs_type_t> nvs_type{
            {std::type_index(typeid(std::string)),NVS_TYPE_STR},
            {std::type_index(typeid(std::vector<uint8_t>)),NVS_TYPE_BLOB},
            {std::type_index(typeid(uint8_t)),NVS_TYPE_U8},
            {std::type_index(typeid(uint16_t)),NVS_TYPE_U16},
            {std::type_index(typeid(uint32_t)),NVS_TYPE_U32},
            {std::type_index(typeid(uint64_t)),NVS_TYPE_U64},
            {std::type_index(typeid(int8_t)),NVS_TYPE_I8},
            {std::type_index(typeid(int16_t)),NVS_TYPE_I16},
            {std::type_index(typeid(int32_t)),NVS_TYPE_I32},
            {std::type_index(typeid(int64_t)),NVS_TYPE_I64}
        };

        /// @brief Public: load a Map from NV memory of a Namespace; Only allow to load once.
        /// @param Ssid Namespace and Map involved
        void load(std::string Ssid = "NVStorage");

        /// @brief Public: Overfide load and reload a Map from NV memory of a Namespace;
        /// @param Ssid Namespace and Map involved
        void reload(std::string Ssid = "NVStorage");

        /// @brief Public: Commit any Key,value pair that is new or has changed since last load()
        /// @param Ssid Namespace and Map involved
        void commit(std::string Ssid = "NVStorage");

        /// @brief Public: Send a list of key,value pairs to terminal
        /// @param Ssid  Namespace and Map involved
        void list(std::string Ssid = "NVStorage");

        /// @brief Public: For testing only -  to erase specifc key,value pairs
        /// @param SSID String containing element to be erased
        /// @param Ssid Namespace and Map involved
        void erase(std::string SSID, std::string Ssid);

        /// @brief 
        /// @param vSSID vector containing strings to be erased.
        /// @param Ssid  Namespace and Map involved.
        void erase(std::vector<std::string> vSSID, std::string Ssid);

        /// @brief Print number of commits by key and in total per hour
        void commit_report(void);
        
    private:
        /// @brief Private: Helper function to populate the internal map with values from NV storage; this is called by load() and reload() to read values from NV storage and populate the internal map with them
        /// @param Ssid Namespace and Map involved  
        /// @tparam T Type of the value being read; this must match the type of the value stored in the NVRecord, or be a numeric type that can be converted from a string if the value is stored as a string
        /// @param key Key to find.
        /// @param t Type of the value being read; this must match the type of the value stored in the NVRecord, or be a numeric type that can be converted from a string if the value is stored as a string 
        /// @tparam Fn Type of the nvs_get function being called; this is used to allow the same to_map function to be used for both string and non-string types, which require different nvs_get functions
        /// @param nvs_get Function to read the value from NV storage; this is either
        template<typename T, typename Fn>
        void to_map(std::string key, nvs_type_t t, std::string Ssid, T value, Fn nvs_get ){
            if (SV[Ssid].M.find(key) == SV[Ssid].M.end()){
                mk(SV,Ssid,key ,false, t,T(0));
            }
            nvs_get(SV[Ssid].H,key.c_str(), &value);
            SV[Ssid].M[key].value   = value;
            SV[Ssid].M[key].changed = false;
            SV[Ssid].M[key].t       = t;
        };

        /// @brief Private: Helper function to read a vector of bytes from NV storage and populate the internal map with it; this is called by load() and reload() to read blob values from NV storage and populate the internal map with them
        /// @param Ssid Namespace and Map involved
        /// @tparam T Type of the value being read; this must match the type of the value stored in the NVRecord, or be a numeric type that can be converted from a string if the value is stored as a string
        /// @param key Key to find.
        /// @tparam Fn Type of the nvs_get function being called; this is used to allow the same to_map function to be used for both string and non-string types, which require different nvs_get functions
        /// @param t Type of the value being read; this must match the type of the value stored in the NVRecord, or be a numeric type that can be converted from a string if the value is stored as a string 
        /// @param value Reference to the variable where the value will be stored; this is used to allow the same to_map function to be used for both string and non-string types, which require different nvs_get functions, which have different parameters for the value being read 
        /// @param nvs_get Function to read the value from NV storage; this is either nvs_get_blob for blob values, which requires a pointer to a vector of bytes and a reference to a size variable, or one of the other nvs_get functions for non-blob values, which requires a pointer to the value being read and does not require a size variable
        /// @param size Reference to a size variable; this is used to allow the same to_map function to be used for both string and non-string types, which require different nvs_get functions, which have different parameters for the value being read, which may or may not require a size variable
        template<typename T, typename Fn>
        void to_map(std::string key, nvs_type_t t, std::string Ssid, T value, Fn nvs_get, size_t size ){
            nvs_get_str(SV[Ssid].H, key.c_str(), NULL, &size);
            if (size > 1000) {
                nvs_erase_key(SV[Ssid].H, key.c_str());
                LOGW("%s: required size %u to big - deleting",key.c_str(),size );
                return;
            }
            if (SV[Ssid].M.find(key) == SV[Ssid].M.end()){
                mk(SV,Ssid,key ,false, t,T());
            }
            value.assign(t==NVS_TYPE_STR?size-1:size,'\0');//must be size-1 for strings; vectors?? 
            nvs_get(SV[Ssid].H,key.c_str(), value.data(), &size);
            SV[Ssid].M[key].value   = value;
            SV[Ssid].M[key].changed = false;
            SV[Ssid].M[key].t       = t;
        };
};
//extern NV& NVS;

NV& NVRef(); // .h
#define NVS (NVRef())

#endif