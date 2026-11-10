#include "vpp/native/aot.h"

#include <algorithm>
#include <atomic>
#include <cerrno>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <iterator>
#include <memory>
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
fs::path executableDirectory();
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
int compilerCommand(Program &program,std::vector<Value> arguments,bool captureErrors=false) {
    PhaseTimer profiler;
    const auto plan=program.invokeName("biên dịch phân tích dòng lệnh máy",
                                      {make_list_value(std::move(arguments))});
    auto fields=values(plan);
    if(fields.size()!=8)throw std::runtime_error("Native AOT: invalid CLI plan");
    const auto field=[&](std::size_t i)->const std::string & {
        const auto *value=std::get_if<std::string>(&fields[i]);
        if(!value)throw std::runtime_error("Native AOT: invalid CLI plan string");
        return *value;
    };
    fs::path output;
    // Filesystem canonicalization/identity remain a host ABI operation. All
    // source commands and assembly generation are handled by V++ below.
    if(field(0)!="message") {
        const auto source=fs::weakly_canonical(fs::absolute(core::utf8Path(field(1))));
        output=field(2).empty()?fs::path{}:fs::absolute(core::utf8Path(field(2)));
        const auto base=field(5).empty()?fs::path{}:
            fs::weakly_canonical(fs::absolute(core::utf8Path(field(5))));
        if(!output.empty()&&(fs::weakly_canonical(output)==source||
                            (fs::exists(output)&&fs::equivalent(output,source))))
            throw std::runtime_error("Native AOT: output must not overwrite source");
        fields[1]=source.u8string();fields[2]=output.u8string();fields[5]=base.u8string();
    }
    const auto root=installation(false);
    // VPP_HOME is the library/package installation root. VPP_AOT_ROOT is
    // the assembler/linker toolchain root. An external package installation
    // may intentionally have no scripts/quality/native_aot_link directory.
    fs::path libraryRoot = root;
    if (const char *home=std::getenv("VPP_HOME"); home && *home)
        libraryRoot=fs::absolute(core::utf8Path(home));
    const auto prepared=program.invokeName("biên dịch chuẩn bị dòng lệnh máy",
        {make_list_value(std::move(fields)),libraryRoot.u8string(),canonicalTriple(hostTarget()),
         program.compilerSessionCache});
    profiler.mark("vpp-cli-and-assembly");
    const auto &request=values(prepared);
    if(request.size()!=2||!std::holds_alternative<std::string>(request[0])||
       !std::holds_alternative<std::string>(request[1]))
        throw std::runtime_error("Native AOT: invalid host request");
    const auto &operation=std::get<std::string>(request[0]);
    const auto &text=std::get<std::string>(request[1]);
    if(operation=="complete") {program.output(text);return 0;}
    if(operation=="diagnostic-error") {program.output(text);return 1;}
    if(operation=="assemble") {
        compileAssembly(text,output,root);
        return 0;
    }
    if(operation!="execute")throw std::runtime_error("Native AOT: unknown host operation");
    TemporaryDirectory temporary(fs::temp_directory_path());
    const auto executable=temporary.path/
#if defined(_WIN32)
        "program.exe";
#else
        "program";
#endif
    compileAssembly(text,executable,root);
    const auto result=process({executable.u8string()},captureErrors);
    program.output(result.output);return result.status;
}

