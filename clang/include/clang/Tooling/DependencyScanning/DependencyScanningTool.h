//===- DependencyScanningTool.h - clang-scan-deps service -----------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_TOOLING_DEPENDENCYSCANNING_DEPENDENCYSCANNINGTOOL_H
#define LLVM_CLANG_TOOLING_DEPENDENCYSCANNING_DEPENDENCYSCANNINGTOOL_H

#include "clang/Tooling/DependencyScanning/DependencyScanningService.h"
#include "clang/Tooling/DependencyScanning/DependencyScanningWorker.h"
#include "clang/Tooling/DependencyScanning/ModuleDepCollector.h"
#include "clang/Tooling/JSONCompilationDatabase.h"
#include "llvm/ADT/MapVector.h"
#include "llvm/ADT/StringSet.h"
#include "llvm/ADT/StringMap.h"
#include "llvm/CAS/CASID.h"
#include <string>
#include <vector>

namespace llvm {
namespace cas {
class ObjectProxy;
} // namespace cas
} // namespace llvm

namespace clang {
namespace cas {
class IncludeTreeRoot;
}
namespace tooling {
namespace dependencies {

/// A callback to lookup module outputs for "-fmodule-file=", "-o" etc.
using LookupModuleOutputCallback =
    llvm::function_ref<std::string(const ModuleID &, ModuleOutputKind)>;

/// The full dependencies and module graph for a specific input.
struct FullDependencies {
  /// The identifier of the C++20 module this translation unit exports.
  ///
  /// If the translation unit is not a module then \c ID.ModuleName is empty.
  ModuleID ID;

  /// A collection of absolute paths to files that this translation unit
  /// directly depends on, not including transitive dependencies.
  std::vector<std::string> FileDeps;

  /// A collection of prebuilt modules this translation unit directly depends
  /// on, not including transitive dependencies.
  std::vector<PrebuiltModuleDep> PrebuiltModuleDeps;

  /// A list of modules this translation unit directly depends on, not including
  /// transitive dependencies.
  ///
  /// This may include modules with a different context hash when it can be
  /// determined that the differences are benign for this compilation.
  std::vector<ModuleID> ClangModuleDeps;

  /// The CASID for input file dependency tree.
  llvm::Optional<llvm::cas::CASID> CASFileSystemRootID;

  /// The sequence of commands required to build the translation unit. Commands
  /// should be executed in order.
  ///
  /// FIXME: If we add support for multi-arch builds in clang-scan-deps, we
  /// should make the dependencies between commands explicit to enable parallel
  /// builds of each architecture.
  std::vector<Command> Commands;

  /// Deprecated driver command-line. This will be removed in a future version.
  std::vector<std::string> DriverCommandLine;
};

struct FullDependenciesResult {
  FullDependencies FullDeps;
  std::vector<ModuleDeps> DiscoveredModules;
};

/// The high-level implementation of the dependency discovery tool that runs on
/// an individual worker thread.
class DependencyScanningTool {
public:
  /// Construct a dependency scanning tool.
  DependencyScanningTool(DependencyScanningService &Service,
                         llvm::IntrusiveRefCntPtr<llvm::vfs::FileSystem> FS =
                             llvm::vfs::createPhysicalFileSystem());

  /// Print out the dependency information into a string using the dependency
  /// file format that is specified in the options (-MD is the default) and
  /// return it. If \p ModuleName isn't empty, this function returns the
  /// dependency information of module \p ModuleName.
  ///
  /// \returns A \c StringError with the diagnostic output if clang errors
  /// occurred, dependency file contents otherwise.
  llvm::Expected<std::string>
  getDependencyFile(const std::vector<std::string> &CommandLine, StringRef CWD,
                    llvm::Optional<StringRef> ModuleName = None);

  /// Collect dependency tree.
  llvm::Expected<llvm::cas::ObjectProxy>
  getDependencyTree(const std::vector<std::string> &CommandLine, StringRef CWD);

  /// If \p DiagGenerationAsCompilation is true it will generate error
  /// diagnostics same way as the normal compilation, with "N errors generated"
  /// message and the serialized diagnostics file emitted if the
  /// \p DiagOpts.DiagnosticSerializationFile setting is set for the invocation.
  llvm::Expected<llvm::cas::ObjectProxy>
  getDependencyTreeFromCompilerInvocation(
      std::shared_ptr<CompilerInvocation> Invocation, StringRef CWD,
      DiagnosticConsumer &DiagsConsumer, raw_ostream *VerboseOS,
      bool DiagGenerationAsCompilation,
      llvm::function_ref<StringRef(const llvm::vfs::CachedDirectoryEntry &)>
          RemapPath = nullptr);

  Expected<cas::IncludeTreeRoot>
  getIncludeTree(cas::ObjectStore &DB,
                 const std::vector<std::string> &CommandLine, StringRef CWD);

