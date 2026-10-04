#include "app/MaterialExportDisable.h"
#include "app/HookLifecycle.h"
#include <Windows.h>
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <thread>
#include <stdexcept>

namespace {
using namespace std::chrono_literals;
using namespace lholo::io;
std::size_t checks{};
void check(bool value,char const* reason) {++checks;if(!value)throw std::runtime_error(reason);}
template<class Predicate> bool until(Predicate predicate,std::chrono::milliseconds budget=10s) {
    auto const end=std::chrono::steady_clock::now()+budget;
    do {if(predicate())return true;std::this_thread::sleep_for(1ms);}while(std::chrono::steady_clock::now()<end);
    return predicate();
}
MaterialExportRequest request() {
    auto snapshot=std::make_shared<lholo::structure::detail::MaterialListSnapshot>();
    snapshot->source="ARTIFICIAL lifecycle fixture";snapshot->scope={1,1};
    return {snapshot,{}, {},"fixture"};
}
void testRetentionAndTimeout() {
    MaterialExportJob job;
    auto externalLoaderOwner=std::make_shared<int>(1);
    auto callbackOwner=externalLoaderOwner;
    std::shared_ptr<int> retained;
    std::weak_ptr<int> weak=externalLoaderOwner;
    std::atomic_bool entered{},release{};
    check(job.start(request(),[&](auto const&)->std::optional<std::filesystem::path> {
        entered=true;while(!release.load())std::this_thread::sleep_for(1ms);return std::nullopt;
    }),"start stalled job");
    bool const reached=until([&]{return entered.load();});
    auto const started=std::chrono::steady_clock::now();
    bool const refused=!lholo::app::prepareMaterialExportDisable(job,callbackOwner,retained,25ms);
    auto const elapsed=std::chrono::steady_clock::now()-started;
    bool const held=retained==callbackOwner && !weak.expired();
    auto const polledAt=std::chrono::steady_clock::now();auto const result=job.poll();
    auto const pollTime=std::chrono::steady_clock::now()-polledAt;
    bool const duplicateRejected=!job.start(request(),[](auto const&){return std::optional<std::filesystem::path>{};});
    bool const reopenRejected=!job.openSession();
    // Release before assertions: an assertion must never strand this fixture.
    release=true;bool const drained=job.closeAndDrain(10s);
    check(reached && refused && elapsed<1s,"stalled worker refuses disable within finite budget");
    check(held && result.phase==MaterialExportPhase::Saving && !result.accepting,"refusal retains module and worker and closes admission");
    check(pollTime<100ms,"Present poll does not wait for native/disk worker");
    check(duplicateRejected && reopenRejected,"refused disable prevents restart/new job races");
    check(drained && job.poll().phase==MaterialExportPhase::Cancelled,"stalled worker later drains without detach");
    externalLoaderOwner.reset();
    check(!lholo::app::prepareMaterialExportDisable(job,callbackOwner,retained,0ms) && retained,"never release last external owner inside DLL");
    externalLoaderOwner=callbackOwner;
    check(lholo::app::prepareMaterialExportDisable(job,callbackOwner,retained,0ms) && !retained,"loader-owned retry releases retention after drain");
    check(lholo::app::prepareMaterialExportDisable(job,callbackOwner,retained,0ms),"repeated disable drain is idempotent");
    check(!lholo::app::prepareMaterialExportDisable(job,std::shared_ptr<int>{},retained,0ms),"missing module ownership fails closed");
    check(job.openSession() && job.poll().accepting,"new enabled session reopens after drain");
}
void testBeforeTeardownAndRace() {
    for(int iteration=0;iteration<50;++iteration) {
        MaterialExportJob job;std::atomic_bool entered{};
        auto loader=std::make_shared<int>(1);auto owner=loader;std::shared_ptr<int> retained;
        check(lholo::app::hook_lifecycle::beginEnable(),"lifecycle starts running");
        check(job.start(request(),[&](auto const& cancellation)->std::optional<std::filesystem::path> {
            entered=true;while(!cancellation->cancelled())std::this_thread::yield();return std::nullopt;
        }),"cooperative job starts");
        check(until([&]{return entered.load();}),"worker entered");
        std::thread contender([&]{for(int n=0;n<100;++n){job.poll();job.start(request(),[](auto const&){return std::optional<std::filesystem::path>{};});}});
        bool const drained=lholo::app::prepareMaterialExportDisable(job,owner,retained,250ms);
        contender.join();
        check(lholo::app::hook_lifecycle::state()==lholo::app::hook_lifecycle::State::Running,"export gate preserves hooks before teardown");
        check(drained || lholo::app::prepareMaterialExportDisable(job,owner,retained,10s),"cancel/start/poll race eventually drains");
        lholo::app::hook_lifecycle::beginQuiesce();lholo::app::hook_lifecycle::waitForRunningCallbacks();
        lholo::app::hook_lifecycle::waitForQuiescence();lholo::app::hook_lifecycle::markDisabled();
        check(!job.poll().accepting && !job.start(request(),[](auto const&){return std::optional<std::filesystem::path>{};}),"no post-drain export admission");
    }
}
struct Fixture {
    HMODULE module{};
    bool (*start)(int){};DWORD (*thread)(){};void (*release)(){};
    bool (*close)(unsigned){};int (*phase)(){};bool (*canUnload)(){};
    template<class F> void bind(F& function,char const* name) {
        function=reinterpret_cast<F>(GetProcAddress(module,name));check(function!=nullptr,"fixture export exists");
    }
    explicit Fixture(std::filesystem::path const& path) {
        module=LoadLibraryW(path.c_str());check(module!=nullptr,"load private dialog fixture DLL");
        bind(start,"FixtureStart");bind(thread,"FixtureWorkerThread");bind(release,"FixtureRelease");
        bind(close,"FixtureCloseAndDrain");bind(phase,"FixturePhase");bind(canUnload,"FixtureCanUnload");
    }
    void unload() {
        check(canUnload(),"owned worker completed before FreeLibrary");
        check(FreeLibrary(module)!=FALSE,"actual fixture DLL unload succeeds");module=nullptr;
    }
    // A failing test leaves the module resident until process termination.
    // It never unloads while native hooks/worker code may still be executing.
};
BOOL CALLBACK findDialog(HWND window,LPARAM data) {
    wchar_t type[32]{};GetClassNameW(window,type,32);
    if(std::wstring_view(type)==L"#32770" && IsWindowVisible(window)) *reinterpret_cast<HWND*>(data)=window;
    return TRUE;
}
HWND dialogFor(DWORD worker) {HWND result{};if(worker)EnumThreadWindows(worker,findDialog,reinterpret_cast<LPARAM>(&result));return result;}
void nativeCases(std::filesystem::path const& dll) {
    // All native tests CANCEL. They never choose a destination or write files.
    for(int iteration=0;iteration<8;++iteration) {
        Fixture fixture(dll);check(fixture.start(0),"native dialog starts");
        HWND dialog{};check(until([&]{dialog=dialogFor(fixture.thread());return dialog!=nullptr;}),"actual common save dialog opens");
        auto const start=std::chrono::steady_clock::now();bool const immediate=fixture.close(250);
        check(std::chrono::steady_clock::now()-start<1s,"open-dialog disable returns within finite budget");
        check(immediate || until([&]{return fixture.close(0);}),"worker-owned native cancellation drains");
        check(fixture.phase()==static_cast<int>(MaterialExportPhase::Cancelled),"native cancel reports cancelled");
        check(!IsWindow(dialog) && !dialogFor(fixture.thread()),"native window/hooks destroyed before drain succeeds");
        fixture.unload();
    }
    Fixture before(dll);check(before.start(1) && until([&]{return before.thread()!=0;}),"pre-init chooser barrier");
    check(!before.close(25) && !before.canUnload(),"pre-init stall refuses unload");before.release();
    check(until([&]{return before.close(0);}) && before.phase()==static_cast<int>(MaterialExportPhase::Cancelled),"cancel before opening creates no dialog");
    check(!dialogFor(before.thread()),"pre-cancel never opens native window");before.unload();
    // Race cancellation against GetSaveFileNameW initialization repeatedly.
    for(int iteration=0;iteration<16;++iteration) {
        Fixture fixture(dll);check(fixture.start(0),"init race starts");
        if(iteration%2)check(until([&]{return fixture.thread()!=0;}),"init race worker entered");
        check(fixture.close(250) || until([&]{return fixture.close(0);}),"init race drains");
        check(fixture.phase()==static_cast<int>(MaterialExportPhase::Cancelled),"init race cancellation result");fixture.unload();
    }
    Fixture blocked(dll);check(blocked.start(2) && until([&]{return blocked.thread()!=0;}),"uncooperative native fixture starts");
    auto const start=std::chrono::steady_clock::now();check(!blocked.close(25),"uncooperative worker rejects disable");
    check(std::chrono::steady_clock::now()-start<1s && !blocked.canUnload(),"uncooperative worker rejects unload without unbounded wait");
    check(GetModuleHandleW(dll.filename().c_str())==blocked.module,"fixture module stays loaded after refusal");
    blocked.release();check(until([&]{return blocked.close(0);}),"retry after blocked chooser returns succeeds");blocked.unload();
}
}
int main(int argc,char** argv) {
    try {
        testRetentionAndTimeout();testBeforeTeardownAndRace();
        check(argc==2,"explicit private fixture DLL path required");nativeCases(std::filesystem::absolute(argv[1]));
        std::printf("Export lifecycle: %zu checks PASS; 8 open-dialog cancels, 16 init races, pre-open and stalled refusals; 26 real DLL load/unload cycles; no fixture files exported\n",checks);
        return 0;
    } catch(std::exception const& error) {
        std::fprintf(stderr,"Export lifecycle failed after %zu checks: %s\n",checks,error.what());
        // Test-only fail closed: terminate this fixture process, never detach a
        // live module or run CRT DLL destructors under an uncooperative worker.
        TerminateProcess(GetCurrentProcess(),1);return 1;
    }
}
