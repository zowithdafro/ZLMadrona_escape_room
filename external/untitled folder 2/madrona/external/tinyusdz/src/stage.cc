/*
Copyright (c) 2019 - Present, Syoyo Fujita.
All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are met:
    * Redistributions of source code must retain the above copyright
      notice, this list of conditions and the following disclaimer.
    * Redistributions in binary form must reproduce the above copyright
      notice, this list of conditions and the following disclaimer in the
      documentation and/or other materials provided with the distribution.
    * Neither the name of the Syoyo Fujita nor the
      names of its contributors may be used to endorse or promote products
      derived from this software without specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
DISCLAIMED. IN NO EVENT SHALL <COPYRIGHT HOLDER> BE LIABLE FOR ANY
DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
(INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
(INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/

#include <algorithm>
#include <atomic>
// #include <cassert>
#include <cctype>  // std::tolower
#include <chrono>
#include <fstream>
#include <map>
#include <sstream>

//
#ifndef __wasi__
#include <thread>
#endif
//
#include <tuple>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "io-util.hh"
#include "pprinter.hh"
#include "str-util.hh"
#include "tiny-format.hh"
#include "tinyusdz.hh"
#include "usdLux.hh"
#include "usdShade.hh"
#include "usda-reader.hh"
#include "value-pprint.hh"
//
#include "common-macros.inc"

namespace tinyusdz {

#if 1
// For PUSH_ERROR_AND_RETURN
#define PushError(s) _err += s;
// #define PushWarn(s) if (warn) { (*warn) += s; }
#endif

namespace {

#if 0  // not used yet

///
/// Node represents scene graph node.
/// This does not contain leaf node inormation.
///
class Node {
 public:
  // -2 = initialize as invalid node
  Node() : _parent(-2) {}

  Node(int64_t parent, Path &path) : _parent(parent), _path(path) {}

  int64_t GetParent() const { return _parent; }

  const std::vector<size_t> &GetChildren() const { return _children; }

  ///
  /// child_name is used when reconstructing scene graph.
  ///
  bool AddChildren(const std::string &child_name, size_t node_index) {
    if (_primChildren.count(child_name)) {
      return false;
    }
    //assert(_primChildren.count(child_name) == 0);
    _primChildren.emplace(child_name);
    _children.push_back(node_index);

    return true;
  }

  ///
  /// Get full path(e.g. `/muda/dora/bora` when the parent is `/muda/dora` and
  /// this node is `bora`)
  ///
  // std::string GetFullPath() const { return _path.full_path_name(); }

  ///
  /// Get local path
  ///
  std::string GetLocalPath() const { return _path.full_path_name(); }

  const Path &GetPath() const { return _path; }

  // NodeType GetNodeType() const { return _node_type; }

  const std::unordered_set<std::string> &GetPrimChildren() const {
    return _primChildren;
  }

  void SetAssetInfo(const value::dict &dict) { _assetInfo = dict; }

  const value::dict &GetAssetInfo() const { return _assetInfo; }

 private:
  int64_t
      _parent;  // -1 = this node is the root node. -2 = invalid or leaf node
  std::vector<size_t> _children;                  // index to child nodes.
  std::unordered_set<std::string> _primChildren;  // List of name of child nodes

  Path _path;  // local path
  value::dict _assetInfo;

  // NodeType _node_type;
};
#endif

nonstd::optional<const Prim *> GetPrimAtPathRec(const Prim *parent,
                                                const std::string &parent_path,
                                                const Path &path,
                                                const uint32_t depth) {
  std::string abs_path;
  // if (auto pv = GetPrimElementName(parent->data())) {
  {
    std::string elementName = parent->element_path().prim_part();
    // DCOUT(pprint::Indent(depth) << "Prim elementName = " << elementName);
    // DCOUT(pprint::Indent(depth) << "Given Path = " << path);
    //  fully absolute path
    abs_path = parent_path + "/" + elementName;
    // DCOUT(pprint::Indent(depth) << "abs_path = " << abs_path);
    // DCOUT(pprint::Indent(depth)
    //       << "queriying path = " << path.full_path_name());
    if (abs_path == path.full_path_name()) {
      // DCOUT(pprint::Indent(depth)
      //       << "Got it! Found Prim at Path = " << abs_path);
      return parent;
    }
  }

  // DCOUT(pprint::Indent(depth)
  //       << "# of children : " << parent->children().size());
  for (const auto &child : parent->children()) {
    // const std::string &p = parent->elementPath.full_path_name();
    // DCOUT(pprint::Indent(depth + 1) << "Parent path : " << abs_path);
    if (auto pv = GetPrimAtPathRec(&child, abs_path, path, depth + 1)) {
      return pv.value();
    }
  }

  return nonstd::nullopt;
}

}  // namespace

//
// -- Stage
//

nonstd::expected<const Prim *, std::string> Stage::GetPrimAtPath(
    const Path &path) const {
  DCOUT("GetPrimAtPath : " << path.prim_part() << "(input path: " << path
                           << ")");

  if (_dirty) {
    DCOUT("clear cache.");
    // Clear cache.
    _prim_path_cache.clear();

    _dirty = false;
  } else {
    // First find from a cache.
    auto ret = _prim_path_cache.find(path.prim_part());
    if (ret != _prim_path_cache.end()) {
      DCOUT("Found cache.");
      return ret->second;
    }
  }

  if (!path.is_valid()) {
    DCOUT("Invalid path.");
    return nonstd::make_unexpected("Path is invalid.\n");
  }

  if (path.is_relative_path()) {
    DCOUT("Relative path is todo.");
    // TODO:
    return nonstd::make_unexpected("Relative path is TODO.\n");
  }

  if (!path.is_absolute_path()) {
    DCOUT("Not absolute path.");
    return nonstd::make_unexpected(
        "Path is not absolute. Non-absolute Path is TODO.\n");
  }

  // Brute-force search.
  for (const auto &parent : _root_nodes) {
    if (auto pv =
            GetPrimAtPathRec(&parent, /* root */ "", path, /* depth */ 0)) {
      // Add to cache.
      // Assume pointer address does not change unless dirty state.
      _prim_path_cache[path.prim_part()] = pv.value();
      return pv.value();
    }
  }

  DCOUT("Not found.");
  return nonstd::make_unexpected("Cannot find path <" + path.full_path_name() +
                                 "> int the Stage.\n");
}

