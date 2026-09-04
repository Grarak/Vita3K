// Vita3K emulator project
// Copyright (C) 2026 Vita3K team
//
// This program is free software; you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation; either version 2 of the License, or
// (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License along
// with this program; if not, write to the Free Software Foundation, Inc.,
// 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.

// The runtime Cg compiler. On a real console this is libshacccg.suprx, the SDK's own compiler
// shipped as a PRX; here the compilation is handed to psp2cgc, the same compiler as a host
// tool, through a script named by the VITA3K_CGC environment variable. Titles that generate
// shaders at run time (emulators, engines with a shader cache) are unrunnable without it.

#include <module/module.h>

#include <kernel/state.h>
#include <mem/functions.h>
#include <util/log.h>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <unistd.h>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace {

struct SceShaccCgSourceFile {
    Ptr<const char> fileName;
    Ptr<const char> text;
    uint32_t size;
};
static_assert(sizeof(SceShaccCgSourceFile) == 0xC);

struct SceShaccCgCallbackList {
    Ptr<void> openFile;
    Ptr<void> releaseFile;
    Ptr<void> locateFile;
    Ptr<void> absolutePath;
    Ptr<void> releaseFileName;
    Ptr<void> fileDate;
};
static_assert(sizeof(SceShaccCgCallbackList) == 0x18);

struct SceShaccCgCompileOptions {
    Ptr<const char> mainSourceFile;
    int32_t targetProfile; // 0: vertex, 1: fragment
    Ptr<const char> entryFunctionName;
    uint32_t searchPathCount;
    Ptr<Ptr<const char>> searchPaths;
    uint32_t macroDefinitionCount;
    Ptr<Ptr<const char>> macroDefinitions;
    uint32_t includeFileCount;
    Ptr<Ptr<const char>> includeFiles;
    uint32_t suppressedWarningsCount;
    Ptr<uint32_t> suppressedWarnings;
    int32_t locale;
    int32_t useFx;
    int32_t noStdlib;
    int32_t optimizationLevel;
    int32_t useFastmath;
    int32_t useFastprecision;
    int32_t useFastint;
    int32_t field_48;
    int32_t warningsAsErrors;
    int32_t performanceWarnings;
    int32_t warningLevel;
    int32_t pedantic;
    int32_t pedanticError;
    int32_t field_60;
    int32_t field_64;
};
static_assert(sizeof(SceShaccCgCompileOptions) == 0x68);

struct SceShaccCgCompileOutput {
    Ptr<const uint8_t> programData;
    uint32_t programSize;
    int32_t diagnosticCount;
    Ptr<void> diagnostics;
};
static_assert(sizeof(SceShaccCgCompileOutput) == 0x10);

std::string cgc_script() {
    if (const char *from_env = std::getenv("VITA3K_CGC"))
        return from_env;
    const char *home = std::getenv("HOME");
    return std::string(home ? home : ".") + "/3ds-vita/cgc_one.sh";
}

std::string shell_quote(const std::string &s) {
    std::string out = "'";
    for (const char c : s) {
        if (c == '\'')
            out += "'\\''";
        else
            out += c;
    }
    return out + "'";
}

/// The source text of the program being compiled, taken from the openFile callback the caller
/// installed: the SDK compiler reads its main source through that callback rather than from
/// the file system, and generated shaders never exist as files at all.
std::string read_source(EmuEnvState &emuenv, SceUID thread_id, const SceShaccCgCompileOptions &options,
    Ptr<const SceShaccCgCompileOptions> options_ptr, Ptr<const SceShaccCgCallbackList> callbacks) {
    const SceShaccCgCallbackList *list = callbacks.get(emuenv.mem);
    if (!list || !list->openFile) {
        LOG_ERROR("sceShaccCgCompileProgram without an openFile callback is not supported");
        return {};
    }
    const ThreadStatePtr thread = emuenv.kernel.get_thread(thread_id);
    if (!thread)
        return {};
    LOG_INFO("sceShaccCgCompileProgram: calling openFile at {}", log_hex(list->openFile.address()));

    // The callback's last argument is where it may leave an error string; it is only read on
    // failure, but it must point somewhere the guest can write.
    const Address error_string = alloc(emuenv.mem, 4, "shacccg_error");
    *Ptr<uint32_t>(error_string).get(emuenv.mem) = 0;
    const uint32_t file = thread->run_callback(list->openFile.address(),
        { options.mainSourceFile.address(), 0, options_ptr.address(), error_string });
    free(emuenv.mem, error_string);
    if (!file) {
        LOG_ERROR("The openFile callback refused '{}'", options.mainSourceFile.get(emuenv.mem));
        return {};
    }

    LOG_INFO("sceShaccCgCompileProgram: openFile returned {}", log_hex(file));
    const SceShaccCgSourceFile *source = Ptr<SceShaccCgSourceFile>(file).get(emuenv.mem);
    LOG_INFO("sceShaccCgCompileProgram: source file name {}, text {}, size {}",
        log_hex(source->fileName.address()), log_hex(source->text.address()), source->size);
    const char *text = source->text.get(emuenv.mem);
    if (!text)
        return {};
    // The size the caller reports is not trusted: reading past the end of the source would
    // fault in the emulator rather than in the guest, and the source is a C string anyway.
    const size_t length = source->size ? source->size : strnlen(text, 16 * 1024 * 1024);
    std::string out(text, length);
    LOG_INFO("sceShaccCgCompileProgram: read {} bytes of source", out.size());
    return out;
}

} // namespace

