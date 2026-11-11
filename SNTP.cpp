//SNTP.cpp version 1.0 October 23, 2023
//SNTP.cpp version 1.1 June 17, 2026
//#include "Common.h"
#include "base.h"
#include <string>
#include <time.h>
#include <sys/time.h>
#include "freertos/event_groups.h"
#include "esp_attr.h"
#include "esp_sleep.h"
#include "esp_sntp.h"
#include "SNTP.h"
#include "WebEvents.h"

bool TimeSet {false};
//sntp& SNTP = sntp::This();

void time_sync_notification_cb(struct timeval *tv){
    LOGI("Notification of a time synchronization event");
    if(TimeSet) {
        SNTP.Set_system_time();
    }
    WebEvents::Notify();    // the main loop can publish the new time straight away
}

void sntp::Set_system_time(bool prerequisite){
    if(TimeSet || !prerequisite) return;
    time_t Now_t;
    struct tm Now_tm;
    time(&Now_t);
    localtime_r(&Now_t, &Now_tm);
    
    if (!TimeSet || Now_tm.tm_year < (2023 - 1900)) {// Is time set and valid? If not valid, tm_year will be (1970 - 1900).
        LOGD("Time is not set yet; getting time over NTP.");
        obtain_time();
        time(&Now_t); // update 'Now_t' variable with current time
    }
    // Set timezone to Eastern Standard Time and print local time
    setenv("TZ", "EST5EDT,M3.2.0/2,M11.1.0", 1);
    tzset();
    localtime_r(&Now_t, &Now_tm);
#ifdef Include_sunrise
    SunRise(Now_t, Now_tm);
    struct tm sunrise_tm, sunset_tm;
    localtime_r(&sunrise_t, &sunrise_tm);
    localtime_r(&sunset_t, &sunset_tm);
   COUT<<std::endl
        <<" Sunrise " << std::put_time(&sunrise_tm, "%c %Z")<< std::endl
		<<" Now     " << std::put_time(&Now_tm, "%c %Z") << std::endl
		<<" Sunset  " << std::put_time(&sunset_tm, "%c %Z")<< std::endl;
#endif
    TimeSet = true;
}

void sntp::obtain_time(void){
    initialize_sntp();
    time_t Now_t = 0;
    struct tm Now_tm  {};
    int retry = 0;
    const int retry_count = 10;
    while (esp_sntp_get_sync_status() == SNTP_SYNC_STATUS_RESET && ++retry < retry_count) {
        LOGI("Waiting for system time to be set... (%d/%d)", retry, retry_count);
        vTaskDelay(2000 / portTICK_PERIOD_MS);
    }
    time(&Now_t);
    localtime_r(&Now_t, &Now_tm);
}

 void sntp::initialize_sntp(void){
    LOGI("Initializing sntp");
    esp_sntp_stop();
    esp_sntp_setoperatingmode(SNTP_OPMODE_POLL);
    esp_sntp_setservername(0, "pool.ntp.org");
    esp_sntp_set_time_sync_notification_cb(time_sync_notification_cb);
    esp_sntp_init();
}

bool sntp::TimeValid(void){return TimeSet;}

char tt[128];
const char* sntp::timeStr(int sec, std::string_view sep){
    auto t = time(NULL);
    struct tm now;
    localtime_r(&t, &now);
    if(sec>=0 && sec!=now.tm_sec ) return nullptr;
    std::string fmt = "%A %B %d, %Y";
    for (char c : sep) { fmt += c; if (c == '%') fmt += '%'; }  // sep is literal text: a '%' must reach strftime as "%%"
    fmt += "%I:%M %p";
    if (strftime(tt, sizeof(tt), fmt.c_str(), &now) == 0) tt[0] = '\0';  // didn't fit (very long sep): tt is undefined
    return tt;
}
uint16_t TimeStamp(time_t *t){
        timeval ts {0,0};
        gettimeofday(&ts,NULL);
        if(t!=nullptr) *t=ts.tv_sec;
        return ts.tv_sec;
}

