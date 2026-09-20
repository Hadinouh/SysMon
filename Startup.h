#pragma once
#include <windows.h>
#include <taskschd.h>
#include <sddl.h>
#include <string>
#include <vector>

namespace Startup
{
template<class T> struct ComPtr {
    T* value=nullptr;
    ~ComPtr() { if(value) value->Release(); }
    T* operator->() const { return value; }
    T** out() { return &value; }
};
struct Text {
    BSTR value;
    explicit Text(const wchar_t* text):value(SysAllocString(text)) {}
    ~Text() { SysFreeString(value); }
    operator BSTR() const { return value; }
};
struct Session {
    HRESULT initialized=CoInitializeEx(nullptr,COINIT_MULTITHREADED);
    ComPtr<ITaskService> service;
    ComPtr<ITaskFolder> folder;
    std::wstring sid, name;
    bool valid=false;
    Session() {
        HANDLE token=nullptr;
        if(!OpenProcessToken(GetCurrentProcess(),TOKEN_QUERY,&token)) return;
        DWORD bytes=0;
        GetTokenInformation(token,TokenUser,nullptr,0,&bytes);
        std::vector<BYTE> buffer(bytes);
        BOOL ok=GetTokenInformation(token,TokenUser,buffer.data(),bytes,&bytes);
        CloseHandle(token);
        if(!ok) return;
        LPWSTR text=nullptr;
        if(!ConvertSidToStringSidW(reinterpret_cast<TOKEN_USER*>(buffer.data())->User.Sid,&text)) return;
        sid=text; LocalFree(text);
        name=L"SysMon Startup "+sid;
        HRESULT hr=CoCreateInstance(CLSID_TaskScheduler,nullptr,CLSCTX_INPROC_SERVER,IID_ITaskService,reinterpret_cast<void**>(service.out()));
        if(FAILED(hr)) return;
        VARIANT empty; VariantInit(&empty);
        if(FAILED(service->Connect(empty,empty,empty,empty))) return;
        valid=SUCCEEDED(service->GetFolder(Text(L"\\"),folder.out()));
    }
    ~Session() {
        if(folder.value) { folder.value->Release(); folder.value=nullptr; }
        if(service.value) { service.value->Release(); service.value=nullptr; }
        if(SUCCEEDED(initialized)) CoUninitialize();
    }
};
inline bool enabled() {
    Session session;
    if(!session.valid) return false;
    ComPtr<IRegisteredTask> task;
    if(FAILED(session.folder->GetTask(Text(session.name.c_str()),task.out()))) return false;
    VARIANT_BOOL enabled=VARIANT_FALSE;
    return SUCCEEDED(task->get_Enabled(&enabled)) && enabled==VARIANT_TRUE;
}
inline bool configure(bool enabled, std::wstring* preview=nullptr) {
    Session session;
    if(!session.valid) return false;
    if(!enabled) {
        HRESULT hr=session.folder->DeleteTask(Text(session.name.c_str()),0);
        return SUCCEEDED(hr) || hr==HRESULT_FROM_WIN32(ERROR_FILE_NOT_FOUND);
    }
    wchar_t executable[32768]={};
    DWORD length=GetModuleFileNameW(nullptr,executable,32768);
    if(!length || length>=32768) return false;
    const std::wstring path(executable,length);
    const std::wstring working=path.substr(0,path.find_last_of(L"\\/"));
    ComPtr<ITaskDefinition> definition;
    if(FAILED(session.service->NewTask(0,definition.out()))) return false;
    ComPtr<IPrincipal> principal;
    if(FAILED(definition->get_Principal(principal.out())) ||
       FAILED(principal->put_UserId(Text(session.sid.c_str()))) ||
       FAILED(principal->put_LogonType(TASK_LOGON_INTERACTIVE_TOKEN)) ||
       FAILED(principal->put_RunLevel(TASK_RUNLEVEL_HIGHEST))) return false;
    ComPtr<ITaskSettings> settings;
    if(FAILED(definition->get_Settings(settings.out())) ||
       FAILED(settings->put_DisallowStartIfOnBatteries(VARIANT_FALSE)) ||
       FAILED(settings->put_StopIfGoingOnBatteries(VARIANT_FALSE)) ||
       FAILED(settings->put_ExecutionTimeLimit(Text(L"PT0S"))) ||
       FAILED(settings->put_MultipleInstances(TASK_INSTANCES_IGNORE_NEW))) return false;
    ComPtr<ITriggerCollection> triggers;
    ComPtr<ITrigger> trigger;
    ComPtr<ILogonTrigger> logon;
    if(FAILED(definition->get_Triggers(triggers.out())) ||
       FAILED(triggers->Create(TASK_TRIGGER_LOGON,trigger.out())) ||
       FAILED(trigger->QueryInterface(IID_ILogonTrigger,reinterpret_cast<void**>(logon.out()))) ||
       FAILED(logon->put_UserId(Text(session.sid.c_str())))) return false;
    ComPtr<IActionCollection> actions;
    ComPtr<IAction> action;
    ComPtr<IExecAction> execute;
    if(FAILED(definition->get_Actions(actions.out())) ||
       FAILED(actions->Create(TASK_ACTION_EXEC,action.out())) ||
       FAILED(action->QueryInterface(IID_IExecAction,reinterpret_cast<void**>(execute.out()))) ||
       FAILED(execute->put_Path(Text(path.c_str()))) ||
       FAILED(execute->put_WorkingDirectory(Text(working.c_str())))) return false;
    if(preview) {
        BSTR xml=nullptr;
        const HRESULT hr=definition->get_XmlText(&xml);
        if(SUCCEEDED(hr) && xml) { *preview=xml; SysFreeString(xml); }
        return SUCCEEDED(hr);
    }
    VARIANT user, empty; VariantInit(&user); VariantInit(&empty);
    user.vt=VT_BSTR; user.bstrVal=SysAllocString(session.sid.c_str());
    ComPtr<IRegisteredTask> task;
    HRESULT hr=session.folder->RegisterTaskDefinition(Text(session.name.c_str()),definition.value,TASK_CREATE_OR_UPDATE,
        user,empty,TASK_LOGON_INTERACTIVE_TOKEN,empty,task.out());
    VariantClear(&user);
    return SUCCEEDED(hr);
}
}