  /// If \p DiagGenerationAsCompilation is true it will generate error
  /// diagnostics same way as the normal compilation, with "N errors generated"
  /// message and the serialized diagnostics file emitted if the
  /// \p DiagOpts.DiagnosticSerializationFile setting is set for the invocation.
  Expected<cas::IncludeTreeRoot> getIncludeTreeFromCompilerInvocation(
      cas::ObjectStore &DB, std::shared_ptr<CompilerInvocation> Invocation,
      StringRef CWD, DiagnosticConsumer &DiagsConsumer, raw_ostream *VerboseOS,
      bool DiagGenerationAsCompilation);

  /// Collect the full module dependency graph for the input, ignoring any
  /// modules which have already been seen. If \p ModuleName isn't empty, this
  /// function returns the full dependency information of module \p ModuleName.
  ///
  /// \param AlreadySeen This stores modules which have previously been
  ///                    reported. Use the same instance for all calls to this
  ///                    function for a single \c DependencyScanningTool in a
  ///                    single build. Use a different one for different tools,
  ///                    and clear it between builds.
  /// \param LookupModuleOutput This function is called to fill in
  ///                           "-fmodule-file=", "-o" and other output
  ///                           arguments for dependencies.
  ///
  /// \returns a \c StringError with the diagnostic output if clang errors
  /// occurred, \c FullDependencies otherwise.
  llvm::Expected<FullDependenciesResult>
  getFullDependencies(const std::vector<std::string> &CommandLine,
                      StringRef CWD, const llvm::StringSet<> &AlreadySeen,
                      LookupModuleOutputCallback LookupModuleOutput,
                      llvm::Optional<StringRef> ModuleName = None);

  ScanningOutputFormat getScanningFormat() const {
    return Worker.getScanningFormat();
  }

  const CASOptions &getCASOpts() const { return Worker.getCASOpts(); }

  llvm::cas::CachingOnDiskFileSystem &getCachingFileSystem() {
    return Worker.getCASFS();
  }

  /// If \p DependencyScanningService enabled sharing of \p FileManager this
  /// will return the same instance, otherwise it will create a new one for
  /// each invocation.
  llvm::IntrusiveRefCntPtr<FileManager> getOrCreateFileManager() const {
    return Worker.getOrCreateFileManager();
  }

  llvm::Expected<FullDependenciesResult> getFullDependenciesLegacyDriverCommand(
      const std::vector<std::string> &CommandLine, StringRef CWD,
      const llvm::StringSet<> &AlreadySeen,
      LookupModuleOutputCallback LookupModuleOutput,
      llvm::Optional<StringRef> ModuleName = None);

private:
  DependencyScanningWorker Worker;
};

class FullDependencyConsumer : public DependencyConsumer {
public:
  FullDependencyConsumer(const llvm::StringSet<> &AlreadySeen,
                         LookupModuleOutputCallback LookupModuleOutput,
                         bool EagerLoadModules)
      : AlreadySeen(AlreadySeen), LookupModuleOutput(LookupModuleOutput),
        EagerLoadModules(EagerLoadModules) {}

  void handleBuildCommand(Command Cmd) override {
    Commands.push_back(std::move(Cmd));
  }

  void handleDependencyOutputOpts(const DependencyOutputOptions &) override {}

  void handleFileDependency(StringRef File) override {
    Dependencies.push_back(std::string(File));
  }

  void handlePrebuiltModuleDependency(PrebuiltModuleDep PMD) override {
    PrebuiltModuleDeps.emplace_back(std::move(PMD));
  }

  void handleModuleDependency(ModuleDeps MD) override {
    ClangModuleDeps[MD.ID.ContextHash + MD.ID.ModuleName] = std::move(MD);
  }

  void handleContextHash(std::string Hash) override {
    ContextHash = std::move(Hash);
  }

  void handleCASFileSystemRootID(cas::CASID ID) override {
    CASFileSystemRootID = ID;
  }

  std::string lookupModuleOutput(const ModuleID &ID,
                                 ModuleOutputKind Kind) override {
    return LookupModuleOutput(ID, Kind);
  }

  FullDependenciesResult getFullDependenciesLegacyDriverCommand(
      const std::vector<std::string> &OriginalCommandLine) const;

  FullDependenciesResult takeFullDependencies();

private:
  std::vector<std::string> Dependencies;
  std::vector<PrebuiltModuleDep> PrebuiltModuleDeps;
  llvm::MapVector<std::string, ModuleDeps, llvm::StringMap<unsigned>>
      ClangModuleDeps;
  std::vector<Command> Commands;
  std::string ContextHash;
  Optional<cas::CASID> CASFileSystemRootID;
  std::vector<std::string> OutputPaths;
  const llvm::StringSet<> &AlreadySeen;
  LookupModuleOutputCallback LookupModuleOutput;
  bool EagerLoadModules;
};

} // end namespace dependencies
} // end namespace tooling
} // end namespace clang

#endif // LLVM_CLANG_TOOLING_DEPENDENCYSCANNING_DEPENDENCYSCANNINGTOOL_H
