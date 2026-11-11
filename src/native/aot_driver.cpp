#include "vpp/native/aot.h"

#include <algorithm>
#include <atomic>
#include <cerrno>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iostream>
#include <iterator>
#include <memory>
#include <iomanip>
#include <set>
#include <sstream>
#include <stdexcept>
#if defined(_WIN32)
#include <windows.h>
#else
#include <fcntl.h>
#include <sys/wait.h>
#include <unistd.h>
#if defined(__APPLE__)
#include <mach-o/dyld.h>
#endif
#endif
#include "vpp/core/project_layout.h"
#include "vpp/native/target.h"
#include "vpp/core/text.h"
#include "vpp/runtime/error.h"

namespace vietvm::native::aot {
namespace {
namespace fs=std::filesystem;
class PhaseTimer {
    using Clock=std::chrono::steady_clock;
    Clock::time_point previous=Clock::now();
    bool enabled=false;
public:
    PhaseTimer() {
        const auto *value=std::getenv("VPP_NATIVE_PROFILE");
        enabled=value&&std::string(value)=="1";
    }
    bool active() const { return enabled; }
    void mark(const char *phase) {
        if(!enabled)return;
        const auto now=Clock::now();
        std::cerr<<"VPP_NATIVE_PROFILE "<<phase<<" "
                 <<std::chrono::duration_cast<std::chrono::milliseconds>(now-previous).count()
                 <<" ms\n";
        previous=now;
    }
};
ProcessResult process(const std::vector<std::string> &arguments, bool captureStderr = false,
                      const fs::path &stderrPath = {}) {
    ProcessResult result;
#if defined(_WIN32)
    // Windows CRT quoting, without a shell or command substitution.
    std::wstring command;
    for(const auto &argument:arguments) {
        if(!command.empty())command+=L' ';
        command+=L'"';unsigned slashes=0;
        for(wchar_t c:core::utf8Path(argument).wstring()) {
            if(c==L'\\'){++slashes;continue;}
            command.append(slashes*(c==L'"'?2:1),L'\\');slashes=0;
            if(c==L'"')command+=L'\\';command+=c;
        }
        command.append(slashes*2,L'\\');command+=L'"';
    }
    SECURITY_ATTRIBUTES security{sizeof(security),nullptr,TRUE};HANDLE input=nullptr,output=nullptr;
    if(!CreatePipe(&input,&output,&security,0))throw std::runtime_error("Native AOT: CreatePipe failed");
    SetHandleInformation(input,HANDLE_FLAG_INHERIT,0);
    HANDLE diagnostics=INVALID_HANDLE_VALUE;
    if(!stderrPath.empty()) {
        diagnostics=CreateFileW(stderrPath.c_str(),GENERIC_WRITE,FILE_SHARE_READ,&security,
                                CREATE_ALWAYS,FILE_ATTRIBUTE_NORMAL,nullptr);
        if(diagnostics==INVALID_HANDLE_VALUE) {
            CloseHandle(input);CloseHandle(output);
            throw std::runtime_error("Native AOT: cannot capture child diagnostics");
        }
    }
    STARTUPINFOW startup{};startup.cb=sizeof(startup);startup.dwFlags=STARTF_USESTDHANDLES;
    startup.hStdInput=GetStdHandle(STD_INPUT_HANDLE);startup.hStdOutput=output;
    startup.hStdError=diagnostics!=INVALID_HANDLE_VALUE?diagnostics:
        captureStderr?output:GetStdHandle(STD_ERROR_HANDLE);
    PROCESS_INFORMATION child{};
    if(!CreateProcessW(nullptr,command.data(),nullptr,nullptr,TRUE,0,nullptr,nullptr,&startup,&child)) {
        if(diagnostics!=INVALID_HANDLE_VALUE)CloseHandle(diagnostics);
        CloseHandle(input);CloseHandle(output);throw std::runtime_error("Native AOT: CreateProcess failed");
    }
    if(diagnostics!=INVALID_HANDLE_VALUE)CloseHandle(diagnostics);
    CloseHandle(output);char buffer[8192];DWORD count=0;
    while(ReadFile(input,buffer,sizeof(buffer),&count,nullptr)&&count)result.output.append(buffer,count);
    CloseHandle(input);WaitForSingleObject(child.hProcess,INFINITE);DWORD status=1;GetExitCodeProcess(child.hProcess,&status);
    CloseHandle(child.hProcess);CloseHandle(child.hThread);result.status=static_cast<int>(status);
    // Each native compiler/CLI layer relays the child's stdout via std::cout.
    // MSVC's text-mode stdout turns LF into CRLF at every layer; without
    // normalizing the captured text first, nesting produces CRCRLF (or more).
    // Only remove CR directly before LF, preserving standalone CR bytes.
    std::string normalized;
    normalized.reserve(result.output.size());
    for(std::size_t i=0;i<result.output.size();++i) {
        if(result.output[i]=='\r'&&i+1<result.output.size()&&result.output[i+1]=='\n')continue;
        normalized.push_back(result.output[i]);
    }
    result.output.swap(normalized);
#else
    int descriptors[2];if(pipe(descriptors)!=0)throw std::runtime_error("Native AOT: pipe failed");
    const auto child=fork();
    if(child<0){close(descriptors[0]);close(descriptors[1]);throw std::runtime_error("Native AOT: fork failed");}
    if(child==0) {
        close(descriptors[0]);if(dup2(descriptors[1],STDOUT_FILENO)<0)_exit(127);
        if(!stderrPath.empty()) {
            const int diagnostics=open(stderrPath.c_str(),O_WRONLY|O_CREAT|O_TRUNC,0600);
            if(diagnostics<0||dup2(diagnostics,STDERR_FILENO)<0)_exit(127);
            close(diagnostics);
        } else if(captureStderr&&dup2(descriptors[1],STDERR_FILENO)<0)_exit(127);
        close(descriptors[1]);
        std::vector<char *> argv;for(const auto &argument:arguments)argv.push_back(const_cast<char *>(argument.c_str()));argv.push_back(nullptr);
        execvp(argv[0],argv.data());_exit(127);
    }
    close(descriptors[1]);char buffer[8192];
    for(;;){const auto count=read(descriptors[0],buffer,sizeof(buffer));if(count>0)result.output.append(buffer,static_cast<std::size_t>(count));else if(count<0&&errno==EINTR)continue;else break;}
    close(descriptors[0]);int status=0;while(waitpid(child,&status,0)<0){if(errno!=EINTR)throw std::runtime_error("Native AOT: waitpid failed");}
    result.status=WIFEXITED(status)?WEXITSTATUS(status):128+(WIFSIGNALED(status)?WTERMSIG(status):0);
#endif
    return result;
}
struct TemporaryDirectory {
    fs::path path;
    explicit TemporaryDirectory(const fs::path &parent) {
        static std::atomic<unsigned long> sequence{0};
        for(unsigned i=0;i<100;++i) {
            path=parent/(".vpp-aot-"+std::to_string(++sequence)+"-"+std::to_string(
#if defined(_WIN32)
                GetCurrentProcessId()
#else
                getpid()
#endif
            ));
            std::error_code error;if(fs::create_directory(path,error))return;
        }
        throw std::runtime_error("Native AOT: cannot create temporary directory");
    }
    ~TemporaryDirectory(){std::error_code ignored;fs::remove_all(path,ignored);}
};
// Self-hosted AOT delegates directory allocation and tree cleanup to V++.
// The C++ guard only makes cleanup exception-safe during VM callbacks.
struct VppTemporaryDirectory {
    Program &program;
    fs::path path;
    VppTemporaryDirectory(Program &owner,const fs::path &parent={},bool create=true):program(owner) {
        if(!create) {path=parent;return;}
        const auto value=program.invokeName("biên dịch AOT tạo thư mục tạm",
                                            {parent.u8string()});
        const auto *created=std::get_if<std::string>(&value);
        if(!created||created->empty())
            throw std::runtime_error("Native AOT: invalid V++ temporary directory");
        path=core::utf8Path(*created);
    }
    ~VppTemporaryDirectory() noexcept {
        try {program.invokeName("biên dịch AOT dọn thư mục tạm",{path.u8string()});}
        catch(...) { /* Preserve an in-flight compiler error. */ }
    }
    VppTemporaryDirectory(const VppTemporaryDirectory &)=delete;
    VppTemporaryDirectory &operator=(const VppTemporaryDirectory &)=delete;
};
fs::path executableDirectory();
// Use the exact library selection rules for both linking and the optional
// integration executable cache. The cache must notice a different build tree.
fs::path linkLibraryRoot(const fs::path &root) {
    if(const char *configured=std::getenv("VPP_AOT_LIB_ROOT");configured&&*configured)
        return fs::absolute(core::utf8Path(configured));
    if(fs::exists(root/"lib")||fs::exists(root/"lib64"))return root;
    const auto exeDir=executableDirectory();
    const auto buildDir=exeDir.empty()?fs::path{}:
        (exeDir.filename()=="Release"||exeDir.filename()=="Debug"||
         exeDir.filename()=="RelWithDebInfo"||exeDir.filename()=="MinSizeRel"
            ? exeDir.parent_path().parent_path() : exeDir.parent_path());
    if(!buildDir.empty()&&(fs::exists(buildDir/"lib"/"Release")||
                            fs::exists(buildDir/"lib")))return buildDir;
    for(const char *candidate:{"build","cmake-build-debug","cmake-build-release","cmake-build-sanitized"})
        if(fs::exists(root/candidate/"lib"))return root/candidate;
    return root;
}

struct ExecutableCacheKey {
    std::uint64_t value=14695981039346656037ULL;
    void add(const std::string &part) {
        // Length delimiting prevents ambiguous concatenations of paths/flags.
        const auto length=std::to_string(part.size())+":";
        for(const auto &text:{length,part})for(unsigned char c:text) {
            value^=c;value*=1099511628211ULL;
        }
    }
    void file(const fs::path &path) {
        add(path.generic_u8string());
        std::error_code error;
        if(!fs::is_regular_file(path,error)||error) {add("missing");return;}
        const auto size=fs::file_size(path,error);
        if(error)throw std::runtime_error("Native AOT: cache input unreadable");
        const auto time=fs::last_write_time(path,error);
        if(error)throw std::runtime_error("Native AOT: cache input timestamp unreadable");
        add(std::to_string(size));
        add(std::to_string(std::chrono::duration_cast<std::chrono::nanoseconds>(
            time.time_since_epoch()).count()));
    }
    std::string hex() const {
        std::ostringstream output;
        output<<std::hex<<std::setfill('0')<<std::setw(16)<<value;
        return output.str();
    }
};

fs::path installation(bool required=true) {
    for(const char *name:{"VPP_AOT_ROOT","VPP_HOME"})if(const char *root=std::getenv(name);root&&*root)return fs::absolute(core::utf8Path(root));
    // An uninstalled native compiler in <build>/native-compiler (or its
    // multi-config subdirectory) can use the source checkout's V++ runtime
    // and ASM linker without requiring a manually exported VPP_HOME.
    const auto executable=executableDirectory();
    if(!executable.empty()) {
        for(const auto &buildDir:{executable.parent_path(),executable.parent_path().parent_path()}) {
            const auto sourceRoot=buildDir.parent_path();
            if(fs::is_regular_file(sourceRoot/core::utf8Path("gói/biên dịch/dòng lệnh máy.vi"))&&
               fs::is_directory(sourceRoot/"scripts/quality/native_aot_link"))
                return sourceRoot;
        }
    }
    if(!required)return {};
    throw std::runtime_error("Native AOT: set VPP_HOME to the installed toolchain");
}
fs::path executableDirectory() {
#if defined(_WIN32)
    std::wstring buffer(32768, L'\0');
    const auto length=GetModuleFileNameW(nullptr,buffer.data(),static_cast<DWORD>(buffer.size()));
    if(!length||length>=buffer.size())return {};
    buffer.resize(length);
    return fs::path(buffer).parent_path();
#elif defined(__APPLE__)
    std::uint32_t length=0;
    _NSGetExecutablePath(nullptr,&length);
    if(!length)return {};
    std::vector<char> buffer(length);
    if(_NSGetExecutablePath(buffer.data(),&length)!=0)return {};
    return fs::weakly_canonical(fs::path(buffer.data())).parent_path();
#else
    std::vector<char> buffer(4096);
    const auto length=readlink("/proc/self/exe",buffer.data(),buffer.size());
    if(length<=0||static_cast<std::size_t>(length)==buffer.size())return {};
    return fs::path(std::string(buffer.data(),static_cast<std::size_t>(length))).parent_path();
#endif
}
const std::vector<Value> &values(const Value &value) {
    const auto *list=std::get_if<runtime::ListHandle>(&value);
    if(!list||!*list)throw std::runtime_error("Native AOT: expected wire arrays");return (*list)->elements;
}

// Driver policy and filesystem/cache transactions run entirely in V++.
// The host marshals callbacks, relays diagnostics and launches user programs.
std::string driverString(const Value &value) {
    const auto *text=std::get_if<std::string>(&value);
    if(!text)throw std::runtime_error("Native AOT: invalid V++ driver string");
    return *text;
}
std::string legacyFfiSetting() {
#if defined(VPP_AOT_LINK_LEGACY_LIBFFI) && VPP_AOT_LINK_LEGACY_LIBFFI
    return "ON";
#else
    return "OFF";
#endif
}
fs::path vppInstallation(Program &program,bool required=true) {
    return core::utf8Path(driverString(program.invokeName("biên dịch AOT chọn gốc",
        {executableDirectory().u8string(),make_int_value(required)})));
}
void driverProfile(const Value &records) {
    for(const auto &record:values(records)) {
        const auto &fields=values(record);
        if(fields.size()!=2)throw std::runtime_error("Native AOT: invalid V++ profile record");
        std::cerr<<"VPP_NATIVE_PROFILE "<<driverString(fields[0])<<" "
                 <<driverString(fields[1])<<" ms\n";
    }
}
void compileAssemblyWithVpp(Program &program,const std::string &assembly,
                            const fs::path &output,const fs::path &root,
                            const fs::path &sessionLinkerDirectory) {
    const auto profile=program.invokeName("biên dịch AOT lắp ráp",
        {assembly,output.u8string(),root.u8string(),sessionLinkerDirectory.u8string(),
         canonicalTriple(hostTarget()),legacyFfiSetting(),executableDirectory().u8string()});
    driverProfile(profile);
}

int compilerCommand(Program &program,std::vector<Value> arguments,
                    const fs::path &sessionLinkerDirectory,bool captureErrors=false) {
    PhaseTimer profiler;
    const auto parsed=program.invokeName("biên dịch phân tích dòng lệnh máy",
                                      {make_list_value(std::move(arguments))});
    Frame roots(program);
    roots.stack.push_back(parsed);
    const auto normalized=program.invokeName("biên dịch AOT chuẩn hóa yêu cầu",{parsed});
    roots.stack.push_back(normalized);
    auto fields=values(normalized);
    if(fields.size()!=8)throw std::runtime_error("Native AOT: invalid CLI plan");
    const auto field=[&](std::size_t i) {return driverString(fields.at(i));};
    const auto output=core::utf8Path(field(2));
    const auto root=vppInstallation(program,false);
    const auto libraryRoot=driverString(program.invokeName("biên dịch AOT gốc gói",{root.u8string()}));
    std::string sourceIndex;
    if(field(0)=="run") {
        const auto cached=program.invokeName("biên dịch AOT tìm cache nguồn",
            {field(1),libraryRoot,root.u8string(),executableDirectory().u8string()});
        roots.stack.push_back(cached);
        const auto &cache=values(cached);
        if(cache.size()!=2)throw std::runtime_error("Native AOT: invalid V++ source cache result");
        sourceIndex=driverString(cache[0]);
        const auto hit=driverString(cache[1]);
        if(!hit.empty()) {
            const auto result=process({hit},captureErrors);
            program.output(result.output);
            return result.status;
        }
    }
    const auto prepared=program.invokeName("biên dịch chuẩn bị dòng lệnh máy",
        {make_list_value(std::move(fields)),libraryRoot,canonicalTriple(hostTarget()),
         program.compilerSessionCache});
    roots.stack.push_back(prepared);
    profiler.mark("vpp-cli-and-assembly");
    const auto &request=values(prepared);
    if(request.size()!=2)throw std::runtime_error("Native AOT: invalid host request");
    const auto operation=driverString(request[0]);
    const auto text=driverString(request[1]);
    if(operation=="complete") {program.output(text);return 0;}
    if(operation=="diagnostic-error") {program.output(text);return 1;}
    if(operation=="assemble") {
        compileAssemblyWithVpp(program,text,output,root,sessionLinkerDirectory);
        return 0;
    }
    if(operation!="execute")throw std::runtime_error("Native AOT: unknown host operation");
    const auto preparedRun=program.invokeName("biên dịch AOT chuẩn bị chạy",
        {text,root.u8string(),sessionLinkerDirectory.u8string(),canonicalTriple(hostTarget()),
         legacyFfiSetting(),executableDirectory().u8string(),sourceIndex,program.compilerSessionCache});
    roots.stack.push_back(preparedRun);
    const auto &run=values(preparedRun);
    if(run.size()!=3)throw std::runtime_error("Native AOT: invalid V++ executable request");
    VppTemporaryDirectory temporary(program,core::utf8Path(driverString(run[1])),false);
    driverProfile(run[2]);
    const auto result=process({driverString(run[0])},captureErrors);
    program.output(result.output);return result.status;
}

int compilerBatchTests(Program &program,int argc,char **argv,
                       const fs::path &sessionLinkerDirectory) {
    std::vector<Value> arguments;
    for(int i=2;i<argc;++i)arguments.emplace_back(argv[i]);
    const auto prepared=program.invokeName("biên dịch AOT chuẩn bị batch",
        {make_list_value(std::move(arguments))});
    Frame batchRoots(program);
    batchRoots.stack.push_back(prepared);
    const auto &batch=values(prepared);
    if(batch.size()!=5)throw std::runtime_error("Native AOT: invalid V++ batch plan");
    const auto originalCwd=driverString(batch[0]);
    const auto outputDir=driverString(batch[1]);
    const auto *setting=std::get_if<int>(&batch[2]);
    if(!setting)throw std::runtime_error("Native AOT: invalid V++ batch policy");
    const bool failFast=*setting!=0;
    program.compilerSessionCache=batch[3];
    const auto &sources=values(batch[4]);
    for(std::size_t i=0;i<sources.size();++i) {
        const auto &item=values(sources[i]);
        if(item.size()!=4)throw std::runtime_error("Native AOT: invalid V++ batch source");
        const auto source=driverString(item[0]);
        const auto started=std::chrono::steady_clock::now();
        std::cerr<<"[test "<<i+1<<'/'<<sources.size()<<"] START vpp "<<source<<std::endl;
        std::ostringstream captured;
        const auto savedOutput=program.output;
        program.output=[&captured](const std::string &text){captured<<text;};
        auto *savedErrors=std::cerr.rdbuf(captured.rdbuf());
        int status=1;
        try {
            // Match `vpp-cli run --compiler=vpp`: relative fixture I/O is
            // based on the source directory. Test programs run in children,
            // so their globals, environment and runtime heaps stay isolated.
            program.invokeName("biên dịch AOT đổi thư mục",{item[1]});
            status=compilerCommand(program,{std::string("run"),source},
                                   sessionLinkerDirectory,true);
        } catch(const std::exception &error) {
            captured<<error.what()<<'\n';
        } catch(...) {
            captured<<"Native AOT: unknown failure\n";
        }
        program.output=savedOutput;
        std::cerr.rdbuf(savedErrors);
        program.invokeName("biên dịch AOT đổi thư mục",{originalCwd});
        const auto base=driverString(item[2]);
        const auto elapsed=std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now()-started).count();
        const auto expectedPath=driverString(item[3]);
        const auto evidence=program.invokeName("biên dịch AOT ghi kết quả batch",
            {outputDir,base,captured.str(),status,
             std::to_string(elapsed),failFast,expectedPath});
        const auto *passed=std::get_if<int>(&evidence);
        if(!passed)throw std::runtime_error("Native AOT: invalid V++ batch evidence result");
        std::cerr<<"[test "<<i+1<<'/'<<sources.size()<<"] DONE vpp "<<source
                 <<" exit="<<status<<" elapsed_ms="<<elapsed<<std::endl;
        if(failFast) {
            if(*passed==0) {
                std::cerr<<"GOLDEN FAIL "<<source<<" (exit="<<status
                         <<", expected="<<expectedPath<<")\n";
                return 1;
            }
        }
        // Retain only the explicitly rooted memo and compiler globals.
        program.collect();
    }
    return 0;
}

int compilerMain(Program &program,int argc,char **argv) {
    // V++ creates and destroys the private linker workspace. Its lifetime is
    // one compiler session, so batch sources can reuse a configured CMake tree.
    VppTemporaryDirectory sessionLinker(program);
    if(argc>1&&std::string(argv[1])=="--batch-run-tests")
        return compilerBatchTests(program,argc,argv,sessionLinker.path);
    std::vector<Value> arguments;
    for(int i=1;i<argc;++i)arguments.emplace_back(argv[i]);
    return compilerCommand(program,std::move(arguments),sessionLinker.path);
}
} // namespace

