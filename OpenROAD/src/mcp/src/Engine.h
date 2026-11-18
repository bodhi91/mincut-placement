// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026, The OpenROAD Authors

// Partitioning engines.  A Subproblem is one region's hypergraph in
// local vertex ids: the region's movable cells first, then up to k
// terminal vertices (one per child) that carry the propagated external
// connections.  Weights are in whole sites so that every engine
// (TritonPart, hMETIS, Mt-KaHyPar, FastPart) sees the same integer-valued
// instance.

#pragma once

#include <memory>
#include <string>
#include <vector>

#include "mcp/MinCutPlacer.h"
#include "par/NetVectors.h"

namespace par {
class PartitionMgr;
}

namespace utl {
class Logger;
}

namespace mcp {

struct Subproblem
{
  int k = 2;
  int num_movable = 0;
  // Vertex weights (sites); size num_movable + number of terminals.
  std::vector<float> vertex_weights;
  // -1 for movable vertices, child index for terminals.
  std::vector<int> fixed_part;
  std::vector<std::vector<int>> hyperedges;
  std::vector<float> hyperedge_weights;
  // Per-child bounds on the assigned movable weight (sites).
  std::vector<float> upper;
  std::vector<float> lower;
  // Geometric objective (null for the cut-net objective): per-hyperedge
  // cost for every occupancy mask of the children.  Only the TritonPart
  // engines understand it.
  std::shared_ptr<const par::NetVectors> vectors;
  // Optional initial partition of the movable vertices (num_movable
  // entries) that the engine refines instead of partitioning from
  // scratch.  Only the TritonPart engines understand it.
  std::vector<int> initial_parts;
  unsigned seed = 0;
  int threads = 1;
  // Identifies the region in log and scratch file names.
  int region_id = -1;
  // Level of the region in the tree (root 0); budgeted engines scale
  // their effort with it.
  int level = 0;
};

class Engine
{
 public:
  virtual ~Engine() = default;
  virtual std::string name() const = 0;
  // Whether the engine can optimize Subproblem::vectors.
  virtual bool supportsVectors() const { return false; }
  // Whether the engine can refine Subproblem::initial_parts.
  virtual bool supportsInitialParts() const { return false; }
  // Fills parts[i] for i < num_movable with the child of movable vertex
  // i.  Returns false when the engine failed; the caller falls back.
  virtual bool partition(const Subproblem& sub, std::vector<int>& parts) = 0;
};

// TritonPart in two configurations: multilevel (kTritonPart) or flat
// initial partitioning plus k-way FM (kFm).
class TritonPartEngine : public Engine
{
 public:
  // max_moves / refiner_iters: see Options.
  TritonPartEngine(par::PartitionMgr* mgr,
                   bool multilevel,
                   int max_moves,
                   int refiner_iters);
  std::string name() const override;
  bool supportsVectors() const override { return true; }
  bool supportsInitialParts() const override { return true; }
  bool partition(const Subproblem& sub, std::vector<int>& parts) override;

 private:
  par::PartitionMgr* mgr_;
  bool multilevel_;
  int max_moves_;
  int refiner_iters_;
};

// External partitioner driven through hMETIS-format files in a scratch
// directory.  hMETIS 1.5 through the tools/hmetis/hmetis_driver wrapper
// (HGraphFile FixFile Nparts UBfactor Seed) and Mt-KaHyPar (--fixed,
// --part-weights) are supported.  Only the upper bounds are honored;
// hMETIS receives them as a symmetric UBfactor.
class ExternalEngine : public Engine
{
 public:
  ExternalEngine(EngineKind kind,
                 const std::string& binary,
                 const std::string& scratch_dir,
                 utl::Logger* logger);
  std::string name() const override;
  bool partition(const Subproblem& sub, std::vector<int>& parts) override;

 protected:
  bool writeHypergraph(const Subproblem& sub,
                       const std::string& hgr,
                       const std::string& fix) const;
  bool readPartition(const std::string& file,
                     int num_vertices,
                     std::vector<int>& parts) const;
  // Scratch directory for one subproblem ("" when it cannot be made).
  std::string makeScratch(const Subproblem& sub) const;
  std::string hmetisCommand(const Subproblem& sub,
                            const std::string& hgr,
                            const std::string& fix,
                            const std::string& log) const;
  std::string mtkahyparCommand(const Subproblem& sub,
                               const std::string& dir,
                               const std::string& hgr,
                               const std::string& fix,
                               const std::string& log) const;

  EngineKind kind_;
  std::string binary_;
  std::string scratch_dir_;
  utl::Logger* logger_;
};

// FastPart (the fixed-budget portfolio controller over Mt-KaHyPar)
// through tools/fastpart/fastpart_driver.py, which re-expresses the
// controller for one subproblem: fixed terminals, the per-child bounds
// (both sides), the placer's seed assignment as a warm start, and the
// lanes the placer grants the region (Subproblem::threads).  The driver
// runs as one server process per placement (started here, Unix socket
// in the scratch directory, Options::threads persistent engine workers)
// so that a region costs its budget and not the engine start-up; a
// region's budget is time * decay^level, at least min_time (seconds).
// Regions with at most min_cells movable cells (the exact end game
// solves them anyway) go to a TritonPart delegate.  The partition is
// the best feasible member of the portfolio's pool; when no run is
// feasible the least-violating one is used (as the other external
// engines accept whatever balance their binary produced).
class FastPartEngine : public ExternalEngine
{
 public:
  FastPartEngine(const std::string& driver,
                 const Options& options,
                 par::PartitionMgr* mgr,
                 utl::Logger* logger);
  ~FastPartEngine() override;
  std::string name() const override { return "fastpart"; }
  bool supportsInitialParts() const override { return true; }
  bool partition(const Subproblem& sub, std::vector<int>& parts) override;

 private:
  bool startServer();
  void stopServer();
  // One request/reply exchange with the server; false when the server
  // is unreachable or the reply is not "ok".
  bool request(const std::string& json, std::string& reply) const;

  double time_budget_;
  double decay_;
  double min_time_;
  int min_cells_;
  int stall_;
  int lanes_;
  std::string socket_path_;
  std::string server_log_;
  bool server_up_ = false;
  std::unique_ptr<Engine> delegate_;
};

// Engine for the requested kind, or null (with an error logged) when
// the kind is unavailable.
std::unique_ptr<Engine> makeEngine(EngineKind kind,
                                   const Options& options,
                                   par::PartitionMgr* mgr,
                                   utl::Logger* logger);

// Sanity check of an engine result against the subproblem: every
// movable vertex has a part in [0, k).  Returns the number of violations.
int checkParts(const Subproblem& sub, const std::vector<int>& parts);

}  // namespace mcp
