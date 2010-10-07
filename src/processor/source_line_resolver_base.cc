// Copyright (c) 2010 Google Inc.
// All rights reserved.
//
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are
// met:
//
//     * Redistributions of source code must retain the above copyright
// notice, this list of conditions and the following disclaimer.
//     * Redistributions in binary form must reproduce the above
// copyright notice, this list of conditions and the following disclaimer
// in the documentation and/or other materials provided with the
// distribution.
//     * Neither the name of Google Inc. nor the names of its
// contributors may be used to endorse or promote products derived from
// this software without specific prior written permission.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
// "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
// LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
// A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
// OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
// SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
// LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
// DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
// THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
// (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
// OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
//
// source_line_resolver_base.cc: Implementation of SourceLineResolverBase.
//
// See source_line_resolver_base.h and source_line_resolver_base_types.h for
// more documentation.
//
// Author: Siyang Xie (lambxsy@google.com)

#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

#include <map>
#include <utility>

#include "google_breakpad/processor/source_line_resolver_base.h"
#include "processor/source_line_resolver_base_types.h"
#include "processor/module_factory.h"

using std::map;
using std::make_pair;

namespace google_breakpad {

SourceLineResolverBase::SourceLineResolverBase(
    ModuleFactory *module_factory)
  : modules_(new ModuleMap),
    module_factory_(module_factory) {
}

SourceLineResolverBase::~SourceLineResolverBase() {
  ModuleMap::iterator it;
  // Iterate through ModuleMap and delete all loaded modules.
  for (it = modules_->begin(); it != modules_->end(); ++it) {
    // Delete individual module.
    delete it->second;
  }
  // Delete the map of modules.
  delete modules_;
  delete module_factory_;

  // Helper method to be specified by subclasses.
  ClearLocalMemory();
}

// Helper methods to be specified by subclasses.
void SourceLineResolverBase::StoreDataBeforeLoad(const CodeModule *module,
                                                 char *symbol_data) { }
void SourceLineResolverBase::DeleteDataAfterLoad(char *symbol_data) { }
void SourceLineResolverBase::DeleteDataUnload(const CodeModule *module) { }
void SourceLineResolverBase::ClearLocalMemory() { }


bool SourceLineResolverBase::ReadSymbolFile(char **symbol_data,
                                            const string &map_file) {
  if (symbol_data == NULL) {
    BPLOG(ERROR) << "Could not Read file into Null memory pointer";
    return false;
  }

  struct stat buf;
  int error_code = stat(map_file.c_str(), &buf);
  if (error_code == -1) {
    string error_string;
    int error_code = ErrnoString(&error_string);
    BPLOG(ERROR) << "Could not open " << map_file <<
        ", error " << error_code << ": " << error_string;
    return false;
  }

  off_t file_size = buf.st_size;

  // Allocate memory for file contents, plus a null terminator
  // since we may use strtok() on the contents.
  *symbol_data = reinterpret_cast<char*>(operator new(file_size + 1));

  if (*symbol_data == NULL) {
    BPLOG(ERROR) << "Could not allocate memory for " << map_file;
    return false;
  }

  BPLOG(INFO) << "Opening " << map_file;

  FILE *f = fopen(map_file.c_str(), "rt");
  if (!f) {
    string error_string;
    int error_code = ErrnoString(&error_string);
    BPLOG(ERROR) << "Could not open " << map_file <<
        ", error " << error_code << ": " << error_string;
    delete (*symbol_data);
    *symbol_data = NULL;
    return false;
  }

  AutoFileCloser closer(f);

  int items_read = 0;

  items_read = fread(*symbol_data, 1, file_size, f);

  if (items_read != file_size) {
    string error_string;
    int error_code = ErrnoString(&error_string);
    BPLOG(ERROR) << "Could not slurp " << map_file <<
        ", error " << error_code << ": " << error_string;
    delete (*symbol_data);
    *symbol_data = NULL;
    return false;
  }

  (*symbol_data)[file_size] = '\0';
  return true;
}

bool SourceLineResolverBase::LoadModule(const CodeModule *module,
                                        const string &map_file) {
  if (module == NULL)
    return false;

  // Make sure we don't already have a module with the given name.
  if (modules_->find(module->code_file()) != modules_->end()) {
    BPLOG(INFO) << "Symbols for module " << module->code_file()
                << " already loaded";
    return false;
  }

  BPLOG(INFO) << "Loading symbols for module " << module->code_file()
              << " from " << map_file;

  char *memory_buffer;
  if (!ReadSymbolFile(&memory_buffer, map_file))
    return false;

  BPLOG(INFO) << "Read symbol file " << map_file << " succeeded";

  // Invoke helper method, let the concrete subclass decides its own action.
  StoreDataBeforeLoad(module, memory_buffer);

  return LoadModuleUsingMemoryBuffer(module, memory_buffer);
}

bool SourceLineResolverBase::LoadModuleUsingMapBuffer(
    const CodeModule *module, const string &map_buffer) {
  char *memory_buffer = reinterpret_cast<char*>(
      operator new(map_buffer.size() + 1));
  if (memory_buffer == NULL)
    return false;

  // Can't use strcpy, as the data may contain '\0's before the end.
  memcpy(memory_buffer, map_buffer.c_str(), map_buffer.size());
  memory_buffer[map_buffer.size()] = '\0';

  // Invoke helper method, let the concrete subclass decides its own action.
  StoreDataBeforeLoad(module, memory_buffer);

  return LoadModuleUsingMemoryBuffer(module, memory_buffer);
}

bool SourceLineResolverBase::LoadModuleUsingMemoryBuffer(
    const CodeModule *module, char *memory_buffer) {
  if (!module) {
    // Invoke helper method, let the concrete subclass decides its own action.
    DeleteDataAfterLoad(memory_buffer);
    return false;
  }

  // Make sure we don't already have a module with the given name.
  if (modules_->find(module->code_file()) != modules_->end()) {
    BPLOG(INFO) << "Symbols for module " << module->code_file()
                << " already loaded";
    DeleteDataAfterLoad(memory_buffer);
    return false;
  }

  BPLOG(INFO) << "Loading symbols for module " << module->code_file()
             << " from buffer";

  Module *basic_module = module_factory_->CreateModule(module->code_file());

  // Ownership of memory is NOT transfered to Module::LoadMapFromMemory().
  if (!basic_module->LoadMapFromMemory(memory_buffer)) {
    delete basic_module;
    DeleteDataAfterLoad(memory_buffer);
    return false;
  }

  modules_->insert(make_pair(module->code_file(), basic_module));
  DeleteDataAfterLoad(memory_buffer);
  return true;
}

void SourceLineResolverBase::UnloadModule(const CodeModule *code_module) {
  if (!code_module)
    return;

  ModuleMap::iterator iter = modules_->find(code_module->code_file());
  if (iter != modules_->end()) {
    Module *symbol_module = iter->second;
    delete symbol_module;
    modules_->erase(iter);
  }

  DeleteDataUnload(code_module);
}

bool SourceLineResolverBase::HasModule(const CodeModule *module) {
  if (!module)
    return false;
  return modules_->find(module->code_file()) != modules_->end();
}

void SourceLineResolverBase::FillSourceLineInfo(StackFrame *frame) {
  if (frame->module) {
    ModuleMap::const_iterator it = modules_->find(frame->module->code_file());
    if (it != modules_->end()) {
      it->second->LookupAddress(frame);
    }
  }
}

WindowsFrameInfo *SourceLineResolverBase::FindWindowsFrameInfo(
    const StackFrame *frame) {
  if (frame->module) {
    ModuleMap::const_iterator it = modules_->find(frame->module->code_file());
    if (it != modules_->end()) {
      return it->second->FindWindowsFrameInfo(frame);
    }
  }
  return NULL;
}

CFIFrameInfo *SourceLineResolverBase::FindCFIFrameInfo(
    const StackFrame *frame) {
  if (frame->module) {
    ModuleMap::const_iterator it = modules_->find(frame->module->code_file());
    if (it != modules_->end()) {
      return it->second->FindCFIFrameInfo(frame);
    }
  }
  return NULL;
}

bool SourceLineResolverBase::CompareString::operator()(
    const string &s1, const string &s2) const {
  return strcmp(s1.c_str(), s2.c_str()) < 0;
}

bool SourceLineResolverBase::Module::ParseCFIRuleSet(
    const string &rule_set, CFIFrameInfo *frame_info) const {
  CFIFrameInfoParseHandler handler(frame_info);
  CFIRuleParser parser(&handler);
  return parser.Parse(rule_set);
}

}  // namespace google_breakpad
