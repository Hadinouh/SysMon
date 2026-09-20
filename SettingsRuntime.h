#pragma once
#include "Settings.h"
#include <array>
#include <cmath>
#include <sstream>
#include <iomanip>
#include <fstream>
#include <filesystem>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <deque>

namespace SettingsRuntime {
inline double temperatureValue(double c) { return appSettings.temperatureUnit ? c * 1.8 + 32 : c; }
inline const char* temperatureSuffix() { return appSettings.temperatureUnit ? " F" : " C"; }
inline std::string temperature(double c) {
    if (!std::isfinite(c) || c < 0) return std::string("--") + temperatureSuffix();
    std::ostringstream out; out << std::fixed << std::setprecision(0) << temperatureValue(c) << temperatureSuffix();
    return out.str();
}
inline std::string networkRate(double mbps) {
    double value = std::isfinite(mbps) ? (std::max)(0.0, mbps) : 0.0;
    const char* unit = " Mbps";
    if (appSettings.networkUnit == 1) { value /= 8; unit = " MB/s"; }
    if (appSettings.networkUnit == 2) { value *= 1000; unit = " Kbps"; }
    std::ostringstream out; out << std::fixed << std::setprecision(2) << value << unit;
    return out.str();
}

struct Sample { double cpu=0, memory=-1, disk=-1, cpuC=-1, gpuC=-1, down=0, up=0; };
class AlertState {
    std::array<bool,4> active_{};
public:
    std::string evaluate(const Sample& s, const SysMonAppSettings& settings) {
        const double values[]={s.cpuC,s.gpuC,s.disk,s.memory};
        const int limits[]={settings.cpuTemperatureAlert,settings.gpuTemperatureAlert,settings.diskUsageAlert,settings.memoryUsageAlert};
        const char* names[]={"CPU temperature","GPU temperature","Disk space used","Memory used"};
        std::string result;
        for(int i=0;i<4;++i) {
            if(values[i]<0 || !std::isfinite(values[i])) continue;
            if(values[i]<limits[i]-2) active_[i]=false;
            if(values[i]>=limits[i] && !active_[i]) {
                active_[i]=true;
                if(!result.empty()) result+="; ";
                result+=std::string(names[i])+": "+std::to_string(static_cast<int>(values[i]))+(i<2 ? " C" : "%");
            }
        }
        return result;
    }
};

// File IO and retention run on a bounded worker queue, never during painting.
class LogWriter {
    struct Job { std::string folder, text; int retention; bool alert; };
    std::mutex mutex_; std::condition_variable ready_; std::deque<Job> jobs_;
    std::thread worker_; bool stopping_=false;
    void run() {
        std::string lastCleanup;
        for(;;) {
            Job job;
            { std::unique_lock<std::mutex> lock(mutex_); ready_.wait(lock,[&]{return stopping_||!jobs_.empty();});
              if(jobs_.empty()&&stopping_) return; job=std::move(jobs_.front()); jobs_.pop_front(); }
            try {
                SYSTEMTIME now{}; GetLocalTime(&now); char date[16];
                snprintf(date,sizeof(date),"%04u-%02u-%02u",now.wYear,now.wMonth,now.wDay);
                const auto folder=std::filesystem::path(job.folder); std::filesystem::create_directories(folder);
                const std::string cleanupKey=job.folder+date+std::to_string(job.retention);
                if(lastCleanup!=cleanupKey) {
                    const auto cutoff=std::filesystem::file_time_type::clock::now()-std::chrono::hours(24*job.retention);
                    for(const auto& entry:std::filesystem::directory_iterator(folder)) {
                        const auto name=entry.path().filename().string();
                        if(!entry.is_symlink() && entry.is_regular_file() && entry.path().extension()==".sysmonlog" &&
                            (name.rfind("monitor-",0)==0||name.rfind("alerts-",0)==0) && entry.last_write_time()<cutoff)
                            std::filesystem::remove(entry.path());
                    }
                    lastCleanup=cleanupKey;
                }
                std::ofstream out(folder/(std::string(job.alert?"alerts-":"monitor-")+date+".sysmonlog"),std::ios::app);
                out << date << ' ' << std::setfill('0') << std::setw(2) << now.wHour << ':' << std::setw(2) << now.wMinute
                    << ':' << std::setw(2) << now.wSecond << ' ' << job.text << '\n';
                if(!out) throw std::runtime_error("Cannot write monitoring log");
                std::lock_guard<std::mutex> lock(mutex_); error_.clear();
            } catch(const std::exception& e) { std::lock_guard<std::mutex> lock(mutex_); error_=e.what(); }
        }
    }
    std::string error_;
public:
    ~LogWriter(){stop();}
    void enqueue(std::string folder,std::string text,int retention,bool alert=false) {
        std::lock_guard<std::mutex> lock(mutex_); if(stopping_) return;
        if(jobs_.size()>=128) { error_="Log queue full"; return; }
        jobs_.push_back({std::move(folder),std::move(text),retention,alert});
        if(!worker_.joinable()) worker_=std::thread([this]{run();}); ready_.notify_one();
    }
    std::string error(){std::lock_guard<std::mutex> lock(mutex_);return error_;}
    void stop(){ {std::lock_guard<std::mutex> lock(mutex_);stopping_=true;ready_.notify_one();} if(worker_.joinable())worker_.join(); }
};
inline LogWriter logs;
inline AlertState alerts;
}