#ifdef Include_sunrise
void sntp::SunRise(time_t Now_t, tm Now_tm) {

	// calculate the day of the year (the algorithm's day number starts at 1; tm_yday starts at 0)
    double day = Now_tm.tm_yday + 1;
	double lnHour = longitude / 15;// convert the longitude to hour value
	for (int i = 0; i <= 1; i++) {
	    double t = day + (((i ? 6:18)-lnHour)/24);

        //calculate the Sun's mean anomaly
		double M = (0.9856 * t) - 3.289;
		double L = M + (1.916 * sin(M * D2R)) + (0.020 * sin(2 * M * D2R)) + 282.634;

		//calculate the Sun's true longitude
		L = L + 360*(L>360 ? -1:(L<0 ? 1:0));

		//calculate the Sun's right ascension
		double RA = R2D * atan(0.91764 * tan(L * D2R));
		RA = RA + 360*(RA>360 ? -1:(RA<0 ? 1:0));

		//right ascension value needs to be in the same qua
		double Lquadrant = (floor(L / (90))) * 90;
		double RAquadrant = (floor(RA / 90)) * 90;
		RA = RA + (Lquadrant - RAquadrant);
		RA = RA / 15;//right ascension value converted into hours

		//calculate the Sun's declination
		double sinDec = 0.39782 * sin(L * D2R);
		double cosDec = cos(asin(sinDec));

		//calculate the Sun's local hour angle
		double cosH = (cos(zenith * D2R) - (sinDec * sin(latitude * D2R)))
				/ (cosDec * cos(latitude * D2R));
		double H = i ? 360 - R2D * acos(cosH): R2D * acos(cosH);
		H = H / 15;

		//calculate local mean time of rising/setting
		double T = H + RA - (0.06571 * t) - 6.622;
		double UT = T - lnHour;		//adjust back to UTC
        UT = UT +  (UT>24 ? -24 : UT<0 ? 24 : 0);
		//convert UT value to local time zone of latitude/longitude
		double localT = UT + (int) lnHour + Now_tm.tm_isdst;
		localT = fmod(localT + 24, 24);	//a summer sunset is after 00:00 UT, which would otherwise land on the previous local day

		//convert to seconds
		auto seconds = (int) (localT * 3600);
		auto Now = Now_t - Now_tm.tm_hour * 3600 - Now_tm.tm_min * 60
				- Now_tm.tm_sec;

		if (i) {sunrise_t = Now + seconds;} else {sunset_t = Now + seconds;}
	}
	sun_date = Now_tm.tm_year * 1000 + Now_tm.tm_yday;
}

// Sunrise/sunset only hold for the local date they were calculated for: recalculate on the first use of a new date.
// This also replaces values calculated from an unset clock, if NTP hadn't answered yet when Set_system_time() ran.
void sntp::RefreshSun(){
	time_t Now_t = time(NULL);
	struct tm Now_tm;
	localtime_r(&Now_t, &Now_tm);
	if (Now_tm.tm_year * 1000 + Now_tm.tm_yday != sun_date) SunRise(Now_t, Now_tm);
}

void sntp::SetLocation(float lat, float lon){
	latitude  = lat;
	longitude = lon;
}

bool sntp::SunIsUp(time_t now){
    RefreshSun();
    return (now>sunrise_t && now<sunset_t);
}

tm sntp::sun(bool updn){
    RefreshSun();
    struct tm result;
    localtime_r((updn)?&sunrise_t:&sunset_t, &result);
    return result;
}

std::string sntp::sunSpan(){
    RefreshSun();
    std::ostringstream os;
    struct tm sunrise_tm, sunset_tm;
    localtime_r(&sunrise_t, &sunrise_tm);
    localtime_r(&sunset_t, &sunset_tm);
     os << "Sunrise: " << std::put_time(&sunrise_tm, "%I:%M:%S %p")<< "&nbsp;&nbsp;&nbsp;"
         << "Sunset: " << std::put_time(&sunset_tm, "%I:%M:%S %p")<< std::endl;
    return os.str();
}

#endif

sntp& SNTPRef(){ // .cpp
	return sntp::This();
}
