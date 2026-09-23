#pragma once
#include "BackgroundSampler.h"
#include "Version.h"
#include <windows.h>
#include <winhttp.h>
#include <array>
#include <string>
#include <regex>
#include <limits>

namespace Updates
{
inline constexpr const char* currentVersion=SYSMON_VERSION_STRING;
inline constexpr const char* releasePage="https://github.com/Hadinouh/SysMon/releases/latest";
struct Result { std::string status; std::string version; bool newer=false; bool failed=false; std::string notes; };
inline bool parseVersion(const std::string& text, std::array<unsigned,3>& output)
{
    static const std::regex format(R"(^[vV]?([0-9]+)\.([0-9]+)(?:\.([0-9]+))?$)");
    std::smatch parts;
    if(!std::regex_match(text,parts,format)) return false;
    try {
        for(size_t i=0;i<3;++i) {
            unsigned long long number=parts[i+1].matched ? std::stoull(parts[i+1].str()) : 0;
            if(number>std::numeric_limits<unsigned>::max()) return false;
            output[i]=static_cast<unsigned>(number);
        }
    } catch(...) { return false; }
    return true;
}
inline Result interpret(unsigned status, const std::string& body)
{
    if(status==404) return {"No public release found.","",false,false};
    if(status==403 || status==429) return {"Check delayed by GitHub; retrying later.","",false,true};
    if(status!=200) return {"Update check failed; retrying later.","",false,true};
    std::smatch match;
    static const std::regex tag(R"tag("tag_name"\s*:\s*"([^"\\]+)")tag");
    if(!std::regex_search(body,match,tag)) return {"Invalid release response.","",false,true};
    std::array<unsigned,3> release={},current={};
    if(!parseVersion(match[1].str(),release) || !parseVersion(currentVersion,current))
        return {"Unrecognized release version.","",false,true};
    const bool newer=release>current;
    Result result{newer ? "Update "+match[1].str()+" is available." : "You are up to date.", match[1].str(),newer,false};
    static const std::regex notes(R"notes("body"\s*:\s*"((?:\\.|[^"\\])*)")notes");
    if(std::regex_search(body,match,notes)) {
        std::string raw=match[1].str();
        for(size_t i=0;i<raw.size() && result.notes.size()<12000;++i) {
            if(raw[i]=='\\' && i+1<raw.size()) {char c=raw[++i];result.notes+=c=='n'?'\n':c=='r'?'\r':c=='t'?'\t':c;}
            else result.notes+=raw[i];
        }
    }
    return result;
}
struct Internet {
    HINTERNET handle=nullptr;
    explicit Internet(HINTERNET value):handle(value) {}
    ~Internet() { if(handle) WinHttpCloseHandle(handle); }
    operator HINTERNET() const { return handle; }
};
inline Result check()
{
    const Result failure={"Could not reach GitHub; retrying later.","",false,true};
    Internet session(WinHttpOpen(SYSMON_USER_AGENT_WSTRING,WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,WINHTTP_NO_PROXY_NAME,WINHTTP_NO_PROXY_BYPASS,0));
    if(!session.handle) return failure;
    WinHttpSetTimeouts(session,5000,5000,5000,5000);
    Internet connection(WinHttpConnect(session,L"api.github.com",INTERNET_DEFAULT_HTTPS_PORT,0));
    if(!connection.handle) return failure;
    Internet request(WinHttpOpenRequest(connection,L"GET",L"/repos/Hadinouh/SysMon/releases/latest",nullptr,
        WINHTTP_NO_REFERER,WINHTTP_DEFAULT_ACCEPT_TYPES,WINHTTP_FLAG_SECURE));
    if(!request.handle) return failure;
    const wchar_t* headers=L"Accept: application/vnd.github+json\r\nX-GitHub-Api-Version: 2022-11-28\r\n";
    if(!WinHttpSendRequest(request,headers,static_cast<DWORD>(-1),nullptr,0,0,0) || !WinHttpReceiveResponse(request,nullptr)) return failure;
    DWORD status=0,size=sizeof(status);
    if(!WinHttpQueryHeaders(request,WINHTTP_QUERY_STATUS_CODE|WINHTTP_QUERY_FLAG_NUMBER,WINHTTP_HEADER_NAME_BY_INDEX,&status,&size,WINHTTP_NO_HEADER_INDEX)) return failure;
    if(status!=200) return interpret(status,"");
    std::string body;
    const ULONGLONG deadline=GetTickCount64()+15000;
    char buffer[8192]; DWORD bytes=0;
    for(;;) {
        if(GetTickCount64()>deadline || !WinHttpReadData(request,buffer,sizeof(buffer),&bytes)) return failure;
        if(bytes==0) break;
        if(body.size()+bytes>1024*1024) return {"Release response is too large.","",false,true};
        body.append(buffer,bytes);
    }
    return interpret(status,body);
}
inline BackgroundSampler<Result>& sampler() {
    static BackgroundSampler<Result> worker([](unsigned) { return check(); }, false);
    return worker;
}
inline std::string status="Automatically check for updates.";
inline std::shared_ptr<const Result> displayed;
inline ULONGLONG requestedAt=0;
inline bool pending=false;
inline std::string notifiedVersion;
inline void poll(bool enabled)
{
    auto result=sampler().latest();
    if(result && result!=displayed) { displayed=std::move(result); pending=false; }
    if(!enabled) { status="Automatically check for updates."; requestedAt=0; return; }
    const ULONGLONG now=GetTickCount64();
    const ULONGLONG interval=displayed && displayed->failed ? 3600000ULL : 86400000ULL;
    if(!pending && (requestedAt==0 || now-requestedAt>=interval)) {
        sampler().request(); requestedAt=now; pending=true;
    }
    status=pending ? "Checking GitHub releases..." : displayed ? displayed->status : "Waiting to check for updates.";
}
}