ProcessResult runProcess(const std::vector<std::string> &arguments) {
    return runProcess(arguments,false);
}
ProcessResult runProcess(const std::vector<std::string> &arguments, bool captureStderr) {
    if (arguments.empty()) throw std::invalid_argument("Native AOT: empty process arguments");
    return process(arguments,captureStderr);
}

void compileExecutable(const Snapshot &snapshot,const fs::path &output,const fs::path &root,bool compiler) {
    PhaseTimer profiler;
    const auto assembly=emitAssembly(snapshot,compiler);
    profiler.mark("emit-native-assembly");
    compileAssembly(assembly,output,root);
}

// The Linux freestanding runtime consists entirely of V++ object code and
// platform assembly. Build its ELF directly, without configuring a CMake
// project for every source file. This helper is compiled on all hosts so the
// command construction is type-checked even when Linux CI executes it.
[[maybe_unused]] static void linkLinuxFreestanding(
    const fs::path &source,const fs::path &executable,const fs::path &absolute,
    const fs::path &root,const fs::path &temporary,PhaseTimer &profiler) {
    fs::path runtimeObject=root/"build/native-vpp-runtime/runtime.o";
    if(const char *configured=std::getenv("VPP_AOT_VPP_OBJECT");configured&&*configured)
        runtimeObject=core::utf8Path(configured);
    if(!runtimeObject.is_absolute()||!fs::is_regular_file(runtimeObject))
        throw std::runtime_error("Native AOT: Linux V++ runtime object missing: "+runtimeObject.u8string());

    const auto asmRoot=root/"scripts/ASM";
    const auto programObject=temporary/"program.o";
    const auto checked=[&](const std::vector<std::string> &command,const std::string &what) {
        const auto result=process(command,true);
        if(result.status)
            throw std::runtime_error("Native AOT: Linux "+what+" failed ("+
                std::to_string(result.status)+")\n"+result.output);
    };
    const std::vector<std::string> common={"-target","x86_64-unknown-linux-gnu", "-c"};
    const std::vector<std::string> include={"-I"+asmRoot.u8string(),
                                             "-I"+(asmRoot/"common").u8string()};
    checked({"clang","-target","x86_64-unknown-linux-gnu","-c",source.u8string(),
             "-DVPP_AOT_VPP_RUNTIME=1",include[0],include[1],
             "-o",programObject.u8string()},"program assembly");
    profiler.mark("assemble-linux-vpp-program");

    // Cover every shared and platform .inc/.S dependency. Immutable cache
    // objects are published with hard links so concurrent workers never load
    // a partially assembled object.
    std::uint64_t digest=14695981039346656037ULL;
    const auto hashBytes=[&](const char *data,std::size_t size) {
        for(std::size_t i=0;i<size;++i) {
            digest^=static_cast<unsigned char>(data[i]);
            digest*=1099511628211ULL;
        }
    };
    const std::string version="linux-x86_64-vpp-freestanding-direct-1";
    hashBytes(version.data(),version.size());
    std::vector<fs::path> dependencies;
    for(const auto &entry:fs::recursive_directory_iterator(asmRoot)) {
        if(entry.is_regular_file()&&
           (entry.path().extension()==".inc"||entry.path().extension()==".S"||
            entry.path().extension()==".s"))
            dependencies.push_back(entry.path());
    }
    std::sort(dependencies.begin(),dependencies.end());
    for(const auto &dependency:dependencies) {
        const auto name=dependency.lexically_relative(asmRoot).generic_u8string();
        hashBytes(name.data(),name.size());
        std::ifstream contents(dependency,std::ios::binary);
        if(!contents)
            throw std::runtime_error("Native AOT: unreadable Linux ASM source: "+dependency.u8string());
        char buffer[4096];
        while(contents.read(buffer,sizeof(buffer))||contents.gcount()>0)
            hashBytes(buffer,static_cast<std::size_t>(contents.gcount()));
        if(contents.bad())
            throw std::runtime_error("Native AOT: cannot hash Linux ASM source: "+dependency.u8string());
    }
    const auto cacheDirectory=runtimeObject.parent_path()/".vpp-aot-link-cache";
    std::error_code cacheError;
    fs::create_directories(cacheDirectory,cacheError);
    const bool useCache=!cacheError&&fs::is_directory(cacheDirectory);
    const std::vector<std::pair<std::string,std::string>> components={
        {"start","vpp_linux_start_x86_64.S"},
        {"syscalls","vpp_linux_syscalls_x86_64.S"},
        {"runtime-core","vpp_linux_runtime_core_x86_64.S"},
        {"directory","vpp_linux_directory_x86_64.S"},
        {"sha256","vpp_linux_sha256_x86_64.S"},
        {"process","vpp_linux_process_x86_64.S"},
        {"socket","vpp_linux_socket_x86_64.S"},
        {"bridge","vpp_linux_aot_runtime_bridge_x86_64.S"},
        {"numeric","vpp_linux_f64_x86_64.S"}
    };
    std::vector<std::string> link={"clang","-target","x86_64-unknown-linux-gnu",
        "-nostdlib","-static","-no-pie","-Wl,-e,_start","-Wl,--no-undefined",
        "-Wl,--build-id=none","-Wl,-z,noexecstack",programObject.u8string()};
    for(const auto &[name,file]:components) {
        const auto local=temporary/(name+".o");
        const auto cached=cacheDirectory/(name+"-"+std::to_string(digest)+".o");
        if(useCache&&fs::is_regular_file(cached)&&fs::file_size(cached)>0) {
            link.push_back(cached.u8string());
            continue;
        }
        checked({"clang",common[0],common[1],common[2],
                 (asmRoot/"linux"/file).u8string(),include[0],include[1],
                 "-o",local.u8string()},name+" assembly");
        fs::path object=local;
        if(useCache) {
            std::error_code publishError;
            fs::create_hard_link(local,cached,publishError);
            if((!publishError||fs::is_regular_file(cached))&&fs::file_size(cached)>0)
                object=cached;
        }
        link.push_back(object.u8string());
    }
    profiler.mark("prepare-linux-vpp-primitives");
    link.push_back(runtimeObject.u8string());
    link.insert(link.end(),{"-o",executable.u8string()});
    checked(link,"freestanding executable link");
    profiler.mark("link-linux-vpp-executable");
    checked({"/bin/bash",(root/"scripts/quality/audit_linux_static_elf.sh").u8string(),
             executable.u8string()},"freestanding ELF audit");
    if(std::getenv("VPP_AOT_KEEP_ASM"))
        fs::copy_file(source,fs::path(absolute.u8string()+".S"),fs::copy_options::overwrite_existing);
    fs::rename(executable,absolute);
}

