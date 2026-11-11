/* NVStorage.cpp
 *
 *  Created on:
 *  Ported to ESP32 October 14, 2025
 *  Updated May 19, 2026
 *  Updated Sep 22, 2026: FIX - load() called init() (default argument "Both") whenever the namespace being loaded
 *    had no keys yet, unconditionally re-seeding and committing NVStorage's and SSIDStorage's hardcoded defaults -
 *    i.e. resetting WiFi/MQTT/HTTP/FTP credentials to their compiled-in defaults - as a side effect of loading any
 *    OTHER, unrelated namespace for the first time (e.g. a project-specific namespace that happens to still be
 *    empty on a freshly-erased device). It now passes the namespace actually being loaded, matching what the
 *    constructor already does explicitly a few lines below. init() only has hardcoded defaults for "NVStorage" and
 *    "SSIDStorage", so for any other namespace this is now the harmless no-op it should always have been; loading
 *    "NVStorage" or "SSIDStorage" themselves for the first time is unaffected. Affects every project using base/.
 */
#include "NVStorage.h"
#include "nvs_flash.h"

//NV&   NVS = NV::This();

NV::NV(){    // initialize SV map and NVStorage maps for first time use.
    SV.emplace(std::string("NVStorage"),Storage({std::unordered_map<std::string,NVRecord>(),0,0}));
    SV.emplace(std::string("SSIDStorage"),Storage({std::unordered_map<std::string,NVRecord>(),0,0}));

    ESP_ERROR_CHECK(nvs_flash_init());
    load("SSIDStorage");
    load("NVStorage");
    
    LOGI("Storage instantiated: %d SV records and %d M-records",SV.size(),SV["SSIDStorage"].M.size()+SV["NVStorage"].M.size());
    if (SV["NVStorage"].M.size()==0) init("NVStorage");
    if (SV["SSIDStorage"].M.size()==0) init("SSIDStorage");
}

NV::~NV(){}

void NV::init(std::string Ssid){
    if (Ssid == "Both" || Ssid == "SSIDStorage"){
        mk(SV,"SSIDStorage","Vintage"      ,true, NVS_TYPE_STR,std::string  ("bugabooo"));
        mk(SV,"SSIDStorage","NSA_EAP225KL" ,true, NVS_TYPE_STR,std::string  ("bugabooo"));
        mk(SV,"SSIDStorage","EchoPoint"    ,true, NVS_TYPE_STR,std::string  ("Bug@b000"));
        mk(SV,"SSIDStorage","NSA_EAP225EP"   ,true, NVS_TYPE_STR,std::string  ("Bug@b000"));
        commit("SSIDStorage");
        LOGW("SSIDStorage initialized: %d M-records",SV["SSIDStorage"].M.size());
    }
    if (Ssid == "Both" || Ssid == "NVStorage"){ 
        mk(SV,"NVStorage","http_usr"    ,true, NVS_TYPE_STR,std::string  ("ESP32"));
        mk(SV,"NVStorage","http_pwd"    ,true, NVS_TYPE_STR,std::string  ("WebServer"));
        mk(SV,"NVStorage","mqtt_uri"    ,true, NVS_TYPE_STR,std::string  ("mqtt://192.168.50.8"));
        mk(SV,"NVStorage","mqtt_port"   ,true, NVS_TYPE_STR,std::string  ("1883"));
        mk(SV,"NVStorage","mqtt_wsport" ,true, NVS_TYPE_STR,std::string  ("9001"));
        mk(SV,"NVStorage","mqtt_usr"    ,true, NVS_TYPE_STR,std::string  ("ESP32"));
        mk(SV,"NVStorage","mqtt_pwd"    ,true, NVS_TYPE_STR,std::string  ("2A4RQ"));  
        mk(SV,"NVStorage","ftp_usr"     ,true, NVS_TYPE_STR,std::string  ("ESP32"));
        mk(SV,"NVStorage","ftp_pwd"     ,true, NVS_TYPE_STR,std::string  ("WebServer"));
        mk(SV,"NVStorage","Version"     ,true, NVS_TYPE_STR,std::string  (FIRMWARE.VERSION<std::string>("1.0")));  
        commit("NVStorage");
        LOGW("NVStorage initialized: %d M-records",SV["NVStorage"].M.size());
    }
}