int compilerBatchTests(Program &program,int argc,char **argv) {
    // The public CLI consumes --compiler=vpp before forwarding batch requests.
    // Accept the same explicit option in direct native invocations so it can
    // never be mistaken for the output directory (and create a stray folder).
    int outputArgument=2;
    if(argc>2&&std::string(argv[2])=="--compiler=vpp")++outputArgument;
    else if(argc>2&&std::string(argv[2]).rfind("--compiler=",0)==0)
        throw std::runtime_error("Native AOT: batch supports only --compiler=vpp");
    if(argc<outputArgument+2)
        throw std::runtime_error("Native AOT: batch requires output directory and sources");
    if(std::string(argv[outputArgument]).rfind("--",0)==0)
        throw std::runtime_error("Native AOT: unknown batch option before output directory");
    const auto originalCwd=fs::current_path();
    const auto outputDir=fs::absolute(core::utf8Path(argv[outputArgument]));
    fs::create_directories(outputDir);
    std::vector<fs::path> sources;
    for(int i=outputArgument+1;i<argc;++i)
        sources.push_back(fs::weakly_canonical(fs::absolute(core::utf8Path(argv[i]))));
    const char *disabled=std::getenv("VPP_NATIVE_NO_CACHE");
    if(!disabled||std::string(disabled)!="1") {
        runtime::MapValue memo;
        memo.entries={{"thân",make_map_value({})},{"byte",0},{"trúng",0},{"trượt",0}};
        program.compilerSessionCache=make_map_value(std::move(memo));
    }
    const char *setting=std::getenv("VPP_NATIVE_GOLDEN_FAIL_FAST");
    const bool failFast=setting&&std::string(setting)=="1";
    for(std::size_t i=0;i<sources.size();++i) {
        const auto &source=sources[i];
        const auto started=std::chrono::steady_clock::now();
        std::cerr<<"[test "<<i+1<<'/'<<sources.size()<<"] START vpp "<<source.u8string()<<std::endl;
        std::ostringstream captured;
        const auto savedOutput=program.output;
        program.output=[&captured](const std::string &text){captured<<text;};
        auto *savedErrors=std::cerr.rdbuf(captured.rdbuf());
        int status=1;
        try {
            // Match `vpp-cli run --compiler=vpp`: relative fixture I/O is
            // based on the source directory. Test programs run in children,
            // so their globals, environment and runtime heaps stay isolated.
            fs::current_path(source.parent_path());
            status=compilerCommand(program,{std::string("run"),source.u8string()},true);
        } catch(const std::exception &error) {
            captured<<error.what()<<'\n';
        } catch(...) {
            captured<<"Native AOT: unknown failure\n";
        }
        program.output=savedOutput;
        std::cerr.rdbuf(savedErrors);
        fs::current_path(originalCwd);
        const auto base=source.stem().u8string();
        std::ofstream output(outputDir/core::utf8Path(base+".output"),std::ios::binary);
        std::ofstream statusFile(outputDir/core::utf8Path(base+".status"));
        std::ofstream timingFile(outputDir/core::utf8Path(base+".milliseconds"));
        const auto elapsed=std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now()-started).count();
        output<<captured.str();statusFile<<status<<'\n';timingFile<<elapsed<<'\n';
        output.close();statusFile.close();timingFile.close();
        if(!output||!statusFile||!timingFile)
            throw std::runtime_error("Native AOT: cannot write batch evidence");
        std::cerr<<"[test "<<i+1<<'/'<<sources.size()<<"] DONE vpp "<<source.u8string()
                 <<" exit="<<status<<" elapsed_ms="<<elapsed<<std::endl;
        if(failFast) {
            const auto expectedPath=source.parent_path()/"expected"/core::utf8Path(base+".expected");
            std::ifstream expected(expectedPath,std::ios::binary);
            const std::string text{std::istreambuf_iterator<char>(expected),{}};
            if(status!=0||!expected||text!=captured.str()) {
                std::cerr<<"GOLDEN FAIL "<<source.u8string()<<" (exit="<<status
                         <<", expected="<<expectedPath.u8string()<<")\n";
                return 1;
            }
        }
        // Retain only the explicitly rooted memo and compiler globals.
        program.collect();
    }
    return 0;
}