bool Stage::find_prim_at_path(const Path &path, const Prim *&prim,
                              std::string *err) const {
  nonstd::expected<const Prim *, std::string> ret = GetPrimAtPath(path);
  if (ret) {
    prim = ret.value();
    return true;
  } else {
    if (err) {
      (*err) = ret.error();
    }
    return false;
  }
}

bool Stage::find_prim_at_path(const Path &path, int64_t *prim_id,
                              std::string *err) const {
  if (!prim_id) {
    if (err) {
      (*err) = "`prim_id` argument is nullptr.\n";
    }
    return false;
  }

  nonstd::expected<const Prim *, std::string> ret = GetPrimAtPath(path);
  if (ret) {
    (*prim_id) = ret.value()->prim_id();
    return true;
  } else {
    if (err) {
      (*err) = ret.error();
    }
    return false;
  }
}

namespace {

bool FindPrimByPrimIdRec(uint64_t prim_id, const Prim *root,
                         const Prim **primFound, int level, std::string *err) {
  if (level > 1024 * 1024 * 128) {
    // too deep node.
    return false;
  }

  if (!primFound) {
    return false;
  }

  if (root->prim_id() == int64_t(prim_id)) {
    (*primFound) = root;
    return true;
  }

  // Brute-force search.
  for (const auto &child : root->children()) {
    if (FindPrimByPrimIdRec(prim_id, &child, primFound, level + 1, err)) {
      return true;
    }
  }

  return false;
}

}  // namespace

bool Stage::find_prim_by_prim_id(const uint64_t prim_id, const Prim *&prim,
                                 std::string *err) const {
  if (prim_id < 1) {
    if (err) {
      (*err) = "Input prim_id must be 1 or greater.";
    }
    return false;
  }

  if (_prim_id_dirty) {
    DCOUT("clear prim_id cache.");
    // Clear cache.
    _prim_id_cache.clear();

    _prim_id_dirty = false;
  } else {
    // First find from a cache.
    auto ret = _prim_id_cache.find(prim_id);
    if (ret != _prim_id_cache.end()) {
      DCOUT("Found cache.");
      return ret->second;
    }
  }

  const Prim *p{nullptr};
  for (const auto &root : root_prims()) {
    if (FindPrimByPrimIdRec(prim_id, &root, &p, 0, err)) {
      _prim_id_cache[prim_id] = p;
      prim = p;
      return true;
    }
  }

  return false;
}