// Stage 0 on Linux uses the bootstrap C ABI and the project's C++ archives.
// Its object files can be assembled independently of the nested CMake linker
// project. Keep the freestanding runtime on the separate path above: it has
// a different entry point, runtime and link requirements.
[[maybe_unused]] static void linkLinuxStage0(
    const fs::path &source,const fs::path &executable,const fs::path &absolute,
    const fs::path &root,const fs::path &libraries,const fs::path &temporary,
    PhaseTimer &profiler) {
    const auto asmRoot=root/"scripts/ASM";
    const auto runtimeSource=root/"src/runtime/native/vpp_aot_runtime.c";
#if defined(__aarch64__)
    const auto machineSource=asmRoot/"linux/vpp_aot_stage0_machine_aarch64.S";
    const std::string platform="linux-aarch64";
#else
    const auto machineSource=asmRoot/"linux/vpp_aot_stage0_machine_x86_64.S";
    const std::string platform="linux-x86_64";
#endif
    const auto programObject=temporary/"program.o";
    const auto localRuntime=temporary/"bootstrap-runtime.o";
    const auto localMachine=temporary/"stage0-machine.o";
    const std::string cCompiler=[] {
        const char *value=std::getenv("VPP_AOT_C_COMPILER");
        return value&&*value?std::string(value):std::string("cc");
    }();
    const std::string cxxCompiler=[] {
        const char *value=std::getenv("CXX");
        return value&&*value?std::string(value):std::string("c++");
    }();
    const auto checked=[&](const std::vector<std::string> &command,const char *step) {
        const auto result=process(command,true);
        if(result.status)
            throw std::runtime_error(std::string("Native AOT: Linux Stage 0 ")+step+
                " failed ("+std::to_string(result.status)+")\n"+result.output);
    };
    const std::string asmInclude="-I"+asmRoot.u8string();
    const std::string commonInclude="-I"+(asmRoot/"common").u8string();
    checked({cxxCompiler,"-c",source.u8string(),asmInclude,commonInclude,
             "-o",programObject.u8string()},"program assembly");
    profiler.mark("assemble-stage0-program");

    // Hash transitive assembler includes and runtime source, then publish
    // complete, immutable object files using hard links for parallel builds.
    ExecutableCacheKey hash;
    hash.add("linux-stage0-direct-1");
    hash.add(platform);
    hash.add(cCompiler);
    hash.add(cxxCompiler);
    std::vector<fs::path> dependencies{runtimeSource,machineSource};
    for(const auto &folder:{asmRoot/"linux",asmRoot/"common"}) {
        if(!fs::is_directory(folder))continue;
        for(const auto &entry:fs::recursive_directory_iterator(folder))
            if(entry.is_regular_file()&&
               (entry.path().extension()==".inc"||entry.path().extension()==".S"))
                dependencies.push_back(entry.path());
    }
    std::sort(dependencies.begin(),dependencies.end());
    for(const auto &dependency:dependencies) {
        hash.add(dependency.lexically_relative(root).generic_u8string());
        std::ifstream content(dependency,std::ios::binary);
        if(!content)throw std::runtime_error("Native AOT: unreadable Stage 0 dependency: "+dependency.u8string());
        char bytes[4096];
        while(content.read(bytes,sizeof(bytes))||content.gcount()>0)
            hash.add(std::string(bytes,static_cast<std::size_t>(content.gcount())));
        if(content.bad())throw std::runtime_error("Native AOT: cannot hash Stage 0 dependency: "+dependency.u8string());
    }
    const auto cacheDirectory=libraries/"native-stage0-direct-cache";
    std::error_code cacheError;
    fs::create_directories(cacheDirectory,cacheError);
    const bool cacheAvailable=!cacheError&&fs::is_directory(cacheDirectory);
    const auto cached=[&](const fs::path &local,const std::string &name,
                          const std::vector<std::string> &command) -> fs::path {
        const auto target=cacheDirectory/(name+"-"+hash.hex()+".o");
        if(cacheAvailable&&fs::is_regular_file(target)&&fs::file_size(target)>0)
            return target;
        checked(command,name.c_str());
        if(cacheAvailable) {
            std::error_code linkError;
            fs::create_hard_link(local,target,linkError);
            if((!linkError||fs::is_regular_file(target))&&fs::file_size(target)>0)
                return target;
        }
        return local;
    };
    const auto runtimeObject=cached(localRuntime,"bootstrap-runtime",
        {cCompiler,"-std=c11","-c",runtimeSource.u8string(),"-o",localRuntime.u8string()});
    const auto machineObject=cached(localMachine,"stage0-machine",
        {cCompiler,"-c",machineSource.u8string(),asmInclude,commonInclude,
         "-o",localMachine.u8string()});
    profiler.mark("prepare-stage0-runtime");

    std::vector<std::string> link={cxxCompiler,programObject.u8string(),
        runtimeObject.u8string(),machineObject.u8string(),"-Wl,--strip-all"};
    for(const char *name:{"native-aot","runtime-native","compiler","frontend","bytecode","core"}) {
        bool found=false;
        for(const char *folder:{"lib/Release","lib64/Release","lib","lib64"}) {
            const auto archive=libraries/folder/(std::string("libvpp-")+name+".a");
            if(fs::is_regular_file(archive)) {
                link.push_back(archive.u8string());
                found=true;
                break;
            }
        }
        if(!found)throw std::runtime_error(std::string("Native AOT: missing Linux Stage 0 archive: vpp-")+name);
    }
#if defined(VPP_AOT_LINK_LEGACY_LIBFFI) && VPP_AOT_LINK_LEGACY_LIBFFI
    link.push_back("-lffi");
#endif
    // Match the nested CMake link: both OpenSSL archives are static, while
    // the executable may still use the host C/C++ runtime and libdl.
    // Respect a custom OpenSSL installation selected by the parent build.
    // The generic -l fallback works for standard multiarch system paths.
    fs::path sslArchive,cryptoArchive;
    std::vector<std::string> cryptoStaticDeps;
    std::ifstream configuration(libraries/"CMakeCache.txt");
    for(std::string line;std::getline(configuration,line);) {
        constexpr const char *sslKey="OPENSSL_SSL_LIBRARY:FILEPATH=";
        constexpr const char *cryptoKey="OPENSSL_CRYPTO_LIBRARY:FILEPATH=";
        constexpr const char *depsKey="VPP_OPENSSL_STATIC_DEPS:STRING=";
        if(line.find(sslKey)==0)sslArchive=core::utf8Path(line.substr(std::strlen(sslKey)));
        if(line.find(cryptoKey)==0)cryptoArchive=core::utf8Path(line.substr(std::strlen(cryptoKey)));
        if(line.find(depsKey)==0) {
            std::istringstream flags(line.substr(std::strlen(depsKey)));
            for(std::string flag;std::getline(flags,flag,';');)
                if(!flag.empty())cryptoStaticDeps.push_back(flag);
        }
    }
    if(sslArchive.extension()==".a"&&cryptoArchive.extension()==".a"&&
       fs::is_regular_file(sslArchive)&&fs::is_regular_file(cryptoArchive)) {
        link.push_back(sslArchive.u8string());
        link.push_back(cryptoArchive.u8string());
        link.push_back("-Wl,-Bstatic");
    } else {
        link.insert(link.end(),{"-Wl,-Bstatic","-lssl","-lcrypto"});
    }
    // Static libcrypto has additional dependencies on recent Linux distros.
    // Their ordered flags come from the parent CMake/pkg-config configure.
    link.insert(link.end(),cryptoStaticDeps.begin(),cryptoStaticDeps.end());
    link.push_back("-Wl,-Bdynamic");
    link.insert(link.end(),{"-pthread","-ldl","-o",executable.u8string()});
    checked(link,"executable link");
    profiler.mark("link-stage0-executable");
    if(std::getenv("VPP_AOT_KEEP_ASM"))
        fs::copy_file(source,fs::path(absolute.u8string()+".S"),fs::copy_options::overwrite_existing);
    fs::rename(executable,absolute);
}

