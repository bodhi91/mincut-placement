// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026, The OpenROAD Authors

// Top-down (min-cut) global placer.
//
// The design is recursively decomposed into regions (bins).  At every
// level the cells of a region are partitioned into k children with a
// hypergraph partitioner, then the children become the regions of the
// next level.  Recursion stops at small regions ("end case"), where cells
// are written back to the database with PLACED status; detailed
// placement (dpl) legalizes the result.

#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "odb/dbTypes.h"
#include "odb/geom.h"

namespace odb {
class dbDatabase;
class dbBlock;
class dbInst;
}  // namespace odb

namespace utl {
class Logger;
}

namespace par {
class PartitionMgr;
}

namespace mcp {

class RegionTree;
class NetlistModel;
class PlacementArea;

class Observer;
struct RunReport;
struct NetlistStats;

// Hypergraph partitioning engine used to split a region.
enum class EngineKind
{
  kFm,          // TritonPart with coarsening disabled (flat FM)
  kTritonPart,  // TritonPart multilevel (default)
  kHmetis,      // external hMETIS binary via files
  kMtKaHyPar,   // external Mt-KaHyPar via files
  kFastPart     // external FastPart via files
};

// How pins outside a region influence its partitioning.
enum class TerminalMode
{
  kNone,       // ignore external pins
  kPropagate,  // fixed vertex pinned to the nearest child (M1)
};

// Net cost model.  kCutNet works with every engine (external pins become
// terminal vertices); the others are net vectors over the children's
// occupancy masks (par::NetVectors) and need the fm or tritonpart engine.
enum class Objective
{
  kCutNet,  // binary: a net is cut or not (TritonPart's native metric)
  kConn,    // connectivity - 1, external pins at their nearest child
  kHpwl,    // HPWL over occupied child centers and external pins
  kPhpwl,   // probabilistic HPWL: the net's local pins uniform over the
            // occupied children (expected bounding box), external pins
            // at their locations; degree-aware, an uncut net is not free
  kRsmt     // RSMT (FLUTE) over the same points, HPWL above 9 points
};

// Weight of a net in the partitioning objective as a function of its
// pin count d (E24).  kUniform is the classic cut.  kExpHpwl is the
// expected HPWL gain of un-cutting the net under a uniform prior on its
// pins (normalized to 1 for d = 2; rises to 1.5 for large d: protects
// high-fanout nets).  kInvDegree is the clique weight 1 / (d - 1) (lets
// high-fanout nets be cut freely).  The two laws bracket the two
// readings of the k-row losses (E17: the HPWL of high-fanout nets cut
// at the top levels).
enum class NetWeightRule
{
  kUniform,
  kExpHpwl,
  kInvDegree
};

// How the balance of a bisection is chosen (E24).  kBounds: the
// capacity policy's bounds, the partitioner minimizes the cut inside
// them.  kRatio / kNcut: the region is partitioned at every fraction of
// balance_fractions (tight tolerance) and the split minimizing the
// ratio cut, cut / (a (1 - a)) with a the cell-area fraction of the
// first child (Wei-Cheng), or the normalized cut, cut (1 / vol_A +
// 1 / vol_B) with vol the weighted pin count of a side (Shi-Malik), is
// kept: the objective chooses the cut line instead of the whitespace
// policy.
enum class BalanceObjective
{
  kBounds,
  kRatio,
  kNcut
};

// Direction of a bisection when both are possible (E24).  kLonger cuts
// the longer side (square children, the classic rule).  kSparsest
// partitions both ways and keeps the direction with the smaller cut per
// unit length of the cut line (the geometric sparsest cut: fewer nets
// per routing track across the line).
enum class CutDirection
{
  kLonger,
  kSparsest
};

// How the whitespace of a region is divided between its children
// (details in src/Capacity.h).
enum class ReservePolicy
{
  kUniform,     // children keep the region's utilization
  kMinLocal,    // target density; excess away from the external pull
  kCongestion,  // target density; excess proportional to RUDY + pins
  kMixed,       // target density; excess by a blend of both rules
  kAcg          // balance and excess from the analytical mass across the
                // planned cut line (Alpert-Nam-Villarrubia; needs -oracle)
};

// How a seed placement (-seed_placement) enters a split.
enum class AssignmentMode
{
  kNone,          // partitioner from scratch; the seed enters only
                  // through the displacement cost (displacement_weight)
  kTransport,     // min-cost-flow assignment of the seed locations to
                  // the children, then refinement by the engine from it
  kTransportOnly  // the assignment alone, no refinement
};

// Analytical oracle of the decomposition (src/Oracle.h): positions of
// every movable cell, re-solved after each level with the cells tethered
// to their regions, used by the following level's splits in place of
// region centers.
enum class OracleKind
{
  kNone,
  kQp  // bound-to-bound quadratic placement with region anchors
};

// Where the cells of a row go once their order is known
// (-leaf_positions).
enum class PositionRule
{
  kEven,     // packed left to right with the row's free space spread
             // evenly between the cells
  kOptimal,  // the order found with the packing rule, then the
             // positions minimizing the HPWL for it (clumping)
  kJoint     // order and positions searched together (exact up to site
             // snapping; several times slower than kOptimal)
};

struct Options
{
  // Decomposition: 0 = auto (4 when aspect ratio in [1/2, 2] and >= 2
  // rows, otherwise 2); 2 or 4 force the value.  Default 2: in E3
  // quadrisection trailed bisection by 7-15 % HPWL even with the hpwl
  // vectors (and by much more with cutnet).
  int k = 2;
  Objective objective = Objective::kCutNet;
  // Objective from level fine_level on (fine_level < 0: never switch).
  Objective fine_objective = Objective::kCutNet;
  int fine_level = -1;
  // E24 objective study.  Net weights by pin count (every engine), and
  // a file "net_name weight" of per-net factors multiplied on top
  // (e.g. timing criticality; nets not listed keep their weight).
  NetWeightRule net_weight = NetWeightRule::kUniform;
  std::string net_weights_file;
  // Balance chosen by the objective (k = 2, from-scratch splits only):
  // the candidate cell-area fractions of the first child and the
  // tolerance around each; levels >= balance_levels use the capacity
  // policy's bounds (-1: every level).
  BalanceObjective balance_objective = BalanceObjective::kBounds;
  std::vector<double> balance_fractions = {0.3, 0.4, 0.5, 0.6, 0.7};
  double balance_sweep_tolerance = 0.05;
  int balance_levels = -1;
  // Direction of a bisection when both are possible.
  CutDirection cut_direction = CutDirection::kLonger;
  EngineKind engine = EngineKind::kTritonPart;
  // Engine for the first top_levels levels (portfolio); same as engine
  // when top_levels == 0.
  EngineKind top_engine = EngineKind::kTritonPart;
  int top_levels = 0;
  // Target utilization of every region (reserve = 1 - density).  <= 0
  // means "derive from the core": cell area / usable area.  The
  // adaptive reserve policies move only the whitespace above it.
  double density = 0.0;
  // Two-sided tolerance on each child's proportional share of cell area.
  double balance_tolerance = 0.05;
  // How a region's whitespace is divided between its children (see
  // Capacity.h): uniform keeps every child at the region's utilization;
  // minlocal gives each child the room for its cells at `density` and
  // moves the rest away from the region's external pull; congestion
  // distributes it by a RUDY + pin-density surrogate; mixed blends both.
  // Default minlocal (E5: never worse than uniform, 20-60 % less HPWL at
  // low utilization); without a density above the utilization every
  // policy behaves like uniform.
  ReservePolicy reserve = ReservePolicy::kMinLocal;
  // minlocal/mixed: upper limit of the balance tolerance derived from
  // the whitespace above the target density.  1.0 leaves the split
  // unconstrained apart from the density (the cut line follows the
  // result); in the M5 sweeps HPWL improved monotonically up to 1.0.
  double max_tolerance = 1.0;
  TerminalMode terminals = TerminalMode::kPropagate;
  // Vector objectives only: a pin of a movable (not yet placed) external
  // cell whose coordinate lies within this fraction of the child extent
  // of a cut line is ambiguous along that axis and does not pull the net,
  // so the net pays the full price for being cut (classic terminal
  // propagation).  1/3 = the middle third of a bisected region.  0 (the
  // default: 2.5-3 % better HPWL on aes/ibex in E3) uses every external
  // pin at its region center.
  double terminal_band = 0.0;
  // Regions of a level are partitioned in this many waves (checkerboard
  // coloring); regions of a later wave see the children chosen for their
  // neighbors in earlier waves instead of the neighbors' region centers.
  // 1 partitions the whole level against the previous level's centers.
  int waves = 2;
  // A region with at most this many cells is an end case.
  int end_case_cells = 12;
  // Capo-style end game (M4).  A region with at most exact_cells cells
  // is partitioned exactly: the engine's result is the incumbent of a
  // branch-and-bound search over all assignments (0 disables).  A leaf
  // with at most optimal_cells cells is placed by branch and bound (row
  // assignment for the vertical HPWL, then the ordering of every row),
  // starting from the heuristic layout (0 disables).  After the end
  // cases a window of iron_window consecutive cells slides along every
  // row and is reordered optimally, for iron_passes passes (0 disables).
  int exact_cells = 16;
  int optimal_cells = 12;
  // The first of the two leaf searches (cells to rows for the vertical
  // HPWL).  false keeps the heuristic row assignment and runs only the
  // in-row ordering search: the single-row end case (E27 ablation).
  bool optimal_rows = true;
  int iron_window = 7;
  int iron_passes = 2;
  // Leaves are placed one after another, so a leaf is optimized while
  // the cells of the leaves placed after it still sit at their region
  // centers.  Passes beyond the first revisit every leaf with its
  // neighbors at their placed positions (incumbent: the current layout;
  // a leaf is rewritten only when cheaper) and stop early when a pass
  // changes nothing.  E10: the second pass gains 0.3-1.4 % HPWL, a third
  // at most 0.1 % more.
  int end_case_passes = 2;
  // Positions of the cells of a row for a given order, in the optimal
  // end case and in the ironing windows (the heuristic layout always
  // packs with even slack).  kOptimal: the HPWL-optimal positions for
  // the order the packing-rule search finds (E8: the even-slack
  // spreading is the whole remaining gap on gcd and 2-5 % on aes /
  // ibex).  kJoint also searches the order under those positions.
  PositionRule leaf_positions = PositionRule::kOptimal;
  // Placement padding (sites, per side) the end case and the ironing
  // keep between cells: what the legalizer will enforce
  // (set_placement_padding).  -1: pad_left / pad_right.
  int leaf_pad = -1;
  // Nets with more pins than this are ignored (0 = no limit).
  int max_fanout = 0;
  // Placement padding in sites added to the left/right of every movable
  // cell for the capacity accounting (as gpl's -pad_left/-pad_right):
  // the density then refers to padded cells, which is what the detailed
  // placer and the router need.  In E5, ibex at the real density 0.5
  // routed with overflow while gpl (padding 4) did not.
  int pad_left = 0;
  int pad_right = 0;
  // Soft placement blockages (DEF "PLACEMENT + SOFT", e.g. the macro
  // halos the macro placer leaves) are by default ignored, as the
  // detailed placer does.  With this set they are obstacles like hard
  // blockages and fixed instances, which is how gpl treats a soft
  // blockage without MAXDENSITY (every site under it is blocked), so
  // the two placers solve the same constrained problem.
  bool block_soft_blockages = false;
  // Use the current locations of the movable cells (e.g. from
  // `global_placement -skip_nesterov_place`) as a seed: every split
  // starts from the transportation assignment of the seed locations to
  // the children (assignment, bounded by the children's capacities at
  // the target density), the cut lines are shifted only within the gap
  // between the two sides' seeds, the end cases order cells by their
  // seed, and the refinement pays
  // displacement_weight * (a_v / mean area) * d(seed_v, box_q) / unit
  // for putting cell v into child q (its vertex vector; d = distance to
  // the child's box, 0 inside); the unit is chosen so that weight 1
  // makes moving an average cell between child centers as expensive as
  // one net spanning them.  The displacement cost needs a vector
  // objective; cutnet is replaced by conn (identical for k = 2).
  bool seed_placement = false;
  AssignmentMode assignment = AssignmentMode::kTransport;
  double displacement_weight = 0.0;
  // Seeded vector objectives (hpwl/rsmt): where a local pin is modeled
  // when its cell is put into child q.  1 (classical): the child's
  // center.  0: its seed clamped into the child's box -- the exact model
  // given the seed, under which keeping every cell on its seed's side is
  // HPWL-optimal and the partitioner reproduces the transport split
  // except for capacity.  In between: the linear interpolation, a model
  // of how far the spreading inside the child moves the cell.
  double seed_spread = 1.0;
  // Seeded transport assignment: weight of the netlist term in the cost
  // of putting cell i into child q,
  //   cost_iq = d(seed_i, box_q) + assignment_net_weight * dHPWL_i(q),
  // dHPWL_i(q) = change of the HPWL of i's nets when its pins move from
  // the seed (clamped into the region) to the seed clamped into box q,
  // every other pin at its position (local pins: seed clamped into the
  // region; after the first round, into their assigned child).  0: the
  // geometric transport.  assignment_rounds Gauss-Seidel rounds.
  double assignment_net_weight = 0.0;
  int assignment_rounds = 2;
  // The seed drives the levels < seed_levels only (assignment, clamped
  // cut lines, displacement, seed-ordered end cases); from that level on
  // the decomposition continues from scratch.  -1: every level.  Used to
  // attribute the gap between a seed and the from-scratch result to the
  // levels of the tree.
  int seed_levels = -1;
  // American-map study (AMERICAN_MAPS_STUDY.md section 7).
  // am_mincut_levels: in a seeded run the listed levels are split by
  // min-cut from scratch (planned balance, no assignment, cut lines not
  // clamped to the seeds) while every other level follows the seed
  // assignment -- the complement of seed_levels, one level at a time.
  // The seed-located terminals (seed_terminals) stay in force.
  std::vector<int> am_mincut_levels;
  // am_diagnose: in a seeded run with an assignment, every region is
  // also partitioned by min-cut from scratch (same subproblem, planned
  // balance) and the disagreement between the two partitions of the
  // same planned children is recorded per level (LevelStats::am_*:
  // disagreeing cells and area, the surrogate HPWL of the region's nets
  // under each partition with the local cells at the child centers, the
  // area-weighted displacement of each partition from the seed).
  // am_commit_mincut chooses which partition the tree keeps: false (the
  // assignment; identical to the run without the diagnostic) or true
  // (the min-cut: "min-cut decides, American map watched").
  bool am_diagnose = false;
  bool am_commit_mincut = false;
  // With seed_placement: where a movable cell is assumed to be before
  // its leaf is placed -- for the terminal propagation, the vector
  // objectives, the external pull of the reserve policy, the demand map
  // and the neighbors of an end case.  false: the center of its current
  // region (the classical estimate, which for a neighbor across the cut
  // line lies on the cut line itself); true: its seed clamped into its
  // current region.
  bool seed_terminals = false;
  // Analytical oracle (OracleKind).  After every level from
  // oracle_min_level on, the oracle solves for all movable cells with an
  // anchor to their region's center of weight oracle_anchor * degree /
  // region size (1: a cell is held within about its region against its
  // nets), oracle_rounds B2B re-weightings; the solution clamped into
  // the regions becomes the location of every movable cell for the next
  // level (terminal propagation, net costs with seed_spread < 1, the
  // reserve policy's pull, the end cases' neighbors).  The analytical
  // terminal propagation of Adya-Markov-Villarrubia with a B2B QP.
  OracleKind oracle = OracleKind::kNone;
  double oracle_anchor = 1.0;
  // Exponent of (region utilization / target density) in the anchor
  // weight (0: off).  E21b/c: sparse designs want a weak anchor (aes,
  // 4 % at density 0.3: 0.03-0.1), dense ones a stiff one (ibex, 44 % at
  // 0.5: 0.3-3); with exponent 1 and anchor 0.5-1 both are served.
  double oracle_anchor_util = 0.0;
  int oracle_rounds = 5;
  int oracle_min_level = 1;
  // false: the oracle positions do not replace the region centers as
  // the locations of the movable cells (no analytical terminal
  // propagation); they still drive -reserve acg and seed_spread.  For
  // the ablation of the components.
  bool oracle_terminals = true;
  // X1b: whole-level k-way refinement (KwayRefiner).  After the
  // bisections of a level in kway_refine_levels are committed, the
  // assignment of all cells to the regions of the new level is refined
  // as one k-way Steiner-tree mapping (Mt-KaHyPar V-cycles from the
  // tree's assignment; a cell may move between any two regions of the
  // level), every region keeping its cell area times kway_slack.  The
  // result is kept only if the HPWL at the region centers improves.
  // The levels are those of the regions refined ({2 4}: the 4 and the
  // 16 regions of a bisection tree; Mt-KaHyPar maps onto at most 64).
  // Empty: off.  Needs a build with Mt-KaHyPar (MCP_MTKAHYPAR_DIR).
  std::vector<int> kway_refine_levels;
  double kway_slack = 1.02;
  int kway_vcycles = 3;
  // X1a: at these levels the assignment is *constructed* from scratch by
  // the k-way mapping (same blocks, budgets and fixed vertices; the tree
  // supplies boxes and loads only) instead of refined; same acceptance
  // test.  A level in both lists is constructed first, then refined.
  // X0: a joint 4-way beat two bisections by 12-14 % of the proxy on
  // ibex and lost 1-2 % on aes; from scratch beyond k = 4 lost everywhere.
  std::vector<int> kway_construct_levels;
  // X2 (hypothesis H1): reversible boundary sweeps.  At these levels
  // (levels of the regions swept, as kway_refine_levels) every pair of
  // adjacent regions is re-bisected as one two-bin problem whose
  // objective is the HPWL of the surrogate itself (local pins at the two
  // region centers, every other pin where it is: the hpwl net vectors),
  // under the regions' current loads x sweep_slack; a result is kept
  // only if the exact HPWL at the centers decreases.  Passes over all
  // pairs (x boundaries, then y) repeat until a pass accepts nothing or
  // sweep_passes is reached.  sweep_multilevel: the main engine refines
  // each pair (default: the flat FM engine).  sweep_warm: the engine
  // starts from the current assignment (default) instead of from
  // scratch.  sweep_audit: recompute the full HPWL after every accepted
  // move and check it against the incremental one (debug).
  std::vector<int> sweep_levels;
  int sweep_passes = 4;
  double sweep_slack = 1.02;
  bool sweep_multilevel = false;
  bool sweep_warm = true;
  bool sweep_audit = false;
  // Placement feedback.  Passes beyond the first run the whole placer
  // again seeded by its own result (seed_placement with seed_terminals,
  // the assignment feedback_assignment and the displacement weight
  // feedback_weight), so that every level decides with the previous
  // pass's final positions instead of region centers.  The best pass is
  // kept; a pass that does not improve the HPWL ends the loop.  E11:
  // partitioning from scratch with a mild tether (kNone, 0.3) converges
  // in 2-7 passes to -0.6 / -3.2 / -6.9 % HPWL on gcd / aes / ibex; the
  // transport assignment reproduces the previous tree and gains nothing,
  // weight 1 is too stiff, 0.1 oscillates.  Each pass costs 2-3 base
  // runs (the displacement cost needs the vector refiner).
  int feedback_passes = 1;
  double feedback_weight = 0.3;
  // Assignment mode of the feedback passes: kNone partitions from
  // scratch and keeps the previous pass only through the displacement
  // cost and the terminals; kTransport starts every split from the
  // previous pass's split (the refinement then moves cells with the
  // previous positions as terminals).
  AssignmentMode feedback_assignment = AssignmentMode::kNone;
  // Refinement budget of the TritonPart engines: moves per FM pass and
  // number of passes.  -1 keeps TritonPart's defaults (50 moves, 2 passes)
  // for cutnet; vector objectives default to a full pass (one move per
  // vertex), since every vertex is a candidate there and the cut-only
  // pair-wise/greedy refiners do not run.
  int max_moves = -1;
  int refiner_iters = -1;
  // I1 (cut quality versus information).  engine_runs: every bisection
  // is partitioned this many times with different seeds and the cheapest
  // partition kept (best-of-N; 1 = one run).  cut_degrade: after the
  // engine (and the exact search) every bisection is worsened by
  // balance-preserving random swaps until its weighted cut is at least
  // (1 + cut_degrade) times the engine's, or no accepted swap is found
  // within the attempt budget; 0 = off.  The achieved cut before/after
  // is recorded per level (LevelStats::degrade_*).  seed_leaf: with
  // seed_placement, whether the end case orders a leaf's cells by their
  // seed (true, the AM-1 behaviour) or by their netlist pull like an
  // unseeded run (false: the seed enters the tree only through the
  // decisions / terminals it is enabled for).
  int engine_runs = 1;
  double cut_degrade = 0.0;
  bool seed_leaf = true;
  int threads = 1;
  unsigned seed = 0;
  // Path of a JSON report with per-level statistics ("" = none).
  std::string report_file;
  // Path of a text dump of the region tree for visualization ("" =
  // none): one region per line, "id parent level xmin ymin xmax ymax
  // cells cell_area usable_area cut_x cut_y_left cut_y_right" in dbu,
  // followed by one line per movable cell "C inst_name leaf_region
  // seed_x seed_y final_x final_y width height".
  std::string regions_file;
  // Path of a text dump of the placer's hypergraph ("" = none), the
  // companion of regions_file for offline partitioning experiments
  // (X0: a level's regions as the blocks of a direct k-way mapping).
  // "H vertices nets dbu site_width row_height core_xmin core_ymin
  // core_xmax core_ymax", then one line per vertex "V id kind name x y
  // width height area" (kind M movable / F fixed instance / P I/O pin;
  // x y the center of fixed vertices and pins, the seed of movable
  // cells; width the padded footprint) and one line per net "N id
  // weight name v1 ox1 oy1 v2 ox2 oy2 ..." (vertex ids with the pin
  // offsets from the vertex center), all in dbu.
  std::string hypergraph_file;
  // Paths of external partitioner binaries (ExternalEngine).
  std::string hmetis_path;
  std::string mtkahypar_path;
  std::string fastpart_path;
  // FastPart (FastPartEngine).  FastPart is a fixed-budget controller,
  // so the budget is the knob: a region at level L gets
  // fastpart_time * fastpart_decay^L seconds, at least fastpart_min_time
  // (decay 1: the same budget everywhere; 0.5: the root's budget is
  // halved every level, so the whole tree costs about twice the root
  // per lane).  fastpart_stall ends a region after that many consecutive
  // engine runs without improvement (0: the fixed budget, FastPart's
  // protocol).  Regions with at most fastpart_min_cells movable cells
  // are partitioned by TritonPart instead (the exact end game improves
  // them anyway; 0: FastPart everywhere).  The driver runs as one server
  // per placement with `threads` engine workers, so a region costs its
  // budget and not the engine start-up.  Defaults from E15: 10 s at the
  // root halved per level with the early stop costs 10 / 134 / 108 s on
  // gcd / aes / ibex (TritonPart 2 / 111 / 97 s) for 1.054 / 1.053 /
  // 1.121x gpl (1.074 / 1.139 / 1.221x); a fixed budget everywhere or
  // more budget below the root gained nothing more.
  double fastpart_time = 10.0;
  double fastpart_decay = 0.5;
  double fastpart_min_time = 0.2;
  int fastpart_min_cells = 16;
  int fastpart_stall = 10;
  // Scratch directory for external engines.
  std::string scratch_dir = "/tmp";
};

class MinCutPlacer
{
 public:
  MinCutPlacer(odb::dbDatabase* db,
               par::PartitionMgr* partition_mgr,
               utl::Logger* logger);
  ~MinCutPlacer();