bool Stage::find_prim_by_prim_id(const uint64_t prim_id, Prim *&prim,
                                 std::string *err) {
  const Prim *c_prim{nullptr};
  if (!find_prim_by_prim_id(prim_id, c_prim, err)) {
    return false;
  }

  // remove const
  prim = const_cast<Prim *>(c_prim);

  return true;
}

nonstd::expected<const Prim *, std::string> Stage::GetPrimFromRelativePath(
    const Prim &root, const Path &path) const {
  // TODO: Resolve "../"
  // TODO: cache path

  if (!path.is_valid()) {
    return nonstd::make_unexpected("Path is invalid.\n");
  }

  if (path.is_absolute_path()) {
    return nonstd::make_unexpected(
        "Path is absolute. Path must be relative.\n");
  }

  if (path.is_relative_path()) {
    // ok
  } else {
    return nonstd::make_unexpected("Invalid Path.\n");
  }

#if 0  // TODO
  Path abs_path = root.element_path();
  abs_path.AppendElement(path.GetPrimPart());

  DCOUT("root path = " << root.path());
  DCOUT("abs path = " << abs_path);

  // Brute-force search from Stage root.
  if (auto pv = GetPrimAtPathRec(&root, /* root */"", abs_path, /* depth */0)) {
    return pv.value();
  }

  return nonstd::make_unexpected("Cannot find path <" + path.full_path_name() +
                                 "> under Prim: " + to_string(root.path) +
                                 "\n");
#else
  (void)root;
  return nonstd::make_unexpected("GetPrimFromRelativePath is TODO");
#endif
}

bool Stage::find_prim_from_relative_path(const Prim &root,
                                         const Path &relative_path,
                                         const Prim *&prim,
                                         std::string *err) const {
  nonstd::expected<const Prim *, std::string> ret =
      GetPrimFromRelativePath(root, relative_path);
  if (ret) {
    prim = ret.value();
    return true;
  } else {
    if (err) {
      (*err) = ret.error();
    }
    return false;
  }
}

bool Stage::LoadLayerFromMemory(const uint8_t *addr, const size_t nbytes,
                                const std::string &asset_name, Layer *layer,
                                const uint32_t load_states) {
  // TODO: USDC/USDZ support.

  tinyusdz::StreamReader sr(addr, nbytes, /* swap endian */ false);
  tinyusdz::usda::USDAReader reader(&sr);

  // TODO: Uase AssetResolver
  // reader.SetBaseDir(base_dir);

  if (!reader.read(load_states)) {
    return false;
  }

  if (!reader.get_as_layer(layer)) {
    PUSH_ERROR_AND_RETURN(
        "Failed to retrieve USD data as Layer: filepath = " << asset_name);
  }

  return false;
}

bool Stage::LoadLayerFromFile(const std::string &_filename, Layer *layer,
                              const uint32_t load_states) {
  // TODO: Setup AssetResolver.

  std::string filepath = io::ExpandFilePath(_filename, /* userdata */ nullptr);
  std::string base_dir = io::GetBaseDir(_filename);

  DCOUT("load layer from file: " << filepath);

  std::string err;
  std::vector<uint8_t> data;
  size_t max_bytes = std::numeric_limits<size_t>::max();  // TODO:
  if (!io::ReadWholeFile(&data, &err, filepath, max_bytes,
                         /* userdata */ nullptr)) {
    PUSH_ERROR_AND_RETURN("Read file failed: " + err);
  }

  return LoadLayerFromMemory(data.data(), data.size(), filepath, layer,
                             load_states);
}

bool Stage::LoadSubLayers(std::vector<Layer> *sublayers) {
  (void)sublayers;
  return false;
}