/*std::string NV::sread(std::string key,std::string Ssid){
    //COUT<<"sread "<<key<<std::endl;
    if (SV[Ssid].M.find(key) != SV[Ssid].M.end())
        return any_cast<std::string>(SV[Ssid].M[key].value);
    return "NOT_FOUND";
}*/

const char* NV::cread(std::string key,std::string Ssid){
    if (SV[Ssid].M.find(key) != SV[Ssid].M.end()){
        return any_cast<std::string&>(SV[Ssid].M[key].value).c_str();
    }  
    return "NOT_FOUND";
}

 std::vector<uint8_t> NV::vread(std::string key,std::string Ssid){
    if (SV[Ssid].M.find(key) != SV[Ssid].M.end()) {
        try {
            return any_cast<std::vector<uint8_t>>(SV[Ssid].M[key].value);
        } catch (const std::bad_cast& e) {
            COUT << "Bad Cast in vread for key '" << key << "': " << e.what() << std::endl;
            return std::vector<uint8_t>();
        }
    }
    return std::vector<uint8_t>();
 }

void NV::reload(std::string Ssid){
    SV[Ssid].C=0;
    load(Ssid);
}

void NV::load(std::string Ssid){
    if (SV[Ssid].C){/*ESP_LOGW(TAG,"%s is already loaded, use reload() if necessary.",Ssid.c_str());*/ return;}
    SV[Ssid].C++;
    ESP_ERROR_CHECK(nvs_open(Ssid.c_str(), NVS_READWRITE, &SV[Ssid].H));
    nvs_iterator_t it = NULL;
    esp_err_t res = nvs_entry_find("nvs", Ssid.c_str(), NVS_TYPE_ANY, &it);
    if (res!=ESP_OK) LOGE(" %s: %s ",Ssid.c_str(),esp_err_to_name(res));
    if (res == ESP_ERR_NVS_NOT_FOUND) {  // empty namespace: seed defaults (init() commits with its own handle)
        nvs_close(SV[Ssid].H);
        init(Ssid);
        return;
    }
    while(res == ESP_OK) {
        nvs_entry_info_t info;
        if (nvs_entry_info(it, &info) == ESP_OK){
            tostr(info.key,info.type,Ssid);
        }else{
            COUT<<"Got a null pointer"<<std::endl;
        }
        res = nvs_entry_next(&it);
    }
    nvs_release_iterator(it);
    nvs_close(SV[Ssid].H);  // tostr() reads through SV[Ssid].H, so close only after the loop
    if (SV[Ssid].M.size()>0) SV[Ssid].M.erase("Test");
}

void NV::tostr(std::string key,nvs_type_t t, std::string Ssid ){
    size_t required_size {0};
    switch (t){
        case NVS_TYPE_STR: to_map(key, t, Ssid, std::string(),nvs_get_str,required_size);break;
        case NVS_TYPE_BLOB: to_map(key,t, Ssid, std::vector<uint8_t>(),nvs_get_blob,required_size);break;
        case NVS_TYPE_U8:  to_map(key, t, Ssid, uint8_t (0),nvs_get_u8);  break;
        case NVS_TYPE_U16: to_map(key, t, Ssid, uint16_t(0),nvs_get_u16); break;
        case NVS_TYPE_U32: to_map(key, t, Ssid, uint32_t(0),nvs_get_u32); break;
        case NVS_TYPE_U64: to_map(key, t, Ssid, uint64_t(0),nvs_get_u64); break;
        case NVS_TYPE_I8:  to_map(key, t, Ssid, int8_t  (0),nvs_get_i8);  break;
        case NVS_TYPE_I16: to_map(key, t, Ssid, int16_t (0),nvs_get_i16); break;
        case NVS_TYPE_I32: to_map(key, t, Ssid, int32_t (0),nvs_get_i32); break;
        case NVS_TYPE_I64: to_map(key, t, Ssid, int64_t (0),nvs_get_i64); break;
        default: break;
    }
}