#if defined(_WIN32) && defined(_MSC_VER)
// Experimental direct MSVC link. The Visual Studio toolchain must already be
// on PATH (e.g. in a Developer Command Prompt). Opt in explicitly until its
// ABI parity is exercised on a Windows runner.
static void linkWindowsStage0(
    const fs::path &source,const fs::path &executable,const fs::path &absolute,
    const fs::path &root,const fs::path &libraries,const fs::path &temporary,
    PhaseTimer &profiler) {
    const auto asmRoot=root/"scripts/ASM";
    const auto runtimeSource=root/"src/runtime/native/vpp_aot_runtime.c";
    const auto machineSource=asmRoot/"windows/vpp_aot_primitives_windows_x86_64.S";
    const auto programObject=temporary/"program.obj";
    const auto runtimeObject=temporary/"bootstrap-runtime.obj";
    const auto machineObject=temporary/"stage0-machine.obj";
    const auto tool=[](const char *key,const char *fallback) {
        const char *value=std::getenv(key);
        return value&&*value?std::string(value):std::string(fallback);
    };
    const auto assembler=tool("VPP_AOT_ASSEMBLER","clang");
    const auto compiler=tool("VPP_AOT_MSVC_CL","cl");
    const auto linker=tool("VPP_AOT_MSVC_LINK","link");
    const auto checked=[&](const std::vector<std::string> &command,const char *step) {
        const auto result=process(command,true);
        if(result.status)
            throw std::runtime_error(std::string("Native AOT: Windows Stage 0 ")+step+
                " failed ("+std::to_string(result.status)+")\n"+result.output);
    };
    checked({assembler,"-target","x86_64-pc-windows-msvc","-c",source.u8string(),
             "-I"+asmRoot.u8string(),"-I"+(asmRoot/"common").u8string(),
             "-o",programObject.u8string()},"program assembly");
    profiler.mark("assemble-stage0-program");
    checked({compiler,"/nologo","/std:c11","/O2","/DNDEBUG","/MT","/c",
             runtimeSource.u8string(),"/Fo"+runtimeObject.u8string()},
            "bootstrap runtime compilation");
    checked({assembler,"-target","x86_64-pc-windows-msvc","-c",
             "-DVPP_WINDOWS_STAGE0=1","-DVPP_NATIVE_RUNTIME_STACK_ABI=0",
             machineSource.u8string(),"-o",machineObject.u8string()},
            "machine primitives assembly");
    profiler.mark("prepare-stage0-runtime");
    std::vector<std::string> link={linker,"/NOLOGO","/MACHINE:X64",
        "/SUBSYSTEM:CONSOLE","/INCREMENTAL:NO","/OPT:REF","/OPT:ICF",
        "/OUT:"+executable.u8string(),programObject.u8string(),
        runtimeObject.u8string(),machineObject.u8string()};
    for(const char *name:{"native-aot","runtime-native","compiler","frontend","bytecode","core"}) {
        bool found=false;
        for(const char *folder:{"lib/Release","lib64/Release","lib","lib64"}) {
            const auto archive=libraries/folder/(std::string("vpp-")+name+".lib");
            if(fs::is_regular_file(archive)) {
                link.push_back(archive.u8string());
                found=true;
                break;
            }
        }
        if(!found)throw std::runtime_error(std::string("Native AOT: missing Windows Stage 0 archive: vpp-")+name);
    }
    link.insert(link.end(),{"ws2_32.lib","bcrypt.lib","secur32.lib","crypt32.lib",
        "shell32.lib","kernel32.lib","user32.lib","gdi32.lib","winspool.lib",
        "ole32.lib","oleaut32.lib","uuid.lib","comdlg32.lib","advapi32.lib"});
    checked(link,"executable link");
    profiler.mark("link-stage0-executable");
    if(std::getenv("VPP_AOT_KEEP_ASM"))
        fs::copy_file(source,fs::path(absolute.u8string()+".S"),fs::copy_options::overwrite_existing);
    fs::rename(executable,absolute);
}
#endif