namespace {

void PrimPrintRec(std::stringstream &ss, const Prim &prim, uint32_t indent) {
  // Currently, Prim's elementName is read from name variable in concrete Prim
  // class(e.g. Xform::name).
  // TODO: use prim.elementPath for elementName.
  std::string s = pprint_value(prim.data(), indent, /* closing_brace */ false);

  bool require_newline = true;

  // Check last 2 chars.
  // if it ends with '{\n', no properties are authored so do not emit blank line
  // before printing VariantSet or child Prims.
  if (s.size() > 2) {
    if ((s[s.size() - 2] == '{') && (s[s.size() - 1] == '\n')) {
      require_newline = false;
    }
  }

  ss << s;

  //
  // print variant
  //
  if (prim.variantSets().size()) {
    if (require_newline) {
      ss << "\n";
    }

    // need to add blank line after VariantSet stmt and before child Prims,
    // so set require_newline true
    require_newline = true;

    for (const auto &variantSet : prim.variantSets()) {
      ss << pprint::Indent(indent + 1) << "variantSet "
         << quote(variantSet.first) << " = {\n";

      for (const auto &variantItem : variantSet.second.variantSet) {
        ss << pprint::Indent(indent + 2) << quote(variantItem.first);

        const Variant &variant = variantItem.second;

        if (variant.metas().authored()) {
          ss << " (\n";
          ss << print_prim_metas(variant.metas(), indent + 3);
          ss << pprint::Indent(indent + 2) << ")";
        }

        ss << " {\n";

        ss << print_props(variant.properties(), indent + 3);

        if (variant.metas().variantChildren.has_value() &&
            (variant.metas().variantChildren.value().size() ==
             variant.primChildren().size())) {
          std::map<std::string, const Prim *> primNameTable;
          for (size_t i = 0; i < variant.primChildren().size(); i++) {
            primNameTable.emplace(variant.primChildren()[i].element_name(),
                                  &variant.primChildren()[i]);
          }

          for (size_t i = 0; i < variant.metas().variantChildren.value().size();
               i++) {
            value::token nameTok = variant.metas().variantChildren.value()[i];
            const auto it = primNameTable.find(nameTok.str());
            if (it != primNameTable.end()) {
              PrimPrintRec(ss, *(it->second), indent + 3);
              if (i != (variant.primChildren().size() - 1)) {
                ss << "\n";
              }
            } else {
              // TODO: Report warning?
            }
          }

        } else {
          for (size_t i = 0; i < variant.primChildren().size(); i++) {
            PrimPrintRec(ss, variant.primChildren()[i], indent + 3);
            if (i != (variant.primChildren().size() - 1)) {
              ss << "\n";
            }
          }
        }

        ss << pprint::Indent(indent + 2) << "}\n";
      }

      ss << pprint::Indent(indent + 1) << "}\n";
    }
  }

  DCOUT(prim.element_name() << " num_children = " << prim.children().size());

  //
  // primChildren
  //
  if (prim.children().size()) {
    if (require_newline) {
      ss << "\n";
      require_newline = false;
    }
    if (prim.metas().primChildren.size() == prim.children().size()) {
      // Use primChildren info to determine the order of the traversal.

      std::map<std::string, const Prim *> primNameTable;
      for (size_t i = 0; i < prim.children().size(); i++) {
        primNameTable.emplace(prim.children()[i].element_name(),
                              &prim.children()[i]);
      }

      for (size_t i = 0; i < prim.metas().primChildren.size(); i++) {
        if (i > 0) {
          ss << "\n";
        }
        value::token nameTok = prim.metas().primChildren[i];
        DCOUT(fmt::format("primChildren  {}/{} = {}", i,
                          prim.metas().primChildren.size(), nameTok.str()));
        const auto it = primNameTable.find(nameTok.str());
        if (it != primNameTable.end()) {
          PrimPrintRec(ss, *(it->second), indent + 1);
        } else {
          // TODO: Report warning?
        }
      }

    } else {
      for (size_t i = 0; i < prim.children().size(); i++) {
        if (i > 0) {
          ss << "\n";
        }
        PrimPrintRec(ss, prim.children()[i], indent + 1);
      }
    }
  }

  ss << pprint::Indent(indent) << "}\n";
}

}  // namespace

