// Copyright 2015 The Shaderc Authors. All rights reserved.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#ifndef SHADERC_SHADERC_HPP_
#define SHADERC_SHADERC_HPP_

#include <memory>
#include <string>
#include <vector>

#include "shaderc.h"

namespace shaderc {
// A CompilationResult contains the compiler output, compilation status,
// and messages.
//
// The compiler output is stored as an array of elements and accessed
// via random access iterators provided by cbegin() and cend().  The iterators
// are contiguous in the sense of "Contiguous Iterators: A Refinement of
// Random Access Iterators", Nevin Liber, C++ Library Evolution Working
// Group Working Paper N3884.
// http://www.open-std.org/jtc1/sc22/wg21/docs/papers/2014/n3884.pdf
//
// Methods begin() and end() are also provided to enable range-based for.
// They are synonyms to cbegin() and cend(), respectively.
template <typename OutputElementType>
class CompilationResult {
 public:
  typedef OutputElementType element_type;
  // The type used to describe the begin and end iterators on the
  // compiler output.
  typedef const OutputElementType* const_iterator;

  // Upon creation, the CompilationResult takes ownership of the
  // shaderc_compilation_result instance. During destruction of the
  // CompilationResult, the shaderc_compilation_result will be released.
  explicit CompilationResult(shaderc_compilation_result_t compilation_result)
      : compilation_result_(compilation_result) {}
  ~CompilationResult() { shaderc_result_release(compilation_result_); }

  CompilationResult(CompilationResult&& other) {
    compilation_result_ = other.compilation_result_;
    other.compilation_result_ = nullptr;
  }

  // Returns any error message found during compilation.
  std::string GetErrorMessage() const {
    if (!compilation_result_) {
      return "";
    }
    return shaderc_result_get_error_message(compilation_result_);
  }

  // Returns the compilation status, indicating whether the compilation
  // succeeded, or failed due to some reasons, like invalid shader stage or
  // compilation errors.
  shaderc_compilation_status GetCompilationStatus() const {
    if (!compilation_result_) {
      return shaderc_compilation_status_null_result_object;
    }
    return shaderc_result_get_compilation_status(compilation_result_);
  }

  // Returns a random access (contiguous) iterator pointing to the start
  // of the compilation output.  It is valid for the lifetime of this object.
  // If there is no compilation result, then returns nullptr.
  const_iterator cbegin() const {
    if (!compilation_result_) return nullptr;
    return reinterpret_cast<const_iterator>(
        shaderc_result_get_bytes(compilation_result_));
  }

  // Returns a random access (contiguous) iterator pointing to the end of
  // the compilation output.  It is valid for the lifetime of this object.
  // If there is no compilation result, then returns nullptr.
  const_iterator cend() const {
    if (!compilation_result_) return nullptr;
    return cbegin() +
           shaderc_result_get_length(compilation_result_) /
               sizeof(OutputElementType);
  }

  // Returns the same iterator as cbegin().
  const_iterator begin() const { return cbegin(); }
  // Returns the same iterator as cend().
  const_iterator end() const { return cend(); }

  // Returns the number of warnings generated during the compilation.
  size_t GetNumWarnings() const {
    if (!compilation_result_) {
      return 0;
    }
    return shaderc_result_get_num_warnings(compilation_result_);
  }

  // Returns the number of errors generated during the compilation.
  size_t GetNumErrors() const {
    if (!compilation_result_) {
      return 0;
    }
    return shaderc_result_get_num_errors(compilation_result_);
  }

 private:
  CompilationResult(const CompilationResult& other) = delete;
  CompilationResult& operator=(const CompilationResult& other) = delete;

  shaderc_compilation_result_t compilation_result_;
};

// A compilation result for a SPIR-V binary module, which is an array
// of uint32_t words.
using SpvCompilationResult = CompilationResult<uint32_t>;
// A compilation result in SPIR-V assembly syntax.
using AssemblyCompilationResult = CompilationResult<char>;
// Preprocessed source text.
using PreprocessedSourceCompilationResult = CompilationResult<char>;

// Contains any options that can have default values for a compilation.
class CompileOptions {
 public:
  CompileOptions() { options_ = shaderc_compile_options_initialize(); }
  ~CompileOptions() { shaderc_compile_options_release(options_); }
  CompileOptions(const CompileOptions& other) {
    options_ = shaderc_compile_options_clone(other.options_);
  }
  CompileOptions(CompileOptions&& other) {
    options_ = other.options_;
    other.options_ = nullptr;
  }

  // Adds a predefined macro to the compilation options. It behaves the same as
  // shaderc_compile_options_add_macro_definition in shaderc.h.
  void AddMacroDefinition(const char* name, size_t name_length,
                          const char* value, size_t value_length) {
    shaderc_compile_options_add_macro_definition(options_, name, name_length,
                                                 value, value_length);
  }