EXPORT(Ptr<SceShaccCgCompileOutput>, sceShaccCgCompileProgram, Ptr<const SceShaccCgCompileOptions> options_ptr,
    Ptr<const SceShaccCgCallbackList> callbacks, int unk) {
    LOG_INFO("sceShaccCgCompileProgram: options {}, callbacks {}", log_hex(options_ptr.address()), log_hex(callbacks.address()));
    const SceShaccCgCompileOptions *options = options_ptr.get(emuenv.mem);
    if (!options)
        return Ptr<SceShaccCgCompileOutput>(0);

    const std::string source = read_source(emuenv, thread_id, *options, options_ptr, callbacks);
    if (source.empty())
        return Ptr<SceShaccCgCompileOutput>(0);

    const std::filesystem::path dir = std::filesystem::temp_directory_path();
    static uint32_t counter = 0;
    const std::string stem = fmt::format("vita3k_cg_{}_{}", getpid(), counter++);
    const std::filesystem::path cg = dir / (stem + ".cg");
    const std::filesystem::path gxp = dir / (stem + ".gxp");
    const std::filesystem::path err = dir / (stem + ".log");
    {
        std::ofstream out(cg, std::ios::binary);
        out.write(source.data(), source.size());
    }

    std::string command = shell_quote(cgc_script()) + " " + shell_quote(cg.string()) + " "
        + shell_quote(gxp.string()) + (options->targetProfile == 1 ? " sce_fp_psp2" : " sce_vp_psp2");
    command += fmt::format(" -O{}", std::clamp(options->optimizationLevel, 0, 4));
    if (!options->useFastmath)
        command += " -nofastmath";
    if (options->useFastprecision)
        command += " -fastprecision";
    if (!options->useFastint)
        command += " -nofastint";
    if (const char *entry = options->entryFunctionName.get(emuenv.mem); entry && *entry)
        command += " -entry " + shell_quote(entry);
    for (uint32_t i = 0; i < options->macroDefinitionCount; ++i) {
        const char *macro = options->macroDefinitions.get(emuenv.mem)[i].get(emuenv.mem);
        if (macro)
            command += " -D" + shell_quote(macro);
    }
    command += " 2> " + shell_quote(err.string());

    LOG_INFO("sceShaccCgCompileProgram: running {}", command);
    const int status = std::system(command.c_str());
    LOG_INFO("sceShaccCgCompileProgram: psp2cgc exited {}", status);
    std::error_code ignored;
    std::filesystem::remove(cg, ignored);

    std::vector<uint8_t> program;
    if (status == 0) {
        std::ifstream in(gxp, std::ios::binary);
        program.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
    }
    std::filesystem::remove(gxp, ignored);
    if (program.empty()) {
        std::ifstream in(err);
        const std::string diagnostics((std::istreambuf_iterator<char>(in)),
            std::istreambuf_iterator<char>());
        LOG_ERROR("psp2cgc refused the {} program:\n{}",
            options->targetProfile == 1 ? "fragment" : "vertex", diagnostics);
    }
    std::filesystem::remove(err, ignored);
    if (program.empty())
        return Ptr<SceShaccCgCompileOutput>(0);

    const Address program_address = alloc(emuenv.mem, static_cast<uint32_t>(program.size()), "shacccg_program");
    std::memcpy(Ptr<uint8_t>(program_address).get(emuenv.mem), program.data(), program.size());
    const Address output_address = alloc(emuenv.mem, sizeof(SceShaccCgCompileOutput), "shacccg_output");
    SceShaccCgCompileOutput *output = Ptr<SceShaccCgCompileOutput>(output_address).get(emuenv.mem);
    output->programData = Ptr<const uint8_t>(program_address);
    output->programSize = static_cast<uint32_t>(program.size());
    output->diagnosticCount = 0;
    output->diagnostics = Ptr<void>(0);
    LOG_INFO("Compiled a {} program: {} bytes of GXP from {} bytes of Cg",
        options->targetProfile == 1 ? "fragment" : "vertex", program.size(), source.size());
    return Ptr<SceShaccCgCompileOutput>(output_address);
}