void compileAssembly(const std::string &text,const fs::path &output,const fs::path &root) {
    PhaseTimer profiler;
    const auto absolute=fs::absolute(output);if(!fs::is_directory(absolute.parent_path()))throw std::runtime_error("Native AOT: output directory missing");
    TemporaryDirectory temporary(absolute.parent_path());const auto source=temporary.path/"program.S";
    {std::ofstream assembly(source,std::ios::binary);assembly<<text;if(!assembly)throw std::runtime_error("Native AOT: assembly write failed");}
    profiler.mark("write-native-assembly");
    const auto executable=temporary.path/
#if defined(_WIN32)
        "program.exe";
#else
        "program";
#endif
#if defined(__APPLE__) && defined(__aarch64__)
    // The self-hosted macOS runtime only needs V++-emitted ASM, the V++
    // runtime object and the platform ASM primitives. Running a fresh CMake
    // configure/build for each test source dominates short AOT executions.
    // Keep CMake available for parity checks with VPP_AOT_DIRECT_LINK=0.
    const char *runtimeChoice=std::getenv("VPP_AOT_RUNTIME");
    const char *directChoice=std::getenv("VPP_AOT_DIRECT_LINK");
    if(runtimeChoice&&std::string(runtimeChoice)=="vpp"&&
       !(directChoice&&std::string(directChoice)=="0")) {
        fs::path runtimeObject=root/"build/native-vpp-runtime/runtime.o";
        if(const char *configured=std::getenv("VPP_AOT_VPP_OBJECT");configured&&*configured)
            runtimeObject=core::utf8Path(configured);
        if(!runtimeObject.is_absolute()||!fs::is_regular_file(runtimeObject))
            throw std::runtime_error("Native AOT: V++ runtime object missing: "+runtimeObject.u8string());
        const auto asmRoot=root/"scripts/ASM";
        const auto programObject=temporary.path/"program.o";
        const auto primitiveSource=asmRoot/"macos/vpp_aot_primitives_aarch64.S";
        const auto localPrimitive=temporary.path/"primitives.o";
        const auto runChecked=[&](std::vector<std::string> command,const char *description) {
            const auto result=process(command,true);
            if(result.status)throw std::runtime_error(std::string("Native AOT: ")+description+
                " failed ("+std::to_string(result.status)+")\n"+result.output);
        };
        runChecked({"clang","-c",source.u8string(),"-DVPP_AOT_VPP_RUNTIME=1",
            "-I"+asmRoot.u8string(),"-I"+(asmRoot/"common").u8string(),
            "-o",programObject.u8string()},"V++ program assembly");
        profiler.mark("assemble-vpp-program");

        // Cache just the immutable platform primitives, not program output.
        // Hash their source contents (including transitive ASM includes) so
        // concurrent tests never reuse an object built from older sources.
        std::uint64_t digest=14695981039346656037ULL;
        const auto hashBytes=[&](const char *bytes,std::size_t count) {
            for(std::size_t i=0;i<count;++i) {
                digest^=static_cast<unsigned char>(bytes[i]);
                digest*=1099511628211ULL;
            }
        };
        const std::string cacheVersion="macos-arm64-vpp-stack-abi-1";
        hashBytes(cacheVersion.data(),cacheVersion.size());
        std::vector<fs::path> sources;
        for(const auto &entry:fs::recursive_directory_iterator(asmRoot)) {
            if(entry.is_regular_file()&&
               (entry.path().extension()==".S"||entry.path().extension()==".inc"))
                sources.push_back(entry.path());
        }
        std::sort(sources.begin(),sources.end());
        for(const auto &input:sources) {
            const auto name=input.lexically_relative(asmRoot).generic_u8string();
            hashBytes(name.data(),name.size());
            std::ifstream stream(input,std::ios::binary);
            if(!stream)throw std::runtime_error("Native AOT: cannot read ASM dependency: "+input.u8string());
            char buffer[4096];
            while(stream.read(buffer,sizeof(buffer))||stream.gcount()>0)
                hashBytes(buffer,static_cast<std::size_t>(stream.gcount()));
            if(stream.bad())throw std::runtime_error("Native AOT: cannot hash ASM dependency: "+input.u8string());
        }
        const fs::path cacheDirectory=runtimeObject.parent_path()/".vpp-aot-link-cache";
        std::error_code cacheError;
        fs::create_directories(cacheDirectory,cacheError);
        const fs::path cachedPrimitive=cacheDirectory/("primitives-"+std::to_string(digest)+".o");
        const bool cacheAvailable=!cacheError&&fs::is_directory(cacheDirectory);
        fs::path primitiveObject=localPrimitive;
        if(cacheAvailable&&fs::is_regular_file(cachedPrimitive)&&fs::file_size(cachedPrimitive)>0) {
            primitiveObject=cachedPrimitive;
            profiler.mark("reuse-vpp-primitives");
        } else {
            runChecked({"clang","-c",primitiveSource.u8string(),
                "-DVPP_NATIVE_RUNTIME_STACK_ABI=1","-o",localPrimitive.u8string()},
                "V++ platform primitives assembly");
            profiler.mark("assemble-vpp-primitives");
            if(cacheAvailable) {
                // A hard link publishes a complete object atomically; races
                // from parallel CTest workers safely retain the first copy.
                std::error_code linkError;
                fs::create_hard_link(localPrimitive,cachedPrimitive,linkError);
                if(!linkError||fs::is_regular_file(cachedPrimitive))
                    primitiveObject=cachedPrimitive;
            }
        }
        runChecked({"clang",programObject.u8string(),primitiveObject.u8string(),
            runtimeObject.u8string(),"-Wl,-dead_strip","-o",executable.u8string()},
            "V++ executable link");
        profiler.mark("link-vpp-executable");
        if(std::getenv("VPP_AOT_KEEP_ASM"))
            fs::copy_file(source,fs::path(absolute.u8string()+".S"),fs::copy_options::overwrite_existing);
        fs::rename(executable,absolute);
        return;
    }
#endif
#if defined(__linux__) && defined(__x86_64__)
    // Static Linux ELF output needs no Stage 0 archives, C runtime, or
    // nested CMake build. VPP_AOT_DIRECT_LINK=0 exercises the CMake oracle.
    if(const char *runtime=std::getenv("VPP_AOT_RUNTIME");
       runtime&&std::string(runtime)=="vpp-freestanding") {
        const char *choice=std::getenv("VPP_AOT_DIRECT_LINK");
        if(!choice||std::string(choice)!="0") {
            linkLinuxFreestanding(source,executable,absolute,root,temporary.path,profiler);
            return;
        }
    }
#endif
    // Installed toolchains keep libraries under <root>/lib. A developer's
    // Stage 0 checkout can instead use the existing CMake build libraries.
    const fs::path libraries=linkLibraryRoot(root);
#if defined(_WIN32) && defined(_MSC_VER)
    const char *windowsRuntime=std::getenv("VPP_AOT_RUNTIME");
    const char *windowsDirect=std::getenv("VPP_AOT_WINDOWS_DIRECT_LINK");
    if((!windowsRuntime||!*windowsRuntime||std::string(windowsRuntime)=="cpp")&&
       windowsDirect&&std::string(windowsDirect)=="1") {
        linkWindowsStage0(source,executable,absolute,root,libraries,temporary.path,profiler);
        return;
    }
#endif
#if defined(__linux__) && (defined(__x86_64__) || defined(__aarch64__))
    const char *linuxRuntime=std::getenv("VPP_AOT_RUNTIME");
    const char *linuxDirect=std::getenv("VPP_AOT_DIRECT_LINK_CPP");
    if((!linuxRuntime||!*linuxRuntime||std::string(linuxRuntime)=="cpp")&&
       (!linuxDirect||std::string(linuxDirect)!="0")) {
        // Instrumented libraries need coverage/sanitizer link flags. The
        // existing CMake path derives them from the selected build cache.
        bool instrumented=false;
        std::ifstream configuration(libraries/"CMakeCache.txt");
        for(std::string line;std::getline(configuration,line);) {
            if(line=="VPP_ENABLE_COVERAGE:BOOL=ON"||
               ((line.find("CMAKE_CXX_FLAGS:STRING=")==0||
                 line.find("CMAKE_EXE_LINKER_FLAGS:STRING=")==0)&&
                line.find("-fsanitize=")!=std::string::npos)) {
                instrumented=true;
                break;
            }
        }
        // A shared-library build has no static Stage 0 archive set. Let the
        // CMake oracle select its shared libraries instead of forcing .a.
        bool hasStaticArchives=true;
        for(const char *name:{"native-aot","runtime-native","compiler","frontend","bytecode","core"}) {
            bool found=false;
            for(const char *folder:{"lib/Release","lib64/Release","lib","lib64"}) {
                if(fs::is_regular_file(libraries/folder/(std::string("libvpp-")+name+".a"))) {
                    found=true;
                    break;
                }
            }
            if(!found) {hasStaticArchives=false;break;}
        }
        if(!instrumented&&hasStaticArchives) {
            linkLinuxStage0(source,executable,absolute,root,libraries,temporary.path,profiler);
            return;
        }
    }
#endif
#if defined(__APPLE__) && defined(__aarch64__)
    // The default Stage 0 AOT link otherwise configures a fresh CMake project
    // for every golden fixture. Opt in during integration after parity checks;
    // this uses the same bootstrap ABI and archives, without invoking the VM.
    const char *directCpp=std::getenv("VPP_AOT_DIRECT_LINK_CPP");
    const char *selectedRuntime=std::getenv("VPP_AOT_RUNTIME");
    // Assemble and link directly by default for ordinary Stage 0 builds.
    // Keep an explicit CMake opt-out for ABI parity and linker-cache tests.
    if((!directCpp||std::string(directCpp)!="0")&&
       (!selectedRuntime||!*selectedRuntime||std::string(selectedRuntime)=="cpp")) {
        // Instrumented archives require the flags recovered by the CMake
        // linker below. Keep that path for coverage and sanitizer builds.
        bool instrumented=false;
        std::ifstream configuration(libraries/"CMakeCache.txt");
        for(std::string line;std::getline(configuration,line);) {
            if(line=="VPP_ENABLE_COVERAGE:BOOL=ON"||
               ((line.find("CMAKE_CXX_FLAGS:STRING=")==0||
                 line.find("CMAKE_EXE_LINKER_FLAGS:STRING=")==0)&&
                line.find("-fsanitize=")!=std::string::npos)) {
                instrumented=true;
                break;
            }
        }
        if(!instrumented) {
            const fs::path asmRoot=root/"scripts/ASM";
            const fs::path runtimeSource=root/"src/runtime/native/vpp_aot_runtime.c";
            const fs::path machineSource=asmRoot/"macos/vpp_aot_stage0_machine_aarch64.S";
            const fs::path programObject=temporary.path/"program.o";
            const fs::path localRuntime=temporary.path/"bootstrap-runtime.o";
            const fs::path localMachine=temporary.path/"stage0-machine.o";
            const auto checked=[&](const std::vector<std::string> &args,const char *name) {
                const auto result=process(args,true);
                if(result.status)throw std::runtime_error(std::string("Native AOT: direct ")+name+
                    " failed ("+std::to_string(result.status)+")\n"+result.output);
            };
            checked({"clang++","-c",source.u8string(),"-I"+asmRoot.u8string(),
                     "-I"+(asmRoot/"common").u8string(),"-o",programObject.u8string()},
                    "program assembly");
            profiler.mark("assemble-stage0-program");

            // Content-addressed immutable bootstrap objects can be shared by
            // multiple concurrent test processes. A hard link publishes only
            // fully written objects, so readers never see partial output.
            std::vector<fs::path> dependencies{runtimeSource,machineSource};
            for(const auto &folder:{asmRoot/"macos/aot_arm64",asmRoot/"common"}) {
                if(!fs::is_directory(folder))continue;
                for(const auto &entry:fs::recursive_directory_iterator(folder)) {
                    if(entry.is_regular_file()&&
                       (entry.path().extension()==".inc"||entry.path().extension()==".S"))
                        dependencies.push_back(entry.path());
                }
            }
            std::sort(dependencies.begin(),dependencies.end());
            std::uint64_t digest=14695981039346656037ULL;
            const auto hashBytes=[&](const char *bytes,std::size_t size) {
                for(std::size_t i=0;i<size;++i) {
                    digest^=static_cast<unsigned char>(bytes[i]);
                    digest*=1099511628211ULL;
                }
            };
            const std::string version="darwin-arm64-stage0-clang-release-1";
            hashBytes(version.data(),version.size());
            for(const auto &dependency:dependencies) {
                const auto name=dependency.lexically_relative(root).generic_u8string();
                hashBytes(name.data(),name.size());
                std::ifstream content(dependency,std::ios::binary);
                if(!content)throw std::runtime_error("Native AOT: missing direct link dependency: "+dependency.u8string());
                char bytes[4096];
                while(content.read(bytes,sizeof(bytes))||content.gcount()>0)
                    hashBytes(bytes,static_cast<std::size_t>(content.gcount()));
                if(content.bad())throw std::runtime_error("Native AOT: cannot hash direct link dependency: "+dependency.u8string());
            }
            const auto cacheDirectory=libraries/"native-stage0-direct-cache";
            std::error_code cacheError;
            fs::create_directories(cacheDirectory,cacheError);
            const bool cacheAvailable=!cacheError&&fs::is_directory(cacheDirectory);
            const auto cached=[&](const fs::path &local,const std::string &name,
                                  const std::vector<std::string> &args) -> fs::path {
                const auto target=cacheDirectory/(name+"-"+std::to_string(digest)+".o");
                if(cacheAvailable&&fs::is_regular_file(target)&&fs::file_size(target)>0)
                    return target;
                checked(args,name.c_str());
                if(cacheAvailable) {
                    std::error_code linkError;
                    fs::create_hard_link(local,target,linkError);
                    if((!linkError||fs::is_regular_file(target))&&fs::file_size(target)>0)
                        return target;
                }
                return local;
            };
            const auto runtimeObject=cached(localRuntime,"bootstrap-runtime",
                {"clang","-std=c11","-O3","-DNDEBUG","-c",runtimeSource.u8string(),
                 "-o",localRuntime.u8string()});
            const auto machineObject=cached(localMachine,"stage0-machine",
                {"clang++","-c",machineSource.u8string(),"-I"+asmRoot.u8string(),
                 "-I"+(asmRoot/"common").u8string(),"-o",localMachine.u8string()});
            profiler.mark("prepare-stage0-runtime");
            std::vector<std::string> link={"clang++",programObject.u8string(),
                runtimeObject.u8string(),machineObject.u8string()};
            for(const char *name:{"native-aot","runtime-native","compiler","frontend","bytecode","core"}) {
                const auto archive=libraries/"lib"/(std::string("libvpp-")+name+".a");
                if(!fs::is_regular_file(archive))
                    throw std::runtime_error("Native AOT: missing direct link archive: "+archive.u8string());
                link.push_back(archive.u8string());
            }
#if defined(VPP_AOT_LINK_LEGACY_LIBFFI) && VPP_AOT_LINK_LEGACY_LIBFFI
            link.push_back("-lffi");
#endif
            link.insert(link.end(),{"-framework","Security","-framework","CoreFoundation",
                                    "-o",executable.u8string()});
            checked(link,"Stage 0 executable link");
            profiler.mark("link-stage0-executable");
            if(std::getenv("VPP_AOT_KEEP_ASM"))
                fs::copy_file(source,fs::path(absolute.u8string()+".S"),fs::copy_options::overwrite_existing);
            fs::rename(executable,absolute);
            return;
        }
    }
#endif
    const auto cmakePath = [](const fs::path &path) {
        return path.generic_u8string();
    };
    // A fresh nested CMake configure and bootstrap object build per fixture
    // made integration spend most of its time outside the V++ compiler,
    // especially with Visual Studio's compiler checks and bootstrap build.
    // Reuse the link project within this compiler process; every source still
    // gets a fresh program object and executable. Workers have separate PIDs
    // and therefore separate temporary projects.
    fs::path linkerBuild=temporary.path/"build";
    fs::path linkerSource=source;
    fs::path linkerOutput=executable;
    bool configureLinker=true;
    struct CachedLinker {
        std::unique_ptr<TemporaryDirectory> directory;
        std::string configuration;
    };
    static thread_local CachedLinker cachedLinker;
    const auto environment=[](const char *name) {
        const char *value=std::getenv(name);
        return value?std::string(value):std::string();
    };
    // The selected build cache controls instrumentation and linker flags.
    // A batch can outlive a rebuild of the libraries or runtime object.
    // CMakeCache alone cannot distinguish two different builds that used
    // identical compiler flags. Use cheap file metadata rather than hashing
    // the large static archives for every fixture in the same batch.
    std::ifstream selectedBuildCache(libraries/"CMakeCache.txt",std::ios::binary);
    const std::string selectedBuildConfiguration{
        std::istreambuf_iterator<char>(selectedBuildCache),
        std::istreambuf_iterator<char>()};
    std::ostringstream linkInputs;
    const auto trackLinkInput=[&](const fs::path &path) {
        std::error_code ec;
        const bool regular=fs::is_regular_file(path,ec);
        linkInputs<<path.u8string()<<'\n';
        if(ec||!regular) {
            linkInputs<<"missing\n";
            return;
        }
        const auto length=fs::file_size(path,ec);
        if(ec) {linkInputs<<"unreadable\n";return;}
        const auto modified=fs::last_write_time(path,ec);
        if(ec) {linkInputs<<"unreadable\n";return;}
        const auto modifiedMicros=
            std::chrono::duration_cast<std::chrono::microseconds>(
                modified.time_since_epoch()).count();
        linkInputs<<length<<':'<<modifiedMicros<<'\n';
    };
    // Track every supported library search location: an archive can be
    // replaced or the linker can switch from lib/ to lib/Release mid-batch.
    for(const char *directory:{"lib/Release","lib64/Release","lib","lib64"}) {
        for(const char *module:{"native-aot","runtime-native","compiler",
                                 "frontend","bytecode","core"}) {
            const auto stem="libvpp-"+std::string(module);
            for(const char *suffix:{".a",".so",".dylib"})
                trackLinkInput(libraries/directory/(stem+suffix));
#if defined(_WIN32)
            trackLinkInput(libraries/directory/("vpp-"+std::string(module)+".lib"));
            trackLinkInput(libraries/directory/(stem+".dll.a"));
#endif
        }
    }
    const auto linkerRuntime=environment("VPP_AOT_RUNTIME");
    if(linkerRuntime=="vpp"||linkerRuntime=="vpp-freestanding") {
        const auto chosen=environment("VPP_AOT_VPP_OBJECT");
        trackLinkInput(chosen.empty()?root/"build/native-vpp-runtime/runtime.o":
                       core::utf8Path(chosen));
    }
    const std::string configuration=root.u8string()+"\n"+libraries.u8string()+"\n"+
        selectedBuildConfiguration+"\n"+
        linkInputs.str()+"\n"+
        environment("VPP_AOT_RUNTIME")+"\n"+environment("VPP_AOT_VPP_OBJECT")+"\n"+
        environment("CMAKE_GENERATOR")+"\n"+environment("CC")+"\n"+
        environment("CMAKE_GENERATOR_PLATFORM")+"\n"+
        environment("CMAKE_GENERATOR_TOOLSET")+"\n"+
        environment("CXX")+"\n"+environment("CFLAGS")+"\n"+
        environment("CXXFLAGS")+"\n"+environment("LDFLAGS")+"\n"+
        environment("CMAKE_TOOLCHAIN_FILE")+"\n"+
        environment("SDKROOT")+"\n"+environment("MACOSX_DEPLOYMENT_TARGET")+"\n"+
        environment("VPP_AOT_LINK_LEGACY_LIBFFI");
    const bool reuseLinker=environment("VPP_AOT_LINKER_CACHE")!="0";
    if(reuseLinker) {
        if(!cachedLinker.directory||cachedLinker.configuration!=configuration) {
            cachedLinker.directory=std::make_unique<TemporaryDirectory>(fs::temp_directory_path());
            cachedLinker.configuration=configuration;
        } else {
            configureLinker=false;
        }
        linkerBuild=cachedLinker.directory->path/"build";
        linkerSource=cachedLinker.directory->path/"program.S";
        linkerOutput=cachedLinker.directory->path/executable.filename();
        fs::copy_file(source,linkerSource,fs::copy_options::overwrite_existing);
        if(!configureLinker) {
            // Force reassembly even on filesystems with coarse mtimes.
            // Keep immutable bootstrap objects and the linker graph.
            std::error_code ignored;
            fs::remove(linkerBuild/"program.o",ignored);
            fs::remove(linkerBuild/"program.obj",ignored);
            fs::remove(linkerBuild/"bin/vpp-aot-program",ignored);
            fs::remove(linkerBuild/"bin/vpp-aot-program.exe",ignored);
            fs::remove(linkerBuild/"bin/Release/vpp-aot-program.exe",ignored);
            fs::remove(linkerOutput,ignored);
            profiler.mark("reuse-native-linker");
        }
    }
    std::vector<std::string> configure={"cmake","-S",cmakePath(root/"scripts/quality/native_aot_link"),"-B",cmakePath(linkerBuild)};
#if !defined(_WIN32)
    // Unix CI uses single-configuration generators. Visual Studio ignores
    // CMAKE_BUILD_TYPE and warns about it for each nested native link.
    configure.push_back("-DCMAKE_BUILD_TYPE=Release");
#endif
    configure.insert(configure.end(),{"-DVPP_AOT_CONFIGURATION=Release","-DVPP_AOT_ROOT="+cmakePath(root),
        "-DVPP_AOT_LIB_ROOT="+cmakePath(libraries),"-DVPP_AOT_SOURCE="+cmakePath(linkerSource),
        "-DVPP_AOT_OUTPUT="+cmakePath(linkerOutput)});
#if !defined(_WIN32)
    // Windows links its platform libraries directly; its CMake link project
    // does not consume this POSIX-only flag. Passing it produces an unused-cli
    // warning that contaminates the golden output of every native batch test.
#if defined(VPP_AOT_LINK_LEGACY_LIBFFI) && VPP_AOT_LINK_LEGACY_LIBFFI
    configure.push_back("-DVPP_AOT_LINK_LEGACY_LIBFFI=ON");
#else
    configure.push_back("-DVPP_AOT_LINK_LEGACY_LIBFFI=OFF");
#endif
#endif
    if(configureLinker) {
        auto configured=process(configure);
        if(configured.status) {
            if(reuseLinker)cachedLinker.directory.reset();
            throw std::runtime_error("Native AOT: linker configuration failed\n"+configured.output);
        }
        profiler.mark("configure-native-linker");
    }
    auto linked=process({"cmake","--build",linkerBuild.u8string(),"--config","Release","--parallel","2"});
    if(linked.status||!fs::is_regular_file(linkerOutput))throw std::runtime_error("Native AOT: assembly/link failed\n"+linked.output);
    profiler.mark("assemble-and-link");
    if(linkerOutput!=executable)
        fs::copy_file(linkerOutput,executable,fs::copy_options::overwrite_existing);
    if(std::getenv("VPP_AOT_KEEP_ASM"))fs::copy_file(source,fs::path(absolute.u8string()+".S"),fs::copy_options::overwrite_existing);
    fs::rename(executable,absolute);
}