void NV::commit(std::string Ssid){
    nvs_handle_t H;     // local, not SV[Ssid].H, so a concurrent commit/erase can't close it underneath us
    esp_err_t err = nvs_open(Ssid.c_str(), NVS_READWRITE, &H);
    if (err != ESP_OK) {LOGW("Error (%s) on open - opening NVS handle: %s\n", esp_err_to_name(err), Ssid.c_str());
        err = nvs_flash_init();
        if (err !=ESP_OK){LOGE("Error (%s) on nvs_flash_init!\n", esp_err_to_name(err)); return;}
        err = nvs_open(Ssid.c_str(), NVS_READWRITE, &H);
        if (err != ESP_OK) {LOGE("Error (%s) on open - opening NVS handle!\n", esp_err_to_name(err)); return;}
    }
    for (auto &[key, val]:SV[Ssid].M){
//        COUT<< "Committing "<<key<<" with "<<(val.changed?"true":"false")<<std::endl;
        if(val.changed && (key!="Test")){
            const char* k = key.c_str();
            esp_err_t set_err = ESP_OK;
            try{
                switch (val.t){
                    case NVS_TYPE_BLOB: {
                        auto& b = any_cast<std::vector<uint8_t>&>(val.value);
                        set_err = nvs_set_blob(H, k, b.data(), b.size());
                    } break;
                    case NVS_TYPE_STR: set_err = nvs_set_str(H, k, any_cast<std::string&>(val.value).c_str()); break;
                    case NVS_TYPE_I64: set_err = nvs_set_i64(H, k, any_cast<int64_t>(val.value));  break;
                    case NVS_TYPE_I32: set_err = nvs_set_i32(H, k, any_cast<int32_t>(val.value));  break;
                    case NVS_TYPE_I16: set_err = nvs_set_i16(H, k, any_cast<int16_t>(val.value));  break;
                    case NVS_TYPE_I8:  set_err = nvs_set_i8 (H, k, any_cast<int8_t>(val.value));   break;
                    case NVS_TYPE_U64: set_err = nvs_set_u64(H, k, any_cast<uint64_t>(val.value)); break;
                    case NVS_TYPE_U32: set_err = nvs_set_u32(H, k, any_cast<uint32_t>(val.value)); break;
                    case NVS_TYPE_U16: set_err = nvs_set_u16(H, k, any_cast<uint16_t>(val.value)); break;
                    case NVS_TYPE_U8:  set_err = nvs_set_u8 (H, k, any_cast<uint8_t>(val.value));  break;
                    default:
                        LOGE("Commit %s: unsupported NVS type %d; skipped\n", k, (int)val.t);
                        val.changed=false;  // can never succeed; don't retry
                        continue;
                }
            }
            catch(const std::bad_cast&){
                LOGE("Commit %s: stored %s does not match NVS type %d; skipped\n", k, val.value.type().name(), (int)val.t);
                val.changed=false;  // can never succeed; don't retry
                continue;
            }
            if (set_err != ESP_OK){
                LOGE("Error (%s) setting %s; will retry on next commit\n", esp_err_to_name(set_err), k);
                continue;           // leave changed=true so the next commit retries it
            }
            val.changed=false;
            commit_count[key]++;
            COUT<< "Committed "<<key<<" successfully"<<std::endl;
        }
    }

    err = nvs_commit(H);
    if (err != ESP_OK)
        LOGI("Error (%s) on commit - opening NVS handle!\n", esp_err_to_name(err));
    nvs_close(H);
}

