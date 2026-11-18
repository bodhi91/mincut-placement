// Command-line driver around libhmetis.a (hMETIS 1.5, 32-bit) for the
// min-cut placer's ExternalEngine.
//
//   hmetis_driver HGraphFile FixFile Nparts UBfactor Seed [Nruns]
//
// HGraphFile is in hMETIS format ("E V 11": hyperedge weights first on
// each line, then one vertex weight per line).  FixFile has one line per
// vertex: -1 (free) or the block the vertex is fixed to; "-" means no
// fixed vertices.  The partition is written to HGraphFile.part.Nparts,
// one block id per line.  Exit status is non-zero (and no partition file
// is written) when hMETIS fails.
//
// Unlike the SpecPart wrapper this driver passes the pre-assignment flag
// (options[6] = 1) so that fixed vertices are honored.
//
// hMETIS 1.5 corrupts its heap ("corrupted size vs. prev_size" / SIGSEGV)
// when the pre-assignment leaves a block without any fixed vertex -- the
// common case in the placer, where a region often sees a single terminal.
// The driver therefore appends one isolated, zero-weight dummy vertex fixed
// to every such block; the dummies touch no hyperedge and carry no weight,
// so the cut and the balance of the real vertices are unchanged, and they
// are dropped from the output.

#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <iterator>
#include <sstream>
#include <string>
#include <vector>

extern "C" {
void HMETIS_PartRecursive(int nvtxs,
                          int nhedges,
                          int* vwgts,
                          int* eptr,
                          int* eind,
                          int* hewgts,
                          int nparts,
                          int ubfactor,
                          int* options,
                          int* part,
                          int* edgecut);
}

int main(int argc, char* argv[])
{
  if (argc < 6 || argc > 7) {
    std::cerr << "usage: hmetis_driver HGraphFile FixFile Nparts UBfactor "
                 "Seed [Nruns]\n";
    return 2;
  }
  const std::string hgr_file = argv[1];
  const std::string fix_file = argv[2];
  const int nparts = std::atoi(argv[3]);
  const int ubfactor = std::atoi(argv[4]);
  const int seed = std::atoi(argv[5]);
  const int nruns = argc == 7 ? std::atoi(argv[6]) : 10;

  std::ifstream in(hgr_file);
  if (!in) {
    std::cerr << "cannot open " << hgr_file << "\n";
    return 1;
  }
  std::string line;
  std::getline(in, line);
  std::istringstream header(line);
  std::vector<int> stats{std::istream_iterator<int>(header),
                         std::istream_iterator<int>()};
  if (stats.size() < 2) {
    std::cerr << "bad header in " << hgr_file << "\n";
    return 1;
  }
  const int nhedges = stats[0];
  const int nvtxs = stats[1];
  const int fmt = stats.size() == 3 ? stats[2] : 0;
  const bool has_hewgts = (fmt % 10) == 1;
  const bool has_vwgts = fmt >= 10;

  std::vector<int> eptr;
  std::vector<int> eind;
  std::vector<int> hewgts;
  eptr.reserve(nhedges + 1);
  for (int e = 0; e < nhedges; ++e) {
    if (!std::getline(in, line)) {
      std::cerr << "truncated hyperedge list\n";
      return 1;
    }
    std::istringstream buf(line);
    std::vector<int> net{std::istream_iterator<int>(buf),
                         std::istream_iterator<int>()};
    eptr.push_back(static_cast<int>(eind.size()));
    size_t first = 0;
    if (has_hewgts) {
      hewgts.push_back(net.empty() ? 1 : net[0]);
      first = 1;
    }
    for (size_t j = first; j < net.size(); ++j) {
      eind.push_back(net[j] - 1);
    }
  }
  eptr.push_back(static_cast<int>(eind.size()));

  std::vector<int> vwgts;
  if (has_vwgts) {
    vwgts.reserve(nvtxs);
    for (int v = 0; v < nvtxs; ++v) {
      if (!std::getline(in, line)) {
        std::cerr << "truncated vertex weights\n";
        return 1;
      }
      vwgts.push_back(std::atoi(line.c_str()));
    }
  }

  std::vector<int> part(nvtxs, -1);
  bool has_fixed = false;
  if (fix_file != "-") {
    std::ifstream fin(fix_file);
    if (!fin) {
      std::cerr << "cannot open " << fix_file << "\n";
      return 1;
    }
    int p;
    int v = 0;
    while (fin >> p && v < nvtxs) {
      part[v++] = p;
      if (p >= 0) {
        has_fixed = true;
      }
    }
  }

  // Dummy fixed vertices for the blocks without one (see the header).
  int nvtxs_all = nvtxs;
  if (has_fixed) {
    std::vector<bool> block_has_fixed(nparts, false);
    for (int v = 0; v < nvtxs; ++v) {
      if (part[v] >= 0 && part[v] < nparts) {
        block_has_fixed[part[v]] = true;
      }
    }
    for (int b = 0; b < nparts; ++b) {
      if (!block_has_fixed[b]) {
        part.push_back(b);
        if (has_vwgts) {
          vwgts.push_back(0);
        }
        ++nvtxs_all;
      }
    }
    if (nvtxs_all > nvtxs && !has_vwgts) {
      // Unit weights are implied; the dummies must weigh nothing.
      vwgts.assign(nvtxs, 1);
      vwgts.resize(nvtxs_all, 0);
    }
  }
  const bool pass_vwgts = has_vwgts || nvtxs_all > nvtxs;

  // options[0] = 1: use the values below.  [1] Nruns, [2] CType (1 =
  // hybrid first-choice), [3] RType (1 = FM), [4] Vcycle (1 = on the
  // final solution of each run), [5] Reconst, [6] pre-assignment flag,
  // [7] seed, [8] dbglvl.
  int options[9] = {1, nruns, 1, 1, 1, 0, has_fixed ? 1 : 0, seed, 0};
  int edgecut = -1;
  HMETIS_PartRecursive(nvtxs_all,
                       nhedges,
                       pass_vwgts ? vwgts.data() : nullptr,
                       eptr.data(),
                       eind.data(),
                       has_hewgts ? hewgts.data() : nullptr,
                       nparts,
                       ubfactor,
                       options,
                       part.data(),
                       &edgecut);

  for (int v = 0; v < nvtxs; ++v) {
    if (part[v] < 0 || part[v] >= nparts) {
      std::cerr << "hMETIS returned no valid partition\n";
      return 1;
    }
  }
  const std::string out_file = hgr_file + ".part." + std::to_string(nparts);
  std::ofstream out(out_file);
  for (int v = 0; v < nvtxs; ++v) {
    out << part[v] << '\n';
  }
  std::cout << "cut " << edgecut << "\n";
  return out ? 0 : 1;
}
