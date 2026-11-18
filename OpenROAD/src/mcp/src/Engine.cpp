// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026, The OpenROAD Authors

#include "Engine.h"

#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <memory>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#include "par/PartitionMgr.h"
#include "utl/Logger.h"

namespace mcp {

namespace fs = std::filesystem;

int checkParts(const Subproblem& sub, const std::vector<int>& parts)
{
  if (static_cast<int>(parts.size()) < sub.num_movable) {
    return sub.num_movable;
  }
  int bad = 0;
  for (int v = 0; v < sub.num_movable; ++v) {
    if (parts[v] < 0 || parts[v] >= sub.k) {
      ++bad;
    }
  }
  return bad;
}

////////////////////////////////////////////////////////////////
// TritonPartEngine

TritonPartEngine::TritonPartEngine(par::PartitionMgr* mgr,
                                   bool multilevel,
                                   int max_moves,
                                   int refiner_iters)
    : mgr_(mgr),
      multilevel_(multilevel),
      max_moves_(max_moves),
      refiner_iters_(refiner_iters)
{
}

std::string TritonPartEngine::name() const
{
  return multilevel_ ? "tritonpart" : "fm";
}

bool TritonPartEngine::partition(const Subproblem& sub, std::vector<int>& parts)
{
  par::KWayRequest request;
  request.num_parts = sub.k;
  request.seed = sub.seed;
  request.hyperedges = &sub.hyperedges;
  request.vertex_weights = &sub.vertex_weights;
  request.hyperedge_weights = &sub.hyperedge_weights;
  bool has_fixed = false;
  for (int p : sub.fixed_part) {
    if (p >= 0) {
      has_fixed = true;
      break;
    }
  }
  request.fixed_attr = has_fixed ? &sub.fixed_part : nullptr;
  request.upper_block_balance = sub.upper;
  request.lower_block_balance = sub.lower;
  request.max_coarsen_iters = multilevel_ ? -1 : 0;
  request.quiet = true;
  request.net_vectors = sub.vectors;
  request.refiner_iters = refiner_iters_;
  request.max_moves = max_moves_;
  if (max_moves_ < 0 && sub.vectors) {
    // Under a vector objective every vertex is a candidate (uncut nets
    // with external pins have nonzero gains) and only the k-way FM
    // refiner runs, so a pass must be able to visit every vertex.
    // TritonPart's default of 50 moves per pass was tuned for
    // boundary-only cut refinement followed by greedy passes.
    request.max_moves = static_cast<int>(sub.vertex_weights.size());
  }
  std::vector<int> initial;
  if (!sub.initial_parts.empty()) {
    // Movable vertices from the assignment, terminals at their part.
    initial = sub.initial_parts;
    initial.resize(sub.vertex_weights.size());
    for (size_t v = sub.num_movable; v < initial.size(); ++v) {
      initial[v] = sub.fixed_part[v];
    }
    request.initial_solution = &initial;
  }

  std::vector<int> solution = mgr_->PartitionKWay(request);
  if (static_cast<int>(solution.size()) < sub.num_movable) {
    return false;
  }
  parts.assign(solution.begin(), solution.begin() + sub.num_movable);
  return checkParts(sub, parts) == 0;
}

////////////////////////////////////////////////////////////////
// ExternalEngine

ExternalEngine::ExternalEngine(EngineKind kind,
                               const std::string& binary,
                               const std::string& scratch_dir,
                               utl::Logger* logger)
    : kind_(kind), binary_(binary), scratch_dir_(scratch_dir), logger_(logger)
{
}

std::string ExternalEngine::name() const
{
  switch (kind_) {
    case EngineKind::kHmetis:
      return "hmetis";
    case EngineKind::kMtKaHyPar:
      return "mtkahypar";
    default:
      return "external";
  }
}

// hMETIS format: "E V 11" (both weights present), one line per
// hyperedge with 1-based vertex ids, then one weight per vertex.
// Terminals get weight 1 because some readers reject zero weights.
bool ExternalEngine::writeHypergraph(const Subproblem& sub,
                                     const std::string& hgr,
                                     const std::string& fix) const
{
  std::ofstream out(hgr);
  if (!out) {
    return false;
  }
  const int num_vertices = static_cast<int>(sub.vertex_weights.size());
  // The file format takes integer weights.  Unit weights are written as
  // 1; fractional weights (the net-weight rules of E24) are scaled so
  // that the smallest positive weight becomes 1 and the resolution is
  // at least 1/100 of it, then rounded.  Sums stay far below 2^31 for
  // the hypergraphs seen here (< 10^6 nets, weights <= 1.5 x the min).
  double w_min = 0.0;
  bool fractional = false;
  for (float w : sub.hyperedge_weights) {
    if (w > 0.0f && (w_min == 0.0 || w < w_min)) {
      w_min = w;
    }
    if (w != std::round(w)) {
      fractional = true;
    }
  }
  const double scale = fractional && w_min > 0.0 ? 100.0 / w_min : 1.0;
  out << sub.hyperedges.size() << ' ' << num_vertices << " 11\n";
  for (size_t e = 0; e < sub.hyperedges.size(); ++e) {
    out << std::max(1L, std::lround(sub.hyperedge_weights[e] * scale));
    for (int v : sub.hyperedges[e]) {
      out << ' ' << (v + 1);
    }
    out << '\n';
  }
  for (int v = 0; v < num_vertices; ++v) {
    const long w = v < sub.num_movable
                       ? std::max(1L, std::lround(sub.vertex_weights[v]))
                       : 1L;
    out << w << '\n';
  }
  out.close();

  std::ofstream fout(fix);
  if (!fout) {
    return false;
  }
  for (int v = 0; v < num_vertices; ++v) {
    fout << sub.fixed_part[v] << '\n';
  }
  return static_cast<bool>(fout);
}

bool ExternalEngine::readPartition(const std::string& file,
                                   int num_vertices,
                                   std::vector<int>& parts) const
{
  std::ifstream in(file);
  if (!in) {
    return false;
  }
  parts.clear();
  parts.reserve(num_vertices);
  int p;
  while (in >> p) {
    parts.push_back(p);
  }
  return static_cast<int>(parts.size()) == num_vertices;
}

std::string ExternalEngine::hmetisCommand(const Subproblem& sub,
                                          const std::string& hgr,
                                          const std::string& fix,
                                          const std::string& log) const
{
  // UBfactor is the allowed deviation (percent of the total weight)
  // from the balanced share; derive it from the tightest upper bound.
  float total = 0;
  for (int v = 0; v < sub.num_movable; ++v) {
    total += sub.vertex_weights[v];
  }
  double ub = 1.0;
  if (total > 0) {
    for (int q = 0; q < sub.k; ++q) {
      ub = std::max(ub, 100.0 * (sub.upper[q] / total - 1.0 / sub.k));
    }
  }
  const int ubfactor = std::clamp(static_cast<int>(std::ceil(ub)), 1, 49);
  std::ostringstream cmd;
  // tools/hmetis/hmetis_driver: HGraphFile FixFile Nparts UBfactor Seed
  cmd << binary_ << ' ' << hgr << ' ' << fix << ' ' << sub.k << ' ' << ubfactor
      << ' ' << sub.seed << " > " << log << " 2>&1";
  return cmd.str();
}

std::string ExternalEngine::mtkahyparCommand(const Subproblem& sub,
                                             const std::string& dir,
                                             const std::string& hgr,
                                             const std::string& fix,
                                             const std::string& log) const
{
  std::vector<int> terminals(sub.k, 0);
  for (size_t v = sub.num_movable; v < sub.fixed_part.size(); ++v) {
    if (sub.fixed_part[v] >= 0 && sub.fixed_part[v] < sub.k) {
      ++terminals[sub.fixed_part[v]];
    }
  }
  std::ostringstream cmd;
  cmd << binary_ << " -h " << hgr << " -k " << sub.k
      << " -e 0.03 -o cut --preset-type default -t " << std::max(1, sub.threads)
      << " --seed " << sub.seed << " -f " << fix << " --part-weights";
  for (int q = 0; q < sub.k; ++q) {
    cmd << ' ' << static_cast<long>(std::ceil(sub.upper[q])) + terminals[q];
  }
  cmd << " -w --partition-output-folder " << dir << " -q > " << log << " 2>&1";
  return cmd.str();
}

std::string ExternalEngine::makeScratch(const Subproblem& sub) const
{
  std::error_code ec;
  const fs::path dir = fs::path(scratch_dir_)
                       / ("mcp_" + std::to_string(getpid()) + "_r"
                          + std::to_string(sub.region_id));
  fs::create_directories(dir, ec);
  if (ec) {
    logger_->warn(utl::MCP,
                  30,
                  "Cannot create scratch directory {}: {}",
                  dir.string(),
                  ec.message());
    return "";
  }
  return dir.string();
}

bool ExternalEngine::partition(const Subproblem& sub, std::vector<int>& parts)
{
  if (sub.vectors != nullptr) {
    // External partitioners only know the cut; let the fallback run.
    return false;
  }
  std::error_code ec;
  const fs::path dir = makeScratch(sub);
  if (dir.empty()) {
    return false;
  }
  const std::string hgr = (dir / "region.hgr").string();
  const std::string fix = (dir / "region.fix").string();
  const std::string log = (dir / "engine.log").string();
  if (!writeHypergraph(sub, hgr, fix)) {
    logger_->warn(utl::MCP, 31, "Cannot write {}", hgr);
    return false;
  }

  std::string cmd;
  std::string result_file;
  if (kind_ == EngineKind::kHmetis) {
    cmd = hmetisCommand(sub, hgr, fix, log);
    result_file = hgr + ".part." + std::to_string(sub.k);
  } else {
    cmd = mtkahyparCommand(sub, dir.string(), hgr, fix, log);
  }

  const int status = std::system(cmd.c_str());
  if (status != 0) {
    logger_->warn(utl::MCP,
                  32,
                  "{} failed (status {}) on region {}; see {}",
                  name(),
                  status,
                  sub.region_id,
                  log);
    return false;
  }

  if (kind_ == EngineKind::kMtKaHyPar) {
    // <hgr>.part<k>.epsilon<e>.seed<s>.KaHyPar
    for (const auto& entry : fs::directory_iterator(dir, ec)) {
      const std::string fname = entry.path().filename().string();
      if (fname.rfind("region.hgr.part", 0) == 0) {
        result_file = entry.path().string();
        break;
      }
    }
  }
  const int num_vertices = static_cast<int>(sub.vertex_weights.size());
  std::vector<int> all;
  if (result_file.empty() || !readPartition(result_file, num_vertices, all)) {
    logger_->warn(utl::MCP,
                  33,
                  "{} produced no readable partition for region {}",
                  name(),
                  sub.region_id);
    return false;
  }
  parts.assign(all.begin(), all.begin() + sub.num_movable);
  const bool ok = checkParts(sub, parts) == 0;
  if (ok) {
    fs::remove_all(dir, ec);
  }
  return ok;
}

////////////////////////////////////////////////////////////////

FastPartEngine::FastPartEngine(const std::string& driver,
                               const Options& options,
                               par::PartitionMgr* mgr,
                               utl::Logger* logger)
    : ExternalEngine(EngineKind::kFastPart, driver, options.scratch_dir, logger),
      time_budget_(options.fastpart_time),
      decay_(options.fastpart_decay),
      min_time_(options.fastpart_min_time),
      min_cells_(options.fastpart_min_cells),
      stall_(options.fastpart_stall),
      lanes_(std::max(1, options.threads))
{
  if (min_cells_ > 0) {
    delegate_ = std::make_unique<TritonPartEngine>(
        mgr, true, options.max_moves, options.refiner_iters);
  }
  server_up_ = startServer();
}

FastPartEngine::~FastPartEngine()
{
  stopServer();
}

bool FastPartEngine::startServer()
{
  std::error_code ec;
  fs::create_directories(scratch_dir_, ec);
  static std::atomic<int> instance{0};
  const std::string base
      = (fs::path(scratch_dir_)
         / ("mcp_fastpart_" + std::to_string(getpid()) + "_"
            + std::to_string(instance.fetch_add(1))))
            .string();
  socket_path_ = base + ".sock";
  server_log_ = base + ".log";
  if (socket_path_.size() >= sizeof(sockaddr_un::sun_path)) {
    logger_->warn(utl::MCP,
                  45,
                  "Scratch path too long for a Unix socket: {}",
                  socket_path_);
    return false;
  }
  std::ostringstream cmd;
  // A .py driver runs under the python3 of the environment; anything
  // else (a wrapper script) is executed as is.
  if (binary_.size() > 3 && binary_.compare(binary_.size() - 3, 3, ".py") == 0) {
    cmd << "python3 ";
  }
  cmd << binary_ << " --serve " << socket_path_ << " --lanes " << lanes_
      << " --parent " << getpid() << " > " << server_log_ << " 2>&1 &";
  if (std::system(cmd.str().c_str()) != 0) {
    logger_->warn(utl::MCP, 46, "Cannot start the FastPart server: {}", cmd.str());
    return false;
  }
  // The server imports numpy/scipy and starts its workers before it
  // listens; a minute covers a cold NFS cache.
  const auto limit = std::chrono::steady_clock::now() + std::chrono::seconds(60);
  while (std::chrono::steady_clock::now() < limit) {
    if (fs::exists(socket_path_, ec)) {
      logger_->info(utl::MCP,
                    47,
                    "FastPart server with {} lanes on {} (budget {} s x {}^level, "
                    ">= {} s{}{})",
                    lanes_,
                    socket_path_,
                    time_budget_,
                    decay_,
                    min_time_,
                    stall_ > 0 ? ", stall " + std::to_string(stall_) : "",
                    min_cells_ > 0 ? ", <= " + std::to_string(min_cells_)
                                         + " cells to tritonpart"
                                   : "");
      return true;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
  }
  logger_->warn(utl::MCP,
                48,
                "The FastPart server did not come up; see {}",
                server_log_);
  return false;
}

void FastPartEngine::stopServer()
{
  if (!server_up_) {
    return;
  }
  std::string reply;
  request("{\"cmd\": \"quit\"}", reply);
  server_up_ = false;
  std::error_code ec;
  fs::remove(socket_path_, ec);
}

bool FastPartEngine::request(const std::string& json, std::string& reply) const
{
  const int fd = ::socket(AF_UNIX, SOCK_STREAM, 0);
  if (fd < 0) {
    return false;
  }
  sockaddr_un addr{};
  addr.sun_family = AF_UNIX;
  std::strncpy(addr.sun_path, socket_path_.c_str(), sizeof(addr.sun_path) - 1);
  if (::connect(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0) {
    ::close(fd);
    return false;
  }
  const std::string line = json + "\n";
  size_t sent = 0;
  while (sent < line.size()) {
    const ssize_t n = ::send(fd, line.data() + sent, line.size() - sent, 0);
    if (n <= 0) {
      ::close(fd);
      return false;
    }
    sent += static_cast<size_t>(n);
  }
  reply.clear();
  char buf[4096];
  for (;;) {
    const ssize_t n = ::recv(fd, buf, sizeof(buf), 0);
    if (n <= 0) {
      break;
    }
    reply.append(buf, static_cast<size_t>(n));
    if (!reply.empty() && reply.back() == '\n') {
      break;
    }
  }
  ::close(fd);
  return reply.find("\"ok\": true") != std::string::npos;
}

bool FastPartEngine::partition(const Subproblem& sub, std::vector<int>& parts)
{
  if (sub.vectors != nullptr) {
    // The portfolio knows the cut only; let the fallback run.
    return false;
  }
  if (delegate_ && sub.num_movable <= min_cells_) {
    return delegate_->partition(sub, parts);
  }
  if (!server_up_) {
    return false;
  }
  std::error_code ec;
  const fs::path dir = makeScratch(sub);
  if (dir.empty()) {
    return false;
  }
  const std::string hgr = (dir / "region.hgr").string();
  const std::string fix = (dir / "region.fix").string();
  const std::string result_file = (dir / "region.part").string();
  if (!writeHypergraph(sub, hgr, fix)) {
    logger_->warn(utl::MCP, 42, "Cannot write {}", hgr);
    return false;
  }
  const int num_vertices = static_cast<int>(sub.vertex_weights.size());
  const double budget
      = std::max(min_time_, time_budget_ * std::pow(decay_, sub.level));

  std::ostringstream req;
  req << "{\"hgr\": \"" << hgr << "\", \"fix\": \"" << fix << "\", \"k\": "
      << sub.k << ", \"seed\": " << sub.seed << ", \"lanes\": "
      << std::max(1, sub.threads) << ", \"time\": " << budget
      << ", \"stall\": " << stall_ << ", \"level\": " << sub.level
      << ", \"region\": " << sub.region_id << ", \"out\": \"" << result_file
      << "\"";
  req << ", \"upper\": [";
  for (int q = 0; q < sub.k; ++q) {
    req << (q ? ", " : "") << static_cast<long>(std::ceil(sub.upper[q]));
  }
  req << "], \"lower\": [";
  for (int q = 0; q < sub.k; ++q) {
    req << (q ? ", " : "")
        << static_cast<long>(std::floor(std::max(0.0f, sub.lower[q])));
  }
  req << "]";
  if (!sub.initial_parts.empty()) {
    // Warm start: the placer's seed assignment, terminals at their child.
    const std::string warm = (dir / "region.warm").string();
    std::ofstream wout(warm);
    for (int v = 0; v < num_vertices; ++v) {
      wout << (v < sub.num_movable ? sub.initial_parts[v] : sub.fixed_part[v])
           << '\n';
    }
    wout.close();
    req << ", \"warm\": \"" << warm << "\"";
  }
  req << "}";

  std::string reply;
  if (!request(req.str(), reply)) {
    logger_->warn(utl::MCP,
                  43,
                  "{} failed on region {}: {}",
                  name(),
                  sub.region_id,
                  reply.empty() ? "server unreachable (see " + server_log_ + ")"
                                : reply.substr(0, 200));
    return false;
  }
  std::vector<int> all;
  if (!readPartition(result_file, num_vertices, all)) {
    logger_->warn(utl::MCP,
                  44,
                  "{} produced no readable partition for region {}",
                  name(),
                  sub.region_id);
    return false;
  }
  parts.assign(all.begin(), all.begin() + sub.num_movable);
  const bool ok = checkParts(sub, parts) == 0;
  if (ok) {
    fs::remove_all(dir, ec);
  }
  return ok;
}

////////////////////////////////////////////////////////////////

std::unique_ptr<Engine> makeEngine(EngineKind kind,
                                   const Options& options,
                                   par::PartitionMgr* mgr,
                                   utl::Logger* logger)
{
  switch (kind) {
    case EngineKind::kFm:
      return std::make_unique<TritonPartEngine>(
          mgr, false, options.max_moves, options.refiner_iters);
    case EngineKind::kTritonPart:
      return std::make_unique<TritonPartEngine>(
          mgr, true, options.max_moves, options.refiner_iters);
    case EngineKind::kHmetis:
      if (options.hmetis_path.empty()) {
        logger->error(
            utl::MCP, 7, "-hmetis_path is required for -engine hmetis.");
      }
      return std::make_unique<ExternalEngine>(
          kind, options.hmetis_path, options.scratch_dir, logger);
    case EngineKind::kMtKaHyPar:
      if (options.mtkahypar_path.empty()) {
        logger->error(
            utl::MCP, 8, "-mtkahypar_path is required for -engine mtkahypar.");
      }
      return std::make_unique<ExternalEngine>(
          kind, options.mtkahypar_path, options.scratch_dir, logger);
    case EngineKind::kFastPart:
      if (options.fastpart_path.empty()) {
        logger->error(
            utl::MCP, 9, "-fastpart_path is required for -engine fastpart.");
      }
      return std::make_unique<FastPartEngine>(
          options.fastpart_path, options, mgr, logger);
  }
  return nullptr;
}

}  // namespace mcp