  // Run the placer on the current block with the given options.
  void place(const Options& options);

  // Register an observer for debug graphics.
  void setDebug(std::unique_ptr<Observer>& observer);

 private:
  struct SavedInst
  {
    odb::dbInst* inst;
    odb::Point loc;
    odb::dbOrientType orient;
  };
  // Everything one pass of the placer produces: its model and region
  // tree (for the regions dump), its report, its HPWL and -- when
  // feedback passes may follow -- the placement it wrote.
  struct PassState
  {
    std::unique_ptr<NetlistModel> netlist;
    std::unique_ptr<PlacementArea> area;
    std::unique_ptr<RegionTree> tree;
    std::unique_ptr<RunReport> report;
    double hpwl_um = 0.0;
    std::vector<SavedInst> saved;
  };

  odb::dbBlock* getBlock();
  // One run of the decomposition and the end game with the given
  // options (pass numbers from 1; statistics of the design are logged
  // on the first pass only).
  std::unique_ptr<PassState> runPass(odb::dbBlock* block,
                                     const Options& options,
                                     int padding,
                                     int pass);
  void applyNetWeights(const Options& options,
                       NetlistModel& netlist,
                       bool verbose) const;
  void savePlacement(PassState& state) const;
  void restorePlacement(const PassState& state) const;
  int64_t computeHpwl(odb::dbBlock* block) const;
  void writeRegions(const Options& options,
                    const RegionTree& tree,
                    const NetlistModel& netlist) const;
  void writeHypergraph(const Options& options,
                       const NetlistModel& netlist,
                       const PlacementArea& area,
                       int dbu) const;
  void writeReport(const Options& options,
                   const RunReport& report,
                   double hpwl_um,
                   const NetlistStats& st,
                   const std::vector<double>& pass_hpwl,
                   int best_pass) const;

