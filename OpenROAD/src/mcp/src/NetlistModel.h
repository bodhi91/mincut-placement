// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026, The OpenROAD Authors

// Netlist abstraction used by the placer: a hypergraph whose vertices
// are instances (movable or fixed) and I/O pins, and whose hyperedges
// are signal nets.  Built once from odb; regions refer to vertices by
// index.  Geometry is kept in dbu.

#pragma once

#include <cstdint>
#include <span>
#include <string>
#include <unordered_map>
#include <vector>

#include "odb/geom.h"

namespace odb {
class dbBlock;
class dbInst;
class dbBTerm;
class dbNet;
}  // namespace odb

namespace utl {
class Logger;
}

namespace mcp {

struct Vertex
{
  odb::dbInst* inst = nullptr;    // null for I/O pins
  odb::dbBTerm* bterm = nullptr;  // set for I/O pins
  // Footprint used for capacities: master width plus the placement
  // padding (dbu); 0 for pins.  The end case places the master itself.
  int64_t area = 0;  // width * height (dbu^2)
  int width = 0;
  int height = 0;
  int num_pins = 0;  // connected signal pins
  bool fixed = false;
  bool placed = false;  // instance had a location when the model was built
  // Center of the vertex.  Meaningful for fixed vertices and for movable
  // instances that are already placed (seed placement); otherwise (0,0).
  odb::Point loc;
};

struct NetlistStats
{
  int movable = 0;
  int fixed_insts = 0;
  int io_pins = 0;
  int nets_kept = 0;
  int nets_skipped_special = 0;
  int nets_skipped_degree = 0;  // fewer than two distinct vertices
  int nets_skipped_fanout = 0;
  int nets_skipped_no_movable = 0;
  int64_t movable_area = 0;
  int unplaced_non_core = 0;  // reported as an error by the caller
};

class NetlistModel
{
 public:
  // max_fanout <= 0 means no limit.  padding (dbu) is added to the width
  // of every movable instance for the capacity accounting.
  NetlistModel(odb::dbBlock* block,
               int max_fanout,
               int padding,
               utl::Logger* logger);

  int numVertices() const { return static_cast<int>(vertices_.size()); }
  int numNets() const { return static_cast<int>(net_ptr_.size()) - 1; }
  const Vertex& vertex(int v) const { return vertices_[v]; }
  Vertex& vertex(int v) { return vertices_[v]; }

  // Distinct vertices of net e, movable ones first.
  std::span<const int> netVertices(int e) const
  {
    return {net_vertices_.data() + net_ptr_[e],
            net_vertices_.data() + net_ptr_[e + 1]};
  }
  // Pin offsets from the vertex center, parallel to netVertices(e).  For
  // a vertex that has several pins on the net the offset is their mean.
  std::span<const odb::Point> netPinOffsets(int e) const
  {
    return {pin_offsets_.data() + net_ptr_[e],
            pin_offsets_.data() + net_ptr_[e + 1]};
  }
  int netMovableCount(int e) const { return net_movable_[e]; }
  float netWeight(int e) const { return net_weights_[e]; }
  void setNetWeight(int e, float w) { net_weights_[e] = w; }
  odb::dbNet* net(int e) const { return nets_[e]; }

  // Nets incident to vertex v.
  std::span<const int> vertexNets(int v) const
  {
    return {vertex_nets_.data() + vertex_ptr_[v],
            vertex_nets_.data() + vertex_ptr_[v + 1]};
  }
  // Offsets of the vertex's pins on those nets, parallel to vertexNets(v).
  std::span<const odb::Point> vertexPinOffsets(int v) const
  {
    return {vertex_pin_offsets_.data() + vertex_ptr_[v],
            vertex_pin_offsets_.data() + vertex_ptr_[v + 1]};
  }

  // Vertex of an instance, or -1.
  int vertexOf(odb::dbInst* inst) const;

  const NetlistStats& stats() const { return stats_; }
  const std::vector<int>& movableVertices() const { return movable_; }

 private:
  void buildVertices(odb::dbBlock* block, int padding);
  void buildNets(odb::dbBlock* block, int max_fanout);
  void buildIncidence();

  utl::Logger* logger_;
  std::vector<Vertex> vertices_;
  std::vector<int> movable_;
  std::vector<int> inst_to_vertex_;  // indexed by dbInst id
  std::unordered_map<odb::dbBTerm*, int> bterm_to_vertex_;

  // Hyperedges in CSR form.
  std::vector<odb::dbNet*> nets_;
  std::vector<int> net_ptr_;
  std::vector<int> net_vertices_;
  std::vector<odb::Point> pin_offsets_;
  std::vector<int> net_movable_;
  std::vector<float> net_weights_;

  // Vertex -> nets in CSR form.
  std::vector<int> vertex_ptr_;
  std::vector<int> vertex_nets_;
  std::vector<odb::Point> vertex_pin_offsets_;

  NetlistStats stats_;
};

}  // namespace mcp
