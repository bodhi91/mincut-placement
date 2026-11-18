// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026, The OpenROAD Authors

#include "KwayRefine.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <functional>
#include <limits>
#include <mutex>
#include <unordered_map>

#include "NetlistModel.h"
#include "PlacementArea.h"
#include "Region.h"
#include "RegionTree.h"
#include "utl/Logger.h"

#ifdef MCP_HAVE_MTKAHYPAR
#include "mtkahypar.h"
#endif

namespace mcp {

KwayRefiner::KwayRefiner(const NetlistModel& netlist,
                         const PlacementArea& area,
                         utl::Logger* logger)
    : netlist_(netlist), area_(area), logger_(logger)
{
}

bool KwayRefiner::available()
{
#ifdef MCP_HAVE_MTKAHYPAR
  return true;
#else
  return false;
#endif
}

#ifdef MCP_HAVE_MTKAHYPAR

namespace {

// Boxes sharing a boundary segment of positive length.
bool adjacentBoxes(const odb::Rect& a, const odb::Rect& b)
{
  if (a.xMax() == b.xMin() || b.xMax() == a.xMin()) {
    return std::min(a.yMax(), b.yMax()) - std::max(a.yMin(), b.yMin()) > 0;
  }
  if (a.yMax() == b.yMin() || b.yMax() == a.yMin()) {
    return std::min(a.xMax(), b.xMax()) - std::max(a.xMin(), b.xMin()) > 0;
  }
  return false;
}

int64_t boxDistance(const odb::Rect& box, const odb::Point& p)
{
  const int64_t dx = std::max<int64_t>(
      {box.xMin() - p.x(), 0, static_cast<int64_t>(p.x()) - box.xMax()});
  const int64_t dy = std::max<int64_t>(
      {box.yMin() - p.y(), 0, static_cast<int64_t>(p.y()) - box.yMax()});
  return dx + dy;
}

int64_t centerDistance(const odb::Rect& box, const odb::Point& p)
{
  const odb::Point c = box.center();
  return std::abs(static_cast<int64_t>(c.x()) - p.x())
         + std::abs(static_cast<int64_t>(c.y()) - p.y());
}

void initializeOnce(int threads)
{
  static std::once_flag flag;
  std::call_once(flag, [threads]() {
    mt_kahypar_initialize(static_cast<size_t>(std::max(1, threads)), false);
  });
}

}  // namespace

bool KwayRefiner::refine(const std::vector<int>& blocks,
                         const RegionTree& tree,
                         const std::vector<int>& region_of,
                         const std::vector<odb::Point>& loc,
                         double slack,
                         int vcycles,
                         int threads,
                         unsigned seed,
                         bool from_scratch,
                         std::vector<int>& new_region_of,
                         KwayRefineStats& stats) const
{
  const auto start = std::chrono::steady_clock::now();
  const int k = static_cast<int>(blocks.size());
  stats.k = k;
  // Mt-KaHyPar's process mapping precomputes Steiner trees over the
  // target graph; it accepts at most 64 nodes.
  if (k < 2 || k > 64) {
    debugPrint(logger_,
               utl::MCP,
               "kway",
               1,
               "k-way refinement skipped: k = {} outside [2, 64]",
               k);
    return false;
  }
  std::unordered_map<int, int> index;
  index.reserve(blocks.size());
  for (int b = 0; b < k; ++b) {
    index[blocks[b]] = b;
  }

  // Vertex weights in site units; every vertex that cannot move (pins,
  // fixed cells, cells of leaves) is fixed to the nearest block with
  // weight 1 -- Mt-KaHyPar ignores the fixed-vertex assignment of
  // zero-weight vertices (X0).
  const int n = netlist_.numVertices();
  const double unit = static_cast<double>(area_.siteWidth())
                      * static_cast<double>(area_.rowHeight());
  std::vector<mt_kahypar_hypernode_weight_t> vertex_weight(n, 1);
  std::vector<mt_kahypar_partition_id_t> fixed(n, -1);
  std::vector<mt_kahypar_partition_id_t> part(n, 0);
  std::vector<int64_t> load(k, 0);
  std::vector<int64_t> fixed_weight(k, 0);
  for (int v = 0; v < n; ++v) {
    const int r = region_of[v];
    const auto it = r >= 0 ? index.find(r) : index.end();
    if (it != index.end()) {
      const int64_t w = std::max<int64_t>(
          1, std::llround(static_cast<double>(netlist_.vertex(v).area) / unit));
      vertex_weight[v] = static_cast<mt_kahypar_hypernode_weight_t>(
          std::min<int64_t>(w, std::numeric_limits<int>::max()));
      part[v] = it->second;
      load[it->second] += w;
      continue;
    }
    int best = 0;
    int64_t best_box = std::numeric_limits<int64_t>::max();
    int64_t best_center = best_box;
    for (int b = 0; b < k; ++b) {
      const odb::Rect& box = tree.region(blocks[b]).box;
      const int64_t d_box = boxDistance(box, loc[v]);
      const int64_t d_center = centerDistance(box, loc[v]);
      if (d_box < best_box || (d_box == best_box && d_center < best_center)) {
        best = b;
        best_box = d_box;
        best_center = d_center;
      }
    }
    fixed[v] = best;
    part[v] = best;
    fixed_weight[best] += 1;
  }

  // Hyperedges: every net with at least two distinct vertices.
  std::vector<size_t> he_index{0};
  std::vector<mt_kahypar_hypernode_id_t> pins;
  std::vector<mt_kahypar_hyperedge_weight_t> he_weight;
  std::vector<int> scratch;
  for (int e = 0; e < netlist_.numNets(); ++e) {
    const auto verts = netlist_.netVertices(e);
    if (verts.size() < 2) {
      continue;
    }
    scratch.assign(verts.begin(), verts.end());
    std::sort(scratch.begin(), scratch.end());
    scratch.erase(std::unique(scratch.begin(), scratch.end()), scratch.end());
    if (scratch.size() < 2) {
      continue;
    }
    for (int v : scratch) {
      pins.push_back(static_cast<mt_kahypar_hypernode_id_t>(v));
    }
    he_index.push_back(pins.size());
    he_weight.push_back(static_cast<mt_kahypar_hyperedge_weight_t>(
        std::max(1L, std::lround(netlist_.netWeight(e)))));
  }
  const size_t num_he = he_weight.size();
  if (num_he == 0) {
    return false;
  }

  // Target graph: adjacent regions connected with their center distance
  // in sites; a region without neighbors (should not happen on a cut
  // tree) connected to its nearest.
  std::vector<mt_kahypar_hypernode_id_t> edges;
  std::vector<mt_kahypar_hyperedge_weight_t> edge_weight;
  std::vector<int> degree(k, 0);
  auto edge_cost = [&](int a, int b) {
    const odb::Point ca = tree.region(blocks[a]).box.center();
    const odb::Point cb = tree.region(blocks[b]).box.center();
    const int64_t d = std::abs(static_cast<int64_t>(ca.x()) - cb.x())
                      + std::abs(static_cast<int64_t>(ca.y()) - cb.y());
    return static_cast<mt_kahypar_hyperedge_weight_t>(
        std::max<int64_t>(1, d / std::max(1, area_.siteWidth())));
  };
  for (int a = 0; a < k; ++a) {
    for (int b = a + 1; b < k; ++b) {
      if (adjacentBoxes(tree.region(blocks[a]).box,
                        tree.region(blocks[b]).box)) {
        edges.push_back(a);
        edges.push_back(b);
        edge_weight.push_back(edge_cost(a, b));
        ++degree[a];
        ++degree[b];
      }
    }
  }
  // Mt-KaHyPar requires a connected target graph.  The level's regions
  // need not touch each other (a level whose siblings became end cases
  // leaves gaps: aes_nangate45 level 3), so join the components by
  // their cheapest connecting edge until one is left (Kruskal over the
  // component pairs).
  {
    std::vector<int> comp(k);
    for (int a = 0; a < k; ++a) {
      comp[a] = a;
    }
    std::function<int(int)> find = [&](int a) {
      return comp[a] == a ? a : comp[a] = find(comp[a]);
    };
    for (size_t i = 0; i + 1 < edges.size(); i += 2) {
      comp[find(static_cast<int>(edges[i]))]
          = find(static_cast<int>(edges[i + 1]));
    }
    for (;;) {
      int best_a = -1, best_b = -1;
      mt_kahypar_hyperedge_weight_t best_cost = 0;
      for (int a = 0; a < k; ++a) {
        for (int b = a + 1; b < k; ++b) {
          if (find(a) == find(b)) {
            continue;
          }
          const mt_kahypar_hyperedge_weight_t c = edge_cost(a, b);
          if (best_a < 0 || c < best_cost) {
            best_a = a;
            best_b = b;
            best_cost = c;
          }
        }
      }
      if (best_a < 0) {
        break;
      }
      edges.push_back(best_a);
      edges.push_back(best_b);
      edge_weight.push_back(best_cost);
      ++degree[best_a];
      ++degree[best_b];
      comp[find(best_a)] = find(best_b);
    }
  }

  // Block budgets: the tree's cell area times the slack, plus the fixed
  // weight the block received.
  std::vector<mt_kahypar_hypernode_weight_t> budget(k);
  for (int b = 0; b < k; ++b) {
    const int64_t w = static_cast<int64_t>(std::ceil(load[b] * slack))
                      + fixed_weight[b];
    budget[b] = static_cast<mt_kahypar_hypernode_weight_t>(
        std::min<int64_t>(std::max<int64_t>(w, 1),
                          std::numeric_limits<int>::max()));
  }

  initializeOnce(threads);
  mt_kahypar_error_t error{};
  auto fail = [&](const char* what) {
    logger_->warn(utl::MCP,
                  55,
                  "k-way refinement: {} failed: {}",
                  what,
                  error.msg != nullptr ? error.msg : "unknown error");
    mt_kahypar_free_error_content(&error);
    return false;
  };

  mt_kahypar_context_t* ctx = mt_kahypar_context_from_preset(DEFAULT);
  mt_kahypar_set_partitioning_parameters(ctx, k, 0.03, KM1);
  mt_kahypar_set_seed(seed);
  mt_kahypar_set_individual_target_block_weights(ctx, k, budget.data());

  mt_kahypar_hypergraph_t hg = mt_kahypar_create_hypergraph(
      ctx,
      static_cast<mt_kahypar_hypernode_id_t>(n),
      static_cast<mt_kahypar_hyperedge_id_t>(num_he),
      he_index.data(),
      pins.data(),
      he_weight.data(),
      vertex_weight.data(),
      &error);
  if (hg.hypergraph == nullptr) {
    mt_kahypar_free_context(ctx);
    return fail("hypergraph construction");
  }
  if (mt_kahypar_add_fixed_vertices(hg, fixed.data(), k, &error) != SUCCESS) {
    mt_kahypar_free_hypergraph(hg);
    mt_kahypar_free_context(ctx);
    return fail("fixed vertices");
  }
  mt_kahypar_target_graph_t* tg = mt_kahypar_create_target_graph(
      ctx,
      static_cast<mt_kahypar_hypernode_id_t>(k),
      static_cast<mt_kahypar_hyperedge_id_t>(edge_weight.size()),
      edges.data(),
      edge_weight.data(),
      &error);
  if (tg == nullptr) {
    mt_kahypar_free_hypergraph(hg);
    mt_kahypar_free_context(ctx);
    return fail("target graph");
  }
  // The tree's assignment, for its Steiner cost (and as the start of the
  // V-cycles when refining).
  mt_kahypar_partitioned_hypergraph_t phg
      = mt_kahypar_create_partitioned_hypergraph(hg, ctx, k, part.data(),
                                                 &error);
  if (phg.partitioned_hg == nullptr) {
    mt_kahypar_free_target_graph(tg);
    mt_kahypar_free_hypergraph(hg);
    mt_kahypar_free_context(ctx);
    return fail("partitioned hypergraph");
  }
  stats.steiner_before = mt_kahypar_steiner_tree(phg, tg);
  bool ok = false;
  if (from_scratch) {
    // X1a: multilevel process mapping from scratch into the same blocks,
    // budgets and fixed vertices.
    mt_kahypar_free_partitioned_hypergraph(phg);
    phg = mt_kahypar_map(hg, tg, ctx, &error);
    ok = phg.partitioned_hg != nullptr;
  } else {
    ok = mt_kahypar_improve_mapping(
             phg, tg, ctx, static_cast<size_t>(std::max(1, vcycles)), &error)
         == SUCCESS;
  }
  if (ok) {
    stats.steiner_after = mt_kahypar_steiner_tree(phg, tg);
    mt_kahypar_get_partition(phg, part.data());
    mt_kahypar_free_partitioned_hypergraph(phg);
  }
  mt_kahypar_free_target_graph(tg);
  mt_kahypar_free_hypergraph(hg);
  mt_kahypar_free_context(ctx);
  if (!ok) {
    return fail(from_scratch ? "mapping" : "mapping refinement");
  }

  // Result: the refined block of every cell of the level; fixed vertices
  // are verified to have stayed (they must, with weight 1).
  new_region_of = region_of;
  int moved_fixed = 0;
  stats.moved = 0;
  for (int v = 0; v < n; ++v) {
    if (fixed[v] >= 0) {
      moved_fixed += part[v] != fixed[v] ? 1 : 0;
      continue;
    }
    const int r = blocks[part[v]];
    if (r != region_of[v]) {
      ++stats.moved;
    }
    new_region_of[v] = r;
  }
  if (moved_fixed > 0) {
    logger_->warn(utl::MCP,
                  56,
                  "k-way refinement moved {} fixed vertices; result "
                  "discarded.",
                  moved_fixed);
    return false;
  }
  stats.ran = true;
  stats.seconds = std::chrono::duration<double>(
                      std::chrono::steady_clock::now() - start)
                      .count();
  return true;
}

#else  // !MCP_HAVE_MTKAHYPAR

bool KwayRefiner::refine(const std::vector<int>& /* blocks */,
                         const RegionTree& /* tree */,
                         const std::vector<int>& /* region_of */,
                         const std::vector<odb::Point>& /* loc */,
                         double /* slack */,
                         int /* vcycles */,
                         int /* threads */,
                         unsigned /* seed */,
                         bool /* from_scratch */,
                         std::vector<int>& /* new_region_of */,
                         KwayRefineStats& /* stats */) const
{
  logger_->warn(utl::MCP,
                57,
                "k-way refinement requested, but this build has no "
                "Mt-KaHyPar (configure with -DMCP_MTKAHYPAR_DIR).");
  return false;
}

#endif

}  // namespace mcp