std::string Stage::ExportToString(bool relative_path) const {
  (void)relative_path; // TODO

  std::stringstream ss;

  bool authored = false;

  ss << "#usda 1.0\n";

  std::stringstream meta_ss;
  if (stage_metas.doc.value.empty()) {
    // ss << pprint::Indent(1) << "doc = \"Exporterd from TinyUSDZ v" <<
    // tinyusdz::version_major
    //    << "." << tinyusdz::version_minor << "." << tinyusdz::version_micro
    //    << tinyusdz::version_rev << "\"\n";
  } else {
    meta_ss << pprint::Indent(1) << "doc = " << to_string(stage_metas.doc)
            << "\n";
    authored = true;
  }

  if (stage_metas.metersPerUnit.authored()) {
    meta_ss << pprint::Indent(1)
            << "metersPerUnit = " << stage_metas.metersPerUnit.get_value()
            << "\n";
    authored = true;
  }

  if (stage_metas.upAxis.authored()) {
    meta_ss << pprint::Indent(1)
            << "upAxis = " << quote(to_string(stage_metas.upAxis.get_value()))
            << "\n";
    authored = true;
  }

  if (stage_metas.timeCodesPerSecond.authored()) {
    meta_ss << pprint::Indent(1) << "timeCodesPerSecond = "
            << stage_metas.timeCodesPerSecond.get_value() << "\n";
    authored = true;
  }

  if (stage_metas.startTimeCode.authored()) {
    meta_ss << pprint::Indent(1)
            << "startTimeCode = " << stage_metas.startTimeCode.get_value()
            << "\n";
    authored = true;
  }

  if (stage_metas.endTimeCode.authored()) {
    meta_ss << pprint::Indent(1)
            << "endTimeCode = " << stage_metas.endTimeCode.get_value() << "\n";
    authored = true;
  }

  if (stage_metas.framesPerSecond.authored()) {
    meta_ss << pprint::Indent(1)
            << "framesPerSecond = " << stage_metas.framesPerSecond.get_value()
            << "\n";
    authored = true;
  }

  // TODO: Do not print subLayers when consumed(after composition evaluated)
  if (stage_metas.subLayers.size()) {
    meta_ss << pprint::Indent(1) << "subLayers = " << stage_metas.subLayers
            << "\n";
    authored = true;
  }

  if (stage_metas.defaultPrim.str().size()) {
    meta_ss << pprint::Indent(1) << "defaultPrim = "
            << tinyusdz::quote(stage_metas.defaultPrim.str()) << "\n";
    authored = true;
  }

  if (stage_metas.autoPlay.authored()) {
    meta_ss << pprint::Indent(1)
            << "autoPlay = " << to_string(stage_metas.autoPlay.get_value())
            << "\n";
    authored = true;
  }

  if (stage_metas.playbackMode.authored()) {
    auto v = stage_metas.playbackMode.get_value();
    if (v == StageMetas::PlaybackMode::PlaybackModeLoop) {
      meta_ss << pprint::Indent(1) << "playbackMode = \"loop\"\n";
    } else {  // None
      meta_ss << pprint::Indent(1) << "playbackMode = \"none\"\n";
    }
    authored = true;
  }

  if (!stage_metas.comment.value.empty()) {
    // Stage meta omits 'comment'
    meta_ss << pprint::Indent(1) << to_string(stage_metas.comment) << "\n";
    authored = true;
  }

  if (stage_metas.customLayerData.size()) {
    meta_ss << print_customData(stage_metas.customLayerData, "customLayerData",
                                /* indent */ 1);
    authored = true;
  }

  if (authored) {
    ss << "(\n";
    ss << meta_ss.str();
    ss << ")\n";
  }

  ss << "\n";

  if (stage_metas.primChildren.size() == _root_nodes.size()) {
    std::map<std::string, const Prim *> primNameTable;
    for (size_t i = 0; i < _root_nodes.size(); i++) {
      primNameTable.emplace(_root_nodes[i].element_name(), &_root_nodes[i]);
    }

    for (size_t i = 0; i < stage_metas.primChildren.size(); i++) {
      value::token nameTok = stage_metas.primChildren[i];
      DCOUT(fmt::format("primChildren  {}/{} = {}", i,
                        stage_metas.primChildren.size(), nameTok.str()));
      const auto it = primNameTable.find(nameTok.str());
      if (it != primNameTable.end()) {
        PrimPrintRec(ss, *(it->second), 0);
        if (i != (stage_metas.primChildren.size() - 1)) {
          ss << "\n";
        }
      } else {
        // TODO: Report warning?
      }
    }
  } else {
    for (size_t i = 0; i < _root_nodes.size(); i++) {
      PrimPrintRec(ss, _root_nodes[i], 0);

      if (i != (_root_nodes.size() - 1)) {
        ss << "\n";
      }
    }
  }

  return ss.str();
}