  // Adds a valueless predefined macro to the compilation options.
  void AddMacroDefinition(const std::string& name) {
    AddMacroDefinition(name.c_str(), name.size(), nullptr, 0u);
  }

  // Adds a predefined macro to the compilation options.
  void AddMacroDefinition(const std::string& name, const std::string& value) {
    AddMacroDefinition(name.c_str(), name.size(), value.c_str(), value.size());
  }

  // Sets the compiler mode to generate debug information in the output.
  void SetGenerateDebugInfo() {
    shaderc_compile_options_set_generate_debug_info(options_);
  }

  // A C++ version of the libshaderc includer interface.
  class IncluderInterface {
   public:
    // Handles shaderc_include_resolver_fn callbacks.
    virtual shaderc_include_result* GetInclude(
        const char* requested_source, shaderc_include_type type,
        const char* requesting_source,
        size_t include_depth) = 0;

    // Handles shaderc_include_result_release_fn callbacks.
    virtual void ReleaseInclude(shaderc_include_result* data) = 0;
  };

  // Sets the includer instance for libshaderc to call during compilation, as
  // described in shaderc_compile_options_set_include_callbacks().  Callbacks
  // are routed to this includer's methods.
  void SetIncluder(std::unique_ptr<IncluderInterface>&& includer) {
    includer_ = std::move(includer);
    shaderc_compile_options_set_include_callbacks(
        options_,
        [](void* user_data, const char* requested_source,
           int type, const char* requesting_source,
           size_t include_depth) {
          auto* includer = static_cast<IncluderInterface*>(user_data);
          return includer->GetInclude(requested_source,
                                      (shaderc_include_type)type,
                                      requesting_source, include_depth);
        },
        [](void* user_data, shaderc_include_result* include_result) {
          auto* includer = static_cast<IncluderInterface*>(user_data);
          return includer->ReleaseInclude(include_result);
        },
        includer_.get());
  }

  // Forces the GLSL language version and profile to a given pair. The version
  // number is the same as would appear in the #version annotation in the
  // source. Version and profile specified here overrides the #version
  // annotation in the source. Use profile: 'shaderc_profile_none' for GLSL
  // versions that do not define profiles, e.g. versions below 150.
  void SetForcedVersionProfile(int version, shaderc_profile profile) {
    shaderc_compile_options_set_forced_version_profile(options_, version,
                                                       profile);
  }

  // Sets the compiler mode to suppress warnings. Note this option overrides
  // warnings-as-errors mode. When both suppress-warnings and warnings-as-errors
  // modes are turned on, warning messages will be inhibited, and will not be
  // emitted as error message.
  void SetSuppressWarnings() {
    shaderc_compile_options_set_suppress_warnings(options_);
  }

  // Sets the target shader environment, affecting which warnings or errors will
  // be issued.
  // The version will be for distinguishing between different versions of the
  // target environment.
  // "0" is the only supported version at this point
  void SetTargetEnvironment(shaderc_target_env target, uint32_t version) {
    shaderc_compile_options_set_target_env(options_, target, version);
  }

  // Sets the compiler mode to make all warnings into errors. Note the
  // suppress-warnings mode overrides this option, i.e. if both
  // warning-as-errors and suppress-warnings modes are set on, warnings will not
  // be emitted as error message.
  void SetWarningsAsErrors() {
    shaderc_compile_options_set_warnings_as_errors(options_);
  }

 private:
  CompileOptions& operator=(const CompileOptions& other) = delete;
  shaderc_compile_options_t options_;
  std::unique_ptr<IncluderInterface> includer_;

  friend class Compiler;
};

// The compilation context for compiling source to SPIR-V.
class Compiler {
 public:
  Compiler() : compiler_(shaderc_compiler_initialize()) {}
  ~Compiler() { shaderc_compiler_release(compiler_); }

  Compiler(Compiler&& other) {
    compiler_ = other.compiler_;
    other.compiler_ = nullptr;
  }

  bool IsValid() const { return compiler_ != nullptr; }

  // Compiles the given source GLSL and returns a SPIR-V binary module
  // compilation result.
  // The source_text parameter must be a valid pointer.
  // The source_text_size parameter must be the length of the source text.
  // The shader_kind parameter either forces the compilation to be done with a
  // specified shader kind, or hint the compiler how to determine the exact
  // shader kind. If the shader kind is set to shaderc_glslc_infer_from_source,
  // the compiler will try to deduce the shader kind from the source string and
  // a failure in this proess will generate an error. Currently only #pragma
  // annotation is supported. If the shader kind is set to one of the default
  // shader kinds, the compiler will fall back to the specified default shader
  // kind in case it failed to deduce the shader kind from the source string.
  // The input_file_name is a null-termintated string. It is used as a tag to
  // identify the source string in cases like emitting error messages. It
  // doesn't have to be a 'file name'.
  // The compilation is passed any options specified in the CompileOptions
  // parameter.
  // It is valid for the returned CompilationResult object to outlive this
  // compiler object.
  // Note when the options_ has disassembly mode or preprocessing only mode set
  // on, the returned CompilationResult will hold a text string, instead of a
  // SPIR-V binary generated with default options.
  SpvCompilationResult CompileGlslToSpv(const char* source_text,
                                        size_t source_text_size,
                                        shaderc_shader_kind shader_kind,
                                        const char* input_file_name,
                                        const CompileOptions& options) const {
    shaderc_compilation_result_t compilation_result = shaderc_compile_into_spv(
        compiler_, source_text, source_text_size, shader_kind, input_file_name,
        "main", options.options_);
    return SpvCompilationResult(compilation_result);
  }

