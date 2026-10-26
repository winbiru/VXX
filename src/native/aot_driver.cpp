#include "vpp/native/aot.h"

#include <atomic>
#include <cerrno>
#include <chrono>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>
#if defined(_WIN32)
#include <windows.h>
#else
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
ProcessResult process(const std::vector<std::string> &arguments) {
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
    STARTUPINFOW startup{};startup.cb=sizeof(startup);startup.dwFlags=STARTF_USESTDHANDLES;
    startup.hStdInput=GetStdHandle(STD_INPUT_HANDLE);startup.hStdOutput=output;startup.hStdError=GetStdHandle(STD_ERROR_HANDLE);
    PROCESS_INFORMATION child{};
    if(!CreateProcessW(nullptr,command.data(),nullptr,nullptr,TRUE,0,nullptr,nullptr,&startup,&child)) {
        CloseHandle(input);CloseHandle(output);throw std::runtime_error("Native AOT: CreateProcess failed");
    }
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
        close(descriptors[0]);if(dup2(descriptors[1],STDOUT_FILENO)<0)_exit(127);close(descriptors[1]);
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
fs::path installation(bool required=true) {
    for(const char *name:{"VPP_AOT_ROOT","VPP_HOME"})if(const char *root=std::getenv(name);root&&*root)return fs::absolute(core::utf8Path(root));
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
int compilerMain(Program &program,int argc,char **argv) {
    PhaseTimer profiler;
    std::vector<Value> arguments;
    for(int i=1;i<argc;++i)arguments.emplace_back(argv[i]);
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
    const auto prepared=program.invokeName("biên dịch chuẩn bị dòng lệnh máy",
        {make_list_value(std::move(fields)),root.u8string(),canonicalTriple(hostTarget())});
    profiler.mark("vpp-cli-and-assembly");
    const auto &request=values(prepared);
    if(request.size()!=2||!std::holds_alternative<std::string>(request[0])||
       !std::holds_alternative<std::string>(request[1]))
        throw std::runtime_error("Native AOT: invalid host request");
    const auto &operation=std::get<std::string>(request[0]);
    const auto &text=std::get<std::string>(request[1]);
    if(operation=="complete") {program.output(text);return 0;}
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
    const auto result=process({executable.u8string()});
    program.output(result.output);return result.status;
}
} // namespace

ProcessResult runProcess(const std::vector<std::string> &arguments) {
    if (arguments.empty()) throw std::invalid_argument("Native AOT: empty process arguments");
    return process(arguments);
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
    std::vector<std::string> configure={"cmake","-S",(root/"scripts/quality/native_aot_link").u8string(),"-B",(temporary.path/"build").u8string()};
#if !defined(_WIN32)
    // Unix CI uses single-configuration generators. Visual Studio ignores
    // CMAKE_BUILD_TYPE and warns about it for each nested native link.
    configure.push_back("-DCMAKE_BUILD_TYPE=Release");
#endif
    configure.insert(configure.end(),{"-DVPP_AOT_CONFIGURATION=Release","-DVPP_AOT_ROOT="+root.u8string(),
        "-DVPP_AOT_LIB_ROOT="+libraries.u8string(),"-DVPP_AOT_SOURCE="+source.u8string(),
        "-DVPP_AOT_OUTPUT="+executable.u8string()});
#if defined(VPP_AOT_LINK_LEGACY_LIBFFI) && VPP_AOT_LINK_LEGACY_LIBFFI
    configure.push_back("-DVPP_AOT_LINK_LEGACY_LIBFFI=ON");
#else
    configure.push_back("-DVPP_AOT_LINK_LEGACY_LIBFFI=OFF");
#endif
    auto configured=process(configure);
    if(configured.status)throw std::runtime_error("Native AOT: linker configuration failed\n"+configured.output);
    profiler.mark("configure-native-linker");
    auto linked=process({"cmake","--build",(temporary.path/"build").u8string(),"--config","Release","--parallel","2"});
    if(linked.status||!fs::is_regular_file(executable))throw std::runtime_error("Native AOT: assembly/link failed\n"+linked.output);
    profiler.mark("assemble-and-link");
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
        program.initializers.assign(spec->initializers,spec->initializers+spec->initializerCount);program.entry=spec->entry;
        for(std::size_t i=0;i<spec->foreignCount;++i) {
            const auto &f=spec->foreignFunctions[i];bytecode::ForeignFunctionDescriptor descriptor;
            descriptor.id=f.id;descriptor.result=static_cast<bytecode::ForeignAbiType>(f.result);
            descriptor.library=f.library;descriptor.symbol=f.symbol;descriptor.abi=f.abi;descriptor.capability=f.capability;
            for(std::size_t k=0;k<f.parameterCount;++k)descriptor.parameters.push_back(static_cast<bytecode::ForeignAbiType>(f.parameters[k]));
            for(std::size_t k=0;k<f.extentCount;++k)descriptor.bufferExtents.push_back({f.extents[k].parameter,f.extents[k].lengthParameter,f.extents[k].fixed});
            program.foreignFunctions.push_back(std::move(descriptor));
        }
        program.executeArtifact=[&program,spec](const std::vector<Value> &wire)->Value {
            TemporaryDirectory temporary(fs::temp_directory_path());
            const auto executable=temporary.path/
#if defined(_WIN32)
                "artifact.exe";
#else
                "artifact";
#endif
            if(spec->compiler) {
                const auto assembly=program.invokeName("biên dịch asm chương trình",
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
            const auto result=process({executable.u8string(),"embed",file.u8string(),"--base",directory.u8string()});
            if(result.status)
                throw std::runtime_error("Native AOT: embedded script failed: "+std::to_string(result.status));
            return result.output;
        };
        program.run();
        if(spec->compiler)return compilerMain(program,argc,argv);
        return 0;
    }catch(const std::exception &error){std::cerr<<error.what()<<'\n';return 1;}
    catch(...){std::cerr<<"Native AOT: unknown failure\n";return 1;}
}
} // namespace vietvm::native::aot