int compilerMain(Program &program,int argc,char **argv) {
    if(argc>1&&std::string(argv[1])=="--batch-run-tests")
        return compilerBatchTests(program,argc,argv);
    std::vector<Value> arguments;
    for(int i=1;i<argc;++i)arguments.emplace_back(argv[i]);
    return compilerCommand(program,std::move(arguments));
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
    // Installed toolchains keep libraries under <root>/lib. A developer's
    // Stage 0 checkout can instead use the existing CMake build libraries.
    fs::path libraries=root;
    if(const char *configured=std::getenv("VPP_AOT_LIB_ROOT");configured&&*configured) {
        libraries=fs::absolute(core::utf8Path(configured));
    } else if(!fs::exists(root/"lib") && !fs::exists(root/"lib64")) {
        // CI builds use arbitrary names (build-native-cross, build-coverage,
        // cmake-build-debug). Link against the libraries of the running
        // compiler so an instrumented build never picks a release library.
        const auto exeDir=executableDirectory();
        // Unix: <build>/bin/vpp-cli. Visual Studio:
        // <build>/bin/Release/vpp-cli.exe, with libraries in <build>/lib/Release.
        const auto buildDir=exeDir.empty()?fs::path{}:
            (exeDir.filename()=="Release"||exeDir.filename()=="Debug"||
             exeDir.filename()=="RelWithDebInfo"||exeDir.filename()=="MinSizeRel"
                ? exeDir.parent_path().parent_path() : exeDir.parent_path());
        if(!buildDir.empty() && (fs::exists(buildDir/"lib"/"Release") ||
                                 fs::exists(buildDir/"lib"))) {
            libraries=buildDir;
        } else {
            for(const char *candidate:{"build","cmake-build-debug","cmake-build-release","cmake-build-sanitized"}) {
                if(fs::exists(root/candidate/"lib")) {libraries=root/candidate;break;}
            }
        }
    }
#if defined(__APPLE__) && defined(__aarch64__)
    // The default Stage 0 AOT link otherwise configures a fresh CMake project
    // for every golden fixture. Opt in during integration after parity checks;
    // this uses the same bootstrap ABI and archives, without invoking the VM.
    const char *directCpp=std::getenv("VPP_AOT_DIRECT_LINK_CPP");
    const char *selectedRuntime=std::getenv("VPP_AOT_RUNTIME");
    if(directCpp&&std::string(directCpp)=="1"&&
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
            TemporaryDirectory temporary(fs::temp_directory_path());
            const auto executable=temporary.path/
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
                compileAssembly(std::get<std::string>(assembly),executable,installation());
            } else {
                compileExecutable(snapshotFromWire(wire),executable,installation());
            }
            const auto result=process({executable.u8string()});
            if(result.status)throw std::runtime_error("Native AOT: generated artifact failed: "+std::to_string(result.status));
            return result.output;
        };
        program.executeSource=[](const std::vector<Value> &args)->Value {
            if(args.size()!=2||!std::holds_alternative<std::string>(args[0])||
                !std::holds_alternative<std::string>(args[1]))
                throw std::runtime_error("Native AOT: embedded script requires source and base directory strings");
            const auto &source=std::get<std::string>(args[0]);
            const auto &base=std::get<std::string>(args[1]);
            if(!core::isValidUtf8(source))
                throw std::runtime_error("Native AOT: embedded script is not valid UTF-8");
            const auto directory=fs::absolute(core::utf8Path(base.empty()?".":base));
            if(!fs::is_directory(directory))
                throw std::runtime_error("Native AOT: embedded base is not a directory: "+directory.u8string());
            const char *configured=std::getenv("VPP_NATIVE_COMPILER");
            fs::path executable;
            if(configured&&*configured)executable=fs::absolute(core::utf8Path(configured));
            else executable=installation()/"bin"/
#if defined(_WIN32)
                "vpp-native.exe";
#else
                "vpp-native";
#endif
            if(!fs::is_regular_file(executable))
                throw std::runtime_error("Native AOT: missing native V++ compiler: "+executable.u8string());
            // The V++ loader receives the original import base explicitly;
            // no write access to that directory is necessary.
            TemporaryDirectory temporary(fs::temp_directory_path());
            const auto file=temporary.path/"embedded.vi";
            {std::ofstream output(file,std::ios::binary|std::ios::trunc);
             if(!output||!(output<<source))throw std::runtime_error("Native AOT: cannot write embedded source");}
            // Embedded compilation errors are catchable language exceptions.
            // Capture their diagnostics so a caught error cannot leak to the
            // parent's stderr (or corrupt a golden test's combined output).
            const auto diagnostics=temporary.path/"embedded.stderr";
            const auto result=process({executable.u8string(),"embed",file.u8string(),"--base",directory.u8string()},false,diagnostics);
            std::ifstream diagnosticFile(diagnostics,std::ios::binary);
            const std::string errors{std::istreambuf_iterator<char>(diagnosticFile),std::istreambuf_iterator<char>()};
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