  // Compiles the given source GLSL and returns a SPIR-V binary module
  // compilation result.
  // Like the first CompileGlslToSpv method but uses default options.
  SpvCompilationResult CompileGlslToSpv(const char* source_text,
                                        size_t source_text_size,
                                        shaderc_shader_kind shader_kind,
                                        const char* input_file_name) const {
    shaderc_compilation_result_t compilation_result =
        shaderc_compile_into_spv(compiler_, source_text, source_text_size,
                                 shader_kind, input_file_name, "main", nullptr);
    return SpvCompilationResult(compilation_result);
  }

  // Compiles the given source GLSL and returns a SPIR-V binary module
  // compilation result.
  // Like the first CompileGlslToSpv method but the source is provided as
  // a std::string.
  SpvCompilationResult CompileGlslToSpv(const std::string& source_text,
                                        shaderc_shader_kind shader_kind,
                                        const char* input_file_name,
                                        const CompileOptions& options) const {
    return CompileGlslToSpv(source_text.data(), source_text.size(), shader_kind,
                            input_file_name, options);
  }

  // Compiles the given source GLSL and returns a SPIR-V binary module
  // compilation result.
  // Like the first CompileGlslToSpv method but the source is provided as
  // a std::string and also uses default compiler options.
  SpvCompilationResult CompileGlslToSpv(const std::string& source_text,
                                        shaderc_shader_kind shader_kind,
                                        const char* input_file_name) const {
    return CompileGlslToSpv(source_text.data(), source_text.size(), shader_kind,
                            input_file_name);
  }

  // Compiles the given source GLSL and returns the SPIR-V assembly text
  // compilation result.
  // Options are similar to the first CompileToSpv method.
  AssemblyCompilationResult CompileGlslToSpvAssembly(
      const char* source_text, size_t source_text_size,
      shaderc_shader_kind shader_kind, const char* input_file_name,
      const CompileOptions& options) const {
    shaderc_compilation_result_t compilation_result =
        shaderc_compile_into_spv_assembly(
            compiler_, source_text, source_text_size, shader_kind,
            input_file_name, "main", options.options_);
    return AssemblyCompilationResult(compilation_result);
  }

  // Compiles the given source GLSL and returns the SPIR-V assembly text
  // result. Like the first CompileGlslToSpvAssembly method but the source
  // is provided as a std::string.  Options are otherwise similar to
  // the first CompileToSpv method.
  AssemblyCompilationResult CompileGlslToSpvAssembly(
      const std::string& source_text, shaderc_shader_kind shader_kind,
      const char* input_file_name, const CompileOptions& options) const {
    return CompileGlslToSpvAssembly(source_text.data(), source_text.size(),
                                    shader_kind, input_file_name, options);
  }

  // Preprocesses the given source GLSL and returns the preprocessed
  // source text as a compilation result.
  // Options are similar to the first CompileToSpv method.
  PreprocessedSourceCompilationResult PreprocessGlsl(
      const char* source_text, size_t source_text_size,
      shaderc_shader_kind shader_kind, const char* input_file_name,
      const CompileOptions& options) const {
    shaderc_compilation_result_t compilation_result =
        shaderc_compile_into_preprocessed_text(
            compiler_, source_text, source_text_size, shader_kind,
            input_file_name, "main", options.options_);
    return PreprocessedSourceCompilationResult(compilation_result);
  }

  // Preprocesses the given source GLSL and returns text result.  Like the first
  // PreprocessGlsl method but the source is provided as a std::string.
  // Options are otherwise similar to the first CompileToSpv method.
  PreprocessedSourceCompilationResult PreprocessGlsl(
      const std::string& source_text, shaderc_shader_kind shader_kind,
      const char* input_file_name, const CompileOptions& options) const {
    return PreprocessGlsl(source_text.data(), source_text.size(), shader_kind,
                          input_file_name, options);
  }

 private:
  Compiler(const Compiler&) = delete;
  Compiler& operator=(const Compiler& other) = delete;

  shaderc_compiler_t compiler_;
};
}  // namespace shaderc

#endif  // SHADERC_SHADERC_HPP_