EXPORT(void, sceShaccCgDestroyCompileOutput, Ptr<SceShaccCgCompileOutput> output_ptr) {
    SceShaccCgCompileOutput *output = output_ptr.get(emuenv.mem);
    if (!output)
        return;
    if (output->programData)
        free(emuenv.mem, output->programData.address());
    free(emuenv.mem, output_ptr.address());
}

EXPORT(int, sceShaccCgInitializeCompileOptions, SceShaccCgCompileOptions *options) {
    LOG_INFO("sceShaccCgInitializeCompileOptions");
    if (!options)
        return -1;
    std::memset(options, 0, sizeof(*options));
    options->optimizationLevel = 3;
    options->useFastmath = 1;
    options->useFastint = 1;
    options->warningLevel = 1;
    return 0;
}

EXPORT(void, sceShaccCgInitializeCallbackList, SceShaccCgCallbackList *callbacks, int defaults) {
    // Both defaults (system files, trivial) resolve includes through the file system, which no
    // caller of this implementation uses: the main source arrives through the caller's own
    // openFile. Leaving the list empty makes a caller that does rely on them fail loudly.
    if (callbacks)
        std::memset(callbacks, 0, sizeof(*callbacks));
}

EXPORT(int, sceShaccCgSetDefaultAllocator, Ptr<void> malloc_cb, Ptr<void> free_cb) {
    // The output is allocated out of the emulator's guest heap and freed by
    // sceShaccCgDestroyCompileOutput, so the caller's allocator is never needed.
    return 0;
}

EXPORT(void, sceShaccCgReleaseCompiler) {}

EXPORT(int, sceShaccCgGetArrayParameter) {
    return UNIMPLEMENTED();
}

EXPORT(int, sceShaccCgGetArraySize) {
    return UNIMPLEMENTED();
}

EXPORT(int, sceShaccCgGetFirstParameter) {
    return UNIMPLEMENTED();
}

EXPORT(int, sceShaccCgGetFirstStructParameter) {
    return UNIMPLEMENTED();
}

EXPORT(int, sceShaccCgGetFirstUniformBlockParameter) {
    return UNIMPLEMENTED();
}

EXPORT(int, sceShaccCgGetNextParameter) {
    return UNIMPLEMENTED();
}

EXPORT(int, sceShaccCgGetParameterBaseType) {
    return UNIMPLEMENTED();
}

EXPORT(int, sceShaccCgGetParameterBufferIndex) {
    return UNIMPLEMENTED();
}

EXPORT(int, sceShaccCgGetParameterByName) {
    return UNIMPLEMENTED();
}

EXPORT(int, sceShaccCgGetParameterClass) {
    return UNIMPLEMENTED();
}

EXPORT(int, sceShaccCgGetParameterColumns) {
    return UNIMPLEMENTED();
}

EXPORT(int, sceShaccCgGetParameterDirection) {
    return UNIMPLEMENTED();
}

EXPORT(int, sceShaccCgGetParameterMemoryLayout) {
    return UNIMPLEMENTED();
}

EXPORT(int, sceShaccCgGetParameterName) {
    return UNIMPLEMENTED();
}

EXPORT(int, sceShaccCgGetParameterResourceIndex) {
    return UNIMPLEMENTED();
}

EXPORT(int, sceShaccCgGetParameterRows) {
    return UNIMPLEMENTED();
}

EXPORT(int, sceShaccCgGetParameterSemantic) {
    return UNIMPLEMENTED();
}

EXPORT(int, sceShaccCgGetParameterUserType) {
    return UNIMPLEMENTED();
}

EXPORT(int, sceShaccCgGetParameterVariability) {
    return UNIMPLEMENTED();
}

EXPORT(int, sceShaccCgGetParameterVectorWidth) {
    return UNIMPLEMENTED();
}

EXPORT(int, sceShaccCgGetRowParameter) {
    return UNIMPLEMENTED();
}

EXPORT(int, sceShaccCgGetSamplerQueryFormatPrecision) {
    return UNIMPLEMENTED();
}

EXPORT(int, sceShaccCgGetSamplerQueryFormatPrecisionCount) {
    return UNIMPLEMENTED();
}

EXPORT(int, sceShaccCgGetSamplerQueryFormatWidth) {
    return UNIMPLEMENTED();
}

EXPORT(int, sceShaccCgIsParameterReferenced) {
    return UNIMPLEMENTED();
}

EXPORT(int, sceShaccCgIsParameterRegFormat) {
    return UNIMPLEMENTED();
}
