// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026, The OpenROAD Authors

#include "NetlistModel.h"

#include <algorithm>
#include <cstdint>
#include <unordered_map>
#include <utility>
#include <vector>

#include "odb/db.h"
#include "odb/dbTypes.h"
#include "odb/geom.h"
#include "utl/Logger.h"

namespace mcp {

NetlistModel::NetlistModel(odb::dbBlock* block,
                           int max_fanout,
                           int padding,
                           utl::Logger* logger)
    : logger_(logger)
{
  buildVertices(block, padding);
  buildNets(block, max_fanout);
  buildIncidence();
}

int NetlistModel::vertexOf(odb::dbInst* inst) const
{
  const uint32_t id = inst->getId();
  if (id >= inst_to_vertex_.size()) {
    return -1;
  }
  return inst_to_vertex_[id];
}

void NetlistModel::buildVertices(odb::dbBlock* block, int padding)
{
  odb::dbSet<odb::dbInst> insts = block->getInsts();
  uint32_t max_id = 0;
  for (odb::dbInst* inst : insts) {
    max_id = std::max(max_id, inst->getId());
  }
  inst_to_vertex_.assign(max_id + 1, -1);
  vertices_.reserve(insts.size() + block->getBTerms().size());

  for (odb::dbInst* inst : insts) {
    odb::dbMaster* master = inst->getMaster();
    const bool placed = inst->isPlaced();

    Vertex v;
    v.inst = inst;
    v.width = master->getWidth();
    v.height = master->getHeight();
    v.area = static_cast<int64_t>(v.width) * static_cast<int64_t>(v.height);

    // Anything the placer may not move is a fixed vertex: FIRM/LOCKED
    // instances and placed macros.  Unplaced non-core instances cannot
    // be handled and are counted for the caller to report.
    if (inst->isFixed() || (master->isBlock() && placed)) {
      v.fixed = true;
    } else if (master->isCore()) {
      v.fixed = false;
      v.width += padding;
      v.area = static_cast<int64_t>(v.width) * static_cast<int64_t>(v.height);
    } else {
      ++stats_.unplaced_non_core;
      continue;
    }

    if (placed) {
      v.loc = inst->getBBox()->getBox().center();
      v.placed = true;
    }

    inst_to_vertex_[inst->getId()] = static_cast<int>(vertices_.size());
    if (v.fixed) {
      ++stats_.fixed_insts;
    } else {
      movable_.push_back(static_cast<int>(vertices_.size()));
      ++stats_.movable;
      stats_.movable_area += v.area;
    }
    vertices_.push_back(v);
  }

  for (odb::dbBTerm* bterm : block->getBTerms()) {
    int x = 0;
    int y = 0;
    if (!bterm->getFirstPinLocation(x, y)) {
      continue;  // unplaced pin: no geometry to contribute
    }
    Vertex v;
    v.bterm = bterm;
    v.fixed = true;
    v.loc = odb::Point(x, y);
    bterm_to_vertex_.emplace(bterm, static_cast<int>(vertices_.size()));
    vertices_.push_back(v);
    ++stats_.io_pins;
  }
}

void NetlistModel::buildNets(odb::dbBlock* block, int max_fanout)
{
  net_ptr_.push_back(0);

  // Per-net scratch: vertex -> (sum of offsets, count) to merge pins of
  // the same vertex.
  struct PinAcc
  {
    int vertex;
    int64_t sum_x;
    int64_t sum_y;
    int count;
  };
  std::vector<PinAcc> acc;
  std::unordered_map<int, int> vertex_slot;

  for (odb::dbNet* net : block->getNets()) {
    if (net->isSpecial() || net->getSigType().isSupply()) {
      ++stats_.nets_skipped_special;
      continue;
    }

    acc.clear();
    vertex_slot.clear();
    int num_pins = 0;

    auto add_pin = [&](int v, odb::Point offset) {
      ++num_pins;
      auto it = vertex_slot.find(v);
      if (it == vertex_slot.end()) {
        vertex_slot.emplace(v, static_cast<int>(acc.size()));
        acc.push_back({v, offset.x(), offset.y(), 1});
      } else {
        PinAcc& a = acc[it->second];
        a.sum_x += offset.x();
        a.sum_y += offset.y();
        ++a.count;
      }
    };

    for (odb::dbITerm* iterm : net->getITerms()) {
      odb::dbInst* inst = iterm->getInst();
      const int v = vertexOf(inst);
      if (v < 0) {
        continue;
      }
      odb::Point offset;
      if (inst->isPlaced() && vertices_[v].fixed) {
        // A fixed instance keeps its orientation: the offset of the pin
        // as placed.
        const odb::Point pin = iterm->getBBox().center();
        offset = odb::Point(pin.x() - vertices_[v].loc.x(),
                            pin.y() - vertices_[v].loc.y());
      } else {
        // Master coordinates, R0: offset from the master center.  The
        // placer orients movable cells itself (the end case and the
        // ironing apply the orientation to this offset), so a movable
        // cell's offset must not depend on the orientation it arrives
        // in, e.g. flipped by a previous placement.
        odb::dbMaster* master = inst->getMaster();
        const odb::Point pin = iterm->getMTerm()->getBBox().center();
        offset
            = odb::Point(pin.x() - static_cast<int>(master->getWidth()) / 2,
                         pin.y() - static_cast<int>(master->getHeight()) / 2);
      }
      add_pin(v, offset);
    }
    for (odb::dbBTerm* bterm : net->getBTerms()) {
      auto it = bterm_to_vertex_.find(bterm);
      if (it != bterm_to_vertex_.end()) {
        add_pin(it->second, odb::Point(0, 0));
      }
    }

    if (acc.size() < 2) {
      ++stats_.nets_skipped_degree;
      continue;
    }
    if (max_fanout > 0 && num_pins > max_fanout) {
      ++stats_.nets_skipped_fanout;
      continue;
    }

    // Movable vertices first.
    std::stable_partition(acc.begin(), acc.end(), [&](const PinAcc& a) {
      return !vertices_[a.vertex].fixed;
    });
    int movable = 0;
    for (const PinAcc& a : acc) {
      if (!vertices_[a.vertex].fixed) {
        ++movable;
      }
    }
    if (movable == 0) {
      ++stats_.nets_skipped_no_movable;
      continue;
    }

    for (const PinAcc& a : acc) {
      net_vertices_.push_back(a.vertex);
      pin_offsets_.emplace_back(static_cast<int>(a.sum_x / a.count),
                                static_cast<int>(a.sum_y / a.count));
      vertices_[a.vertex].num_pins += a.count;
    }
    net_ptr_.push_back(static_cast<int>(net_vertices_.size()));
    net_movable_.push_back(movable);
    net_weights_.push_back(1.0f);
    nets_.push_back(net);
    ++stats_.nets_kept;
  }
}

void NetlistModel::buildIncidence()
{
  const int nv = numVertices();
  vertex_ptr_.assign(nv + 1, 0);
  for (int v : net_vertices_) {
    ++vertex_ptr_[v + 1];
  }
  for (int v = 0; v < nv; ++v) {
    vertex_ptr_[v + 1] += vertex_ptr_[v];
  }
  vertex_nets_.resize(net_vertices_.size());
  vertex_pin_offsets_.resize(net_vertices_.size());
  std::vector<int> fill(vertex_ptr_.begin(), vertex_ptr_.end() - 1);
  for (int e = 0; e < numNets(); ++e) {
    const auto verts = netVertices(e);
    const auto offsets = netPinOffsets(e);
    for (size_t j = 0; j < verts.size(); ++j) {
      const int v = verts[j];
      vertex_pin_offsets_[fill[v]] = offsets[j];
      vertex_nets_[fill[v]++] = e;
    }
  }
}

}  // namespace mcp
