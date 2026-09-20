#include "SettingsRuntime.h"
#include <cassert>
#include <iostream>
SysMonAppSettings appSettings;
int main() {
    using namespace SettingsRuntime;
    appSettings.temperatureUnit=1;
    assert(temperature(0)=="32 F"); assert(temperature(100)=="212 F");
    assert(temperature(-1)=="-- F");
    appSettings.networkUnit=1; assert(networkRate(8)=="1.00 MB/s");
    appSettings.networkUnit=2; assert(networkRate(1)=="1000.00 Kbps");
    AlertState state; Sample sample; sample.cpuC=90;
    assert(!state.evaluate(sample,appSettings).empty());
    assert(state.evaluate(sample,appSettings).empty());
    sample.cpuC=84; assert(state.evaluate(sample,appSettings).empty());
    sample.cpuC=90; assert(state.evaluate(sample,appSettings).empty());
    sample.cpuC=80; state.evaluate(sample,appSettings);
    sample.cpuC=90; assert(!state.evaluate(sample,appSettings).empty());
    sample.cpuC=-1; assert(state.evaluate(sample,appSettings).empty());
    const auto folder=std::filesystem::temp_directory_path()/std::filesystem::path("sysmon-runtime-test-"+std::to_string(GetCurrentProcessId()));
    std::filesystem::create_directories(folder);
    std::ofstream(folder/"monitor-old.sysmonlog") << "old";
    std::ofstream(folder/"unrelated.sysmonlog") << "keep";
    for(auto name:{"monitor-old.sysmonlog","unrelated.sysmonlog"})
        std::filesystem::last_write_time(folder/name,std::filesystem::file_time_type::clock::now()-std::chrono::hours(24*45));
    { LogWriter writer; writer.enqueue(folder.string(),"sample=1",30); writer.enqueue(folder.string(),"alert=1",30,true); writer.stop(); assert(writer.error().empty()); }
    assert(!std::filesystem::exists(folder/"monitor-old.sysmonlog"));
    assert(std::filesystem::exists(folder/"unrelated.sysmonlog"));
    int generated=0;
    for(const auto& file:std::filesystem::directory_iterator(folder)) {
        std::ifstream in(file.path()); std::string contents((std::istreambuf_iterator<char>(in)),{});
        if(contents.find("sample=1")!=std::string::npos || contents.find("alert=1")!=std::string::npos) ++generated;
        in.close();
        std::filesystem::remove(file.path());
    }
    assert(generated==2); std::filesystem::remove(folder);
    std::cout << "PASS: units, missing sensors, alert hysteresis, logging, flush, and scoped retention\n";
}
