//SNTP.h version 1.0 October 23, 2023
//SNTP.h version 1.1 June 17, 2026
#ifndef SNTP_H
#define SNTP_H

#include <string>
#include <string_view>

//#define Include_sunrise //include this line (uncommented) in Projects Common.h file to use sunrise/sunset calculations

class sntp {
public:
    sntp(const sntp&)= delete;
    static sntp& This(void){static sntp Instance; return Instance;}
    sntp& operator=(const sntp&)=delete;
    void Set_system_time(bool prerequisite = true);
    bool TimeValid(void);
    /// @brief Local date and time, e.g. "Wednesday October 14, 2026 <br> 03:07 PM"
    /// @param sec -1: always; 0-59: only during that second of the minute, otherwise nullptr
    /// @param sep literal text between the date and the time (" <br> " for the web pages; e.g. " " or "\n" elsewhere)
    /// @return pointer to a shared static buffer - copy it before the next call
    const char* timeStr(int sec = -1, std::string_view sep = " <br> ");
    
private:
    sntp(){};
    ~sntp(){};
    void obtain_time(void);
    void initialize_sntp(void);
   
#ifdef Include_sunrise
public:
    bool SunIsUp(time_t now);
    tm sun(bool updn);
    std::string sunSpan();
    void SetLocation(float lat, float lon);
private:
    const double zenith = 90.83333333333333;
    const double D2R = M_PI / 180;
    const double R2D = 180 / M_PI;
    double latitude  { 45.397032};//Echo Point, Carling Twp, Ontario, Canada
    double longitude {-80.231781};//Echo Point, Carling Twp, Ontario, Canada
    time_t sunrise_t;
    time_t sunset_t;
    int sun_date {-1};  // local date sunrise_t/sunset_t were calculated for (tm_year*1000 + tm_yday)
    void SunRise(time_t Now_t, tm Now_tm);
    void RefreshSun();
#endif
};
//extern sntp& SNTP;

sntp& SNTPRef();
#define SNTP (SNTPRef())

#endif