bool Stage::allocate_prim_id(uint64_t *prim_id) const {
  if (!prim_id) {
    return false;
  }

  uint64_t val;
  if (_prim_id_allocator.Allocate(&val)) {
    (*prim_id) = val;
    return true;
  }

  return false;
}

bool Stage::release_prim_id(const uint64_t prim_id) const {
  return _prim_id_allocator.Release(prim_id);
}

bool Stage::has_prim_id(const uint64_t prim_id) const {
  return _prim_id_allocator.Has(prim_id);
}

namespace {

bool ComputeAbsPathAndAssignPrimIdRec(const Stage &stage, Prim &prim,
                                      const Path &parentPath, uint32_t depth,
                                      bool assign_prim_id,
                                      bool force_assign_prim_id = true,
                                      std::string *err = nullptr) {
  if (depth > 1024 * 1024 * 128) {
    // too deep node.
    if (err) {
      (*err) += "Prim hierarchy too deep.\n";
    }
    return false;
  }

  if (prim.element_name().empty()) {
    // Prim's elementName must not be empty.
    if (err) {
      (*err) += "Prim's elementName is empty. Prim's parent Path = " +
                parentPath.full_path_name() + "\n";
    }
    return false;
  }

  Path abs_path = parentPath.AppendPrim(prim.element_name());

  prim.absolute_path() = abs_path;
  if (assign_prim_id) {
    if (force_assign_prim_id || (prim.prim_id() < 1)) {
      uint64_t prim_id{0};
      if (!stage.allocate_prim_id(&prim_id)) {
        if (err) {
          (*err) += "Failed to assign unique Prim ID.\n";
        }
        return false;
      }
      prim.prim_id() = int64_t(prim_id);
    }
  }

  for (Prim &child : prim.children()) {
    if (!ComputeAbsPathAndAssignPrimIdRec(stage, child, abs_path, depth + 1,
                                          assign_prim_id, force_assign_prim_id,
                                          err)) {
      return false;
    }
  }

  return true;
}

}  // namespace

bool Stage::compute_absolute_prim_path_and_assign_prim_id(
    bool force_assign_prim_id) {
  Path rootPath("/", "");
  for (Prim &root : root_prims()) {
    if (!ComputeAbsPathAndAssignPrimIdRec(*this, root, rootPath, 1,
                                          /* assign_prim_id */ true,
                                          force_assign_prim_id, &_err)) {
      return false;
    }
  }

  // TODO: Only set dirty when prim_id changed.
  _prim_id_dirty = true;

  return true;
}

bool Stage::compute_absolute_prim_path() {
  Path rootPath("/", "");
  for (Prim &root : root_prims()) {
    if (!ComputeAbsPathAndAssignPrimIdRec(
            *this, root, rootPath, 1, /* assign prim_id */ false,
            /* force_assign_prim_id */ true, &_err)) {
      return false;
    }
  }

  return true;
}

bool Stage::add_root_prim(Prim &&prim, bool rename_prim_name) {

#if defined(TINYUSD_ENABLE_THREAD)
  // TODO: Only take a lock when dirty.
  std::lock_guard<std::mutex> lock(_mutex);
#endif


  std::string elementName = prim.element_name();

  if (elementName.empty()) {
    if (rename_prim_name) {

      // assign default name `default`
      elementName = "default";

      if (!SetPrimElementName(prim.get_data(), elementName)) {
        PUSH_ERROR_AND_RETURN("Internal error. cannot modify Prim's elementName");
      }
      prim.element_path() = Path(elementName, /* prop_part */"");
    } else {
      PUSH_ERROR_AND_RETURN("Prim has empty elementName.");
    }
  }

  if (_root_nodes.size() != _root_node_nameSet.size()) {
    // Rebuild nameSet
    _root_node_nameSet.clear();
    for (size_t i = 0; i < _root_nodes.size(); i++) {
      if (_root_nodes[i].element_name().empty()) {
        PUSH_ERROR_AND_RETURN("Internal error: Existing root Prim's elementName is empty.");
      }

      if (_root_node_nameSet.count(_root_nodes[i].element_name())) {
        PUSH_ERROR_AND_RETURN("Internal error: Stage contains root Prim with same elementName.");
      }

      _root_node_nameSet.insert(_root_nodes[i].element_name());
    }
  }

  if (_root_node_nameSet.count(elementName)) {
    if (rename_prim_name) {
      std::string unique_name;
      if (!makeUniqueName(_root_node_nameSet, elementName, &unique_name)) {
        PUSH_ERROR_AND_RETURN(fmt::format("Internal error. cannot assign unique name for `{}`.\n", elementName));
      }

      elementName = unique_name;

      // Need to modify both Prim::data::name and Prim::elementPath
      if (!SetPrimElementName(prim.get_data(), elementName)) {
        PUSH_ERROR_AND_RETURN("Internal error. cannot modify Prim's elementName.");
      }
      prim.element_path() = Path(elementName, /* prop_part */"");
    } else {
      PUSH_ERROR_AND_RETURN(fmt::format("Prim name(elementName) {} already exists in children.\n", prim.element_name()));
    }
  }


  _root_node_nameSet.insert(elementName);
  _root_nodes.emplace_back(std::move(prim));

  _dirty = true;

  return true;


}

