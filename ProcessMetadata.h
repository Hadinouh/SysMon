#pragma once
#include "BackgroundSampler.h"
#include <windows.h>
#include <shellapi.h>
#include <shlobj.h>
#include <objbase.h>
#include <map>
#include <string>
#include <vector>

// UI reads completed metadata; executable and shell access stay on one worker.
class ProcessMetadataCache
{
    struct Icons
    {
        std::map<int,HICON> sizes;
        ~Icons() { for (const auto& entry : sizes) if(entry.second) DestroyIcon(entry.second); }
    };
    struct Metadata
    {
        std::wstring path;
        std::shared_ptr<Icons> icons;
    };
    using Snapshot = std::map<DWORD, std::shared_ptr<const Metadata>>;
    struct Request { ULONGLONG tick = 0; bool icon = false; };
    std::mutex queueMutex_;
    std::map<DWORD, bool> queued_;
    std::map<DWORD, Request> requested_; // UI-owned, including negative lookups
    Snapshot collected_; // Worker-owned
    std::shared_ptr<const Snapshot> displayed_;
    BackgroundSampler<Snapshot> sampler_;

    Snapshot collect()
    {
        // Shell icon extraction requires COM on the calling worker.
        const HRESULT com = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
        std::map<DWORD, bool> batch;
        {
            std::lock_guard<std::mutex> lock(queueMutex_);
            batch.swap(queued_);
        }
        for (const auto& request : batch)
        {
            auto result = std::make_shared<Metadata>();
            HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, request.first);
            if (process)
            {
                std::vector<wchar_t> path(32768);
                DWORD length = static_cast<DWORD>(path.size());
                if (QueryFullProcessImageNameW(process, 0, path.data(), &length))
                    result->path.assign(path.data(), length);
                CloseHandle(process);
            }
            if (request.second && !result->path.empty())
            {
                // Share resolutions across instances of the same executable.
                for (const auto& entry : collected_)
                {
                    if (entry.second->path == result->path && entry.second->icons)
                    {
                        result->icons = entry.second->icons;
                        break;
                    }
                }
                if (!result->icons)
                {
                    result->icons = std::make_shared<Icons>();
                    // Extract real icon resources at useful physical pixel sizes.
                    // All shell/executable access remains on this worker.
                    for (int pixels : {16,24,32,48,64,96}) {
                        HICON extracted = nullptr;
                        SHDefExtractIconW(result->path.c_str(),0,0,&extracted,nullptr,pixels);
                        if(extracted) result->icons->sizes[pixels] = extracted;
                    }
                    if (result->icons->sizes.empty()) {
                    SHFILEINFOW info = {};
                    SHGetFileInfoW(result->path.c_str(), 0, &info, sizeof(info), SHGFI_ICON | SHGFI_SMALLICON);
                    if (!info.hIcon)
                        SHGetFileInfoW(result->path.c_str(), FILE_ATTRIBUTE_NORMAL, &info, sizeof(info),
                                       SHGFI_ICON | SHGFI_SMALLICON | SHGFI_USEFILEATTRIBUTES);
                    if(info.hIcon) result->icons->sizes[16] = info.hIcon;
                    }
                }
            }
            collected_[request.first] = std::move(result);
        }
        // Bounded even when applications continuously spawn short-lived children.
        while (collected_.size() > 4096) collected_.erase(collected_.begin());
        if (SUCCEEDED(com)) CoUninitialize();
        return collected_;
    }

    std::shared_ptr<const Metadata> get(DWORD pid, bool icon, bool request)
    {
        auto latest = sampler_.latest();
        if (latest) displayed_ = std::move(latest);
        if (request && pid != 0 && pid != 4)
        {
            const ULONGLONG now = GetTickCount64();
            auto it = requested_.find(pid);
            if (it == requested_.end() || now - it->second.tick >= 30000 || (icon && !it->second.icon))
            {
                const bool wantIcon = icon || (it != requested_.end() && it->second.icon);
                requested_[pid] = {now, wantIcon};
                {
                    std::lock_guard<std::mutex> lock(queueMutex_);
                    queued_[pid] = queued_[pid] || wantIcon;
                }
                sampler_.request();
                while (requested_.size() > 4096) requested_.erase(requested_.begin());
            }
        }
        if (!displayed_) return nullptr;
        auto it = displayed_->find(pid);
        return it != displayed_->end() ? it->second : nullptr;
    }
public:
    ProcessMetadataCache() : sampler_([this](unsigned) { return collect(); }) {}
    ~ProcessMetadataCache() { sampler_.stop(); }
    std::wstring path(DWORD pid)
    {
        auto entry = get(pid, false, true);
        return entry ? entry->path : L"";
    }
    HICON icon(DWORD pid, bool request = true, int physicalPixels = 16)
    {
        auto entry = get(pid, true, request);
        if(!entry || !entry->icons || entry->icons->sizes.empty()) return nullptr;
        auto selected=entry->icons->sizes.lower_bound(physicalPixels);
        if(selected==entry->icons->sizes.end()) selected=std::prev(selected);
        return selected->second;
    }
    void clear()
    {
        sampler_.stop();
        displayed_.reset();
        collected_.clear();
        requested_.clear();
    }
};