void NV::list(std::string Ssid){
    for (auto [k,v]:SV[Ssid].M){
        std::cout<<v.changed<<" '"<<k<<"' ";
        switch(v.t){
            case NVS_TYPE_BLOB:
                try{std::cout<<any_cast<std::string>(v.value)<<" BLOB"<<std::endl;}
                catch(const std::bad_cast& e){std::cout<<"bad name?"<<std::endl;}
            break;
            case NVS_TYPE_STR:
                std::cout<<any_cast<std::string>(v.value)<<" STR"<<std::endl;
            break;
            case NVS_TYPE_I64:
                std::cout<<"0x"<<std::hex<<(int)any_cast<int64_t>(v.value)<<" I64"<<std::dec<<std::endl;
            break;
            case NVS_TYPE_I32:
                std::cout<<"0x"<<std::hex<<(int)any_cast<int32_t>(v.value)<<" I32"<<std::dec<<std::endl;
            break;
            case NVS_TYPE_I16:
                std::cout<<"0x"<<std::hex<<(int)any_cast<int16_t>(v.value)<<" I16"<<std::dec<<std::endl;
            break;
            case NVS_TYPE_I8:
                std::cout<<"0x"<<std::hex<<(int)any_cast<int8_t>(v.value)<<" I8"<<std::dec<<std::endl;
            break;
            case NVS_TYPE_U64:
                std::cout<<"0x"<<std::hex<<(int)any_cast<uint64_t>(v.value)<<" U64"<<std::dec<<std::endl;
            break;
            case NVS_TYPE_U32:
                std::cout<<"0x"<<std::hex<<(int)any_cast<uint32_t>(v.value)<<" U32"<<std::dec<<std::endl;
            break;
            case NVS_TYPE_U16:
                std::cout<<"0x"<<std::hex<<(int)any_cast<uint16_t>(v.value)<<" U16"<<std::dec<<std::endl;
            break;
            case NVS_TYPE_U8:
                std::cout<<"0x"<<std::hex<<(int)any_cast<uint8_t>(v.value)<<" U8"<<std::dec<<std::endl;
            break;
            default: std::cout<<"bad type";
        }
    }
}

void NV::erase(std::string SSID, std::string Ssid){
    LOGI("Opening Non-Volatile Storage (NVS) handle for erase to %s",Ssid.c_str() );
    nvs_handle_t H;     // local, not SV[Ssid].H, so a concurrent commit/erase can't close it underneath us
    esp_err_t err = nvs_open(Ssid.c_str(), NVS_READWRITE, &H);
    if (err != ESP_OK) {LOGE("Error (%s) on open - opening NVS handle!\n", esp_err_to_name(err)); return;}
    std::cout <<std::endl<<"'"<<SSID<<"' in "<<(Ssid);
        auto it = SV[Ssid].M.find(SSID);
        if (it!=SV[Ssid].M.end()) {
             esp_err_t res = nvs_erase_key(H,it->first.c_str());
             if(res == ESP_OK){
             SV[Ssid].M.erase(it);
             nvs_commit(H);
             std::cout<<" - erased"<<std::endl;
             } else std::cout<<" - failed to erase: 0x"<<std::hex<<(int)res<<std::endl;
        }else std::cout<<" - not found"<<std::endl;
    nvs_close(H);
}

void NV::erase(std::vector<std::string> vSSID, std::string Ssid){
     for (auto vit:vSSID){
        erase (vit,Ssid);
     }
}

void NV::commit_report(void){
    int i=0;
    std::cout<<std::endl<<"Commit count:"<<std::endl;
    for (auto [key,val]:commit_count){
        std::cout <<"   "<< key<<" "<<val<<std::endl;
        i+=val;
    }
    std::cout << i/(Secs/3600)<<" per hour."<<std::endl;
}

NV& NVRef(){ // .cpp
	return NV::This();
}