extern "C" int vpp_aot_main(int argc,char **argv,const ProgramSpec *spec) noexcept {
    try {
        Program program;
        for(std::size_t i=0;i<spec->stringCount;++i)program.strings.emplace_back(spec->strings[i].data,spec->strings[i].size);
        for(std::size_t i=0;i<spec->functionCount;++i) {
            const auto &f=spec->functions[i];program.functions.push_back({f.name,f.required,f.maximum,f.function});
        }
        if(spec->reserved>=1 && spec->i32Functions) {
            program.i32Functions.assign(spec->i32Functions,spec->i32Functions+spec->i32FunctionCount);
        }
        program.initializers.assign(spec->initializers,spec->initializers+spec->initializerCount);program.entry=spec->entry;
        for(std::size_t i=0;i<spec->foreignCount;++i) {
            const auto &f=spec->foreignFunctions[i];bytecode::ForeignFunctionDescriptor descriptor;
            descriptor.id=f.id;descriptor.result=static_cast<bytecode::ForeignAbiType>(f.result);
            descriptor.library=f.library;descriptor.symbol=f.symbol;descriptor.abi=f.abi;descriptor.capability=f.capability;
            for(std::size_t k=0;k<f.parameterCount;++k)descriptor.parameters.push_back(static_cast<bytecode::ForeignAbiType>(f.parameters[k]));
            for(std::size_t k=0;k<f.extentCount;++k)descriptor.bufferExtents.push_back({f.extents[k].parameter,f.extents[k].lengthParameter,f.extents[k].fixed});
            program.foreignFunctions.push_back(std::move(descriptor));
        }
        program.executeArtifact=[spec](Program &executor,const std::vector<Value> &wire)->Value {
            std::unique_ptr<VppTemporaryDirectory> vppTemporary;
            std::unique_ptr<TemporaryDirectory> legacyTemporary;
            if(spec->compiler)
                vppTemporary=std::make_unique<VppTemporaryDirectory>(executor);
            else legacyTemporary=std::make_unique<TemporaryDirectory>(fs::temp_directory_path());
            const auto executable=(spec->compiler?vppTemporary->path:legacyTemporary->path)/
#if defined(_WIN32)
                "artifact.exe";
#else
                "artifact";
#endif
            if(spec->compiler) {
                const auto assembly=executor.invokeName("biên dịch asm chương trình",
                    {make_list_value(wire),canonicalTriple(hostTarget()),make_int_value(0)});
                if(!std::holds_alternative<std::string>(assembly))
                    throw std::runtime_error("Native AOT: expected V++ assembly text");
                VppTemporaryDirectory sessionLinker(executor);
                compileAssemblyWithVpp(executor,std::get<std::string>(assembly),executable,
                                       vppInstallation(executor),sessionLinker.path);
            } else {
                compileExecutable(snapshotFromWire(wire),executable,installation());
            }
            const auto result=process({executable.u8string()});
            if(result.status)throw std::runtime_error("Native AOT: generated artifact failed: "+std::to_string(result.status));
            return result.output;
        };
        program.executeSource=[&program,spec](const std::vector<Value> &args)->Value {
            if(args.size()!=2||!std::holds_alternative<std::string>(args[0])||
                !std::holds_alternative<std::string>(args[1]))
                throw std::runtime_error("Native AOT: embedded script requires source and base directory strings");
            const auto &source=std::get<std::string>(args[0]);
            const auto &base=std::get<std::string>(args[1]);
            if(!core::isValidUtf8(source))
                throw std::runtime_error("Native AOT: embedded script is not valid UTF-8");
            fs::path directory,executable;
            if(spec->compiler) {
                const auto prepared=program.invokeName("biên dịch AOT chuẩn bị nhúng",
                    {base,executableDirectory().u8string()});
                const auto &paths=values(prepared);
                if(paths.size()!=2)throw std::runtime_error("Native AOT: invalid V++ embedded paths");
                directory=core::utf8Path(driverString(paths[0]));
                executable=core::utf8Path(driverString(paths[1]));
            } else {
                // Stage 0/legacy callers have no loaded V++ compiler module.
                directory=fs::absolute(core::utf8Path(base.empty()?".":base));
                if(!fs::is_directory(directory))
                    throw std::runtime_error("Native AOT: embedded base is not a directory: "+directory.u8string());
                const char *configured=std::getenv("VPP_NATIVE_COMPILER");
                if(configured&&*configured)executable=fs::absolute(core::utf8Path(configured));
                else executable=installation()/"bin"/
#if defined(_WIN32)
                    "vpp-native.exe";
#else
                    "vpp-native";
#endif
                if(!fs::is_regular_file(executable))
                    throw std::runtime_error("Native AOT: missing native V++ compiler: "+executable.u8string());
            }
            // The V++ loader receives the original import base explicitly;
            // no write access to that directory is necessary.
            std::unique_ptr<VppTemporaryDirectory> vppTemporary;
            std::unique_ptr<TemporaryDirectory> bootstrapTemporary;
            if(spec->compiler)
                vppTemporary=std::make_unique<VppTemporaryDirectory>(program);
            else bootstrapTemporary=std::make_unique<TemporaryDirectory>(fs::temp_directory_path());
            const auto &temporary=spec->compiler?vppTemporary->path:bootstrapTemporary->path;
            const auto file=temporary/"embedded.vi";
            if(spec->compiler) {
                program.invokeName("biên dịch AOT ghi nguồn nhúng",{file.u8string(),source});
            } else {
                std::ofstream output(file,std::ios::binary|std::ios::trunc);
                if(!output||!(output<<source))
                    throw std::runtime_error("Native AOT: cannot write embedded source");
            }
            // Embedded compilation errors are catchable language exceptions.
            // Capture their diagnostics so a caught error cannot leak to the
            // parent's stderr (or corrupt a golden test's combined output).
            const auto diagnostics=temporary/"embedded.stderr";
            const auto result=process({executable.u8string(),"embed",file.u8string(),"--base",directory.u8string()},false,diagnostics);
            std::string errors;
            if(spec->compiler) {
                const auto value=program.invokeName("biên dịch AOT đọc lỗi nhúng",{diagnostics.u8string()});
                const auto *message=std::get_if<std::string>(&value);
                if(!message)throw std::runtime_error("Native AOT: invalid V++ embedded diagnostics");
                errors=*message;
            } else {
                std::ifstream diagnosticFile(diagnostics,std::ios::binary);
                errors.assign(std::istreambuf_iterator<char>(diagnosticFile),
                              std::istreambuf_iterator<char>());
            }
            if(result.status)
                throw std::runtime_error("Native AOT: embedded script failed: "+std::to_string(result.status)+"\n"+errors);
            std::cerr<<errors;
            return result.output;
        };
        auto result=program.run();
        dumpAotCpuProfile();
        if(spec->compiler)return compilerMain(program,argc,argv);
        if(const auto *status=std::get_if<int>(&result))return *status;
        return 0;
    }catch(const std::exception &error){std::cerr<<error.what()<<'\n';return 1;}
    catch(...){std::cerr<<"Native AOT: unknown failure\n";return 1;}
}
} // namespace vietvm::native::aot
