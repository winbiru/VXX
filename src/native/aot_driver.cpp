#include "vpp/native/aot.h"

#include <atomic>
#include <cerrno>
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
#endif
#include "vpp/core/project_layout.h"
#include "vpp/runtime/error.h"

namespace vietvm::native::aot {
namespace {
namespace fs=std::filesystem;
struct ProcessResult { int status=1; std::string output; };
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
fs::path installation() {
    for(const char *name:{"VPP_AOT_ROOT","VPP_HOME"})if(const char *root=std::getenv(name);root&&*root)return fs::absolute(core::utf8Path(root));
    throw std::runtime_error("Native AOT: set VPP_HOME to the installed toolchain");
}
std::string read(const fs::path &path) {
    std::ifstream input(path,std::ios::binary);if(!input)throw std::runtime_error("Native AOT: cannot read "+path.u8string());
    return {std::istreambuf_iterator<char>(input),std::istreambuf_iterator<char>()};
}
const std::vector<Value> &values(const Value &value) {
    const auto *list=std::get_if<runtime::ListHandle>(&value);
    if(!list||!*list)throw std::runtime_error("Native AOT: expected wire arrays");return (*list)->elements;
}
int compilerMain(Program &program,int argc,char **argv) {
    if(argc<3)throw std::runtime_error("Usage: vpp-native build|run <source.vi> [-o output] [--bytecode] [--compiler]");
    const std::string command=argv[1];const auto source=fs::weakly_canonical(fs::absolute(core::utf8Path(argv[2])));
    fs::path output;bool bytecode=false,compiler=false;
    for(int i=3;i<argc;++i) {
        const std::string argument=argv[i];
        if(argument=="-o"&&i+1<argc)output=fs::absolute(core::utf8Path(argv[++i]));
        else if(argument=="--bytecode")bytecode=true;
        else if(argument=="--compiler")compiler=true;
        else throw std::runtime_error("Native AOT: unknown option "+argument);
    }
    if(command!="build"&&command!="run")throw std::runtime_error("Native AOT: unknown command");
    if(!output.empty()&&(fs::weakly_canonical(output)==source||(fs::exists(output)&&fs::equivalent(output,source))))
        throw std::runtime_error("Native AOT: output must not overwrite source");
    const auto root=installation();
    if(bytecode) {
        if(output.empty())throw std::runtime_error("Native AOT: bytecode output required");
        const auto result=program.invokeName("aot_bytecode_cli",{source.u8string(),root.u8string(),output.u8string()});
        program.output(sv_to_string(result));return 0;
    }
    const auto artifact=program.invokeName("aot_compile_source",{source.u8string(),root.u8string()});
    const auto wire=program.invokeName("aot_artifact_wire",{artifact});
    const auto snapshot=snapshotFromWire(values(wire));
    if(command=="build") {
        if(output.empty())throw std::runtime_error("Native AOT: executable output required");
        compileExecutable(snapshot,output,root,compiler);return 0;
    }
    TemporaryDirectory temporary(fs::temp_directory_path());const auto executable=temporary.path/"program";
    compileExecutable(snapshot,executable,root);const auto result=process({executable.u8string()});
    program.output(result.output);return result.status;
}
} // namespace

void compileExecutable(const Snapshot &snapshot,const fs::path &output,const fs::path &root,bool compiler) {
    const auto absolute=fs::absolute(output);if(!fs::is_directory(absolute.parent_path()))throw std::runtime_error("Native AOT: output directory missing");
    TemporaryDirectory temporary(absolute.parent_path());const auto source=temporary.path/"program.S";
    {std::ofstream assembly(source,std::ios::binary);assembly<<emitAssembly(snapshot,compiler);if(!assembly)throw std::runtime_error("Native AOT: assembly write failed");}
    const auto executable=temporary.path/"program";
    auto configured=process({"cmake","-S",(root/"scripts/quality/native_aot_link").u8string(),"-B",(temporary.path/"build").u8string(),
        "-DCMAKE_BUILD_TYPE=Release","-DVPP_AOT_ROOT="+root.u8string(),"-DVPP_AOT_SOURCE="+source.u8string(),"-DVPP_AOT_OUTPUT="+executable.u8string()});
    if(configured.status)throw std::runtime_error("Native AOT: linker configuration failed\n"+configured.output);
    auto linked=process({"cmake","--build",(temporary.path/"build").u8string(),"--config","Release","--parallel","2"});
    if(linked.status||!fs::is_regular_file(executable))throw std::runtime_error("Native AOT: assembly/link failed\n"+linked.output);
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
        program.executeArtifact=[](const std::vector<Value> &wire)->Value {
            TemporaryDirectory temporary(fs::temp_directory_path());const auto executable=temporary.path/"artifact";
            compileExecutable(snapshotFromWire(wire),executable,installation());const auto result=process({executable.u8string()});
            if(result.status)throw std::runtime_error("Native AOT: generated artifact failed: "+std::to_string(result.status));
            return result.output;
        };
        program.run();
        if(spec->compiler)return compilerMain(program,argc,argv);
        return 0;
    }catch(const std::exception &error){std::cerr<<error.what()<<'\n';return 1;}
    catch(...){std::cerr<<"Native AOT: unknown failure\n";return 1;}
}
} // namespace vietvm::native::aot