  odb::dbDatabase* db_ = nullptr;
  par::PartitionMgr* partition_mgr_ = nullptr;
  utl::Logger* logger_ = nullptr;

  std::unique_ptr<Observer> debug_observer_;
};

EngineKind engineKindFromString(const std::string& name);
Objective objectiveFromString(const std::string& name);
std::string objectiveToString(Objective objective);
NetWeightRule netWeightRuleFromString(const std::string& name);
const char* netWeightRuleToString(NetWeightRule rule);
BalanceObjective balanceObjectiveFromString(const std::string& name);
const char* balanceObjectiveToString(BalanceObjective objective);
CutDirection cutDirectionFromString(const std::string& name);
const char* cutDirectionToString(CutDirection direction);
// Weight of a net with d pins under a rule (1 for d <= 2).
double netWeightFor(NetWeightRule rule, int pins);
TerminalMode terminalModeFromString(const std::string& name);
ReservePolicy reservePolicyFromString(const std::string& name);
const char* reservePolicyToString(ReservePolicy policy);
AssignmentMode assignmentModeFromString(const std::string& name);
const char* assignmentModeToString(AssignmentMode mode);
PositionRule positionRuleFromString(const std::string& name);
OracleKind oracleKindFromString(const std::string& name);
const char* oracleKindToString(OracleKind kind);
const char* positionRuleToString(PositionRule rule);

}  // namespace mcp