bool Stage::replace_root_prim(const std::string &prim_name, Prim &&prim) {

#if defined(TINYUSD_ENABLE_THREAD)
  // TODO: Only take a lock when dirty.
  std::lock_guard<std::mutex> lock(_mutex);
#endif

  if (prim_name.empty()) {
    PUSH_ERROR_AND_RETURN(fmt::format("prim_name is empty."));
  }

  if (!ValidatePrimElementName(prim_name)) {
    PUSH_ERROR_AND_RETURN(fmt::format("`{}` is not a valid Prim name.", prim_name));
  }

  if (_root_nodes.size() != _root_node_nameSet.size()) {
    // Rebuild nameSet
    _root_node_nameSet.clear();
    for (size_t i = 0; i < _root_nodes.size(); i++) {
      if (_root_nodes[i].element_name().empty()) {
        PUSH_ERROR_AND_RETURN("Internal error: Existing root Prim's elementName is empty.");
      }

      if (_root_node_nameSet.count(_root_nodes[i].element_name())) {
        PUSH_ERROR_AND_RETURN("Internal error: Stage contains root Prim with same elementName.");
      }

      _root_node_nameSet.insert(_root_nodes[i].element_name());
    }
  }

  // Simple linear scan
  auto result = std::find_if(_root_nodes.begin(), _root_nodes.end(), [prim_name](const Prim &p) {
    return (p.element_name() == prim_name);
  });

  if (result != _root_nodes.end()) {

    // Need to modify both Prim::data::name and Prim::elementPath
    if (!SetPrimElementName(prim.get_data(), prim_name)) {
      PUSH_ERROR_AND_RETURN("Internal error. cannot modify Prim's elementName.");
    }
    prim.element_path() = Path(prim_name, /* prop_part */"");

    (*result) = std::move(prim); // replace

  } else {

    // Need to modify both Prim::data::name and Prim::elementPath
    if (!SetPrimElementName(prim.get_data(), prim_name)) {
      PUSH_ERROR_AND_RETURN("Internal error. cannot modify Prim's elementName.");
    }
    prim.element_path() = Path(prim_name, /* prop_part */"");

    _root_node_nameSet.insert(prim_name);
    _root_nodes.emplace_back(std::move(prim)); // add
  }

  _dirty = true;

  return true;
}

namespace {

std::string DumpPrimTreeRec(const Prim &prim, uint32_t depth) {
  std::stringstream ss;

  if (depth > 1024 * 1024 * 128) {
    // too deep node.
    return ss.str();
  }

  ss << pprint::Indent(depth) << "\"" << prim.element_name() << "\" "
     << prim.absolute_path() << "\n";
  ss << pprint::Indent(depth + 1) << fmt::format("prim_id {}", prim.prim_id())
     << "\n";

  for (const Prim &child : prim.children()) {
    ss << DumpPrimTreeRec(child, depth + 1);
  }

  return ss.str();
}

}  // namespace

std::string Stage::dump_prim_tree() const {
  std::stringstream ss;

  for (const Prim &root : root_prims()) {
    ss << DumpPrimTreeRec(root, 0);
  }
  return ss.str();
}

}  // namespace tinyusdz
