// Refines a given mapping of a hypergraph onto a target graph with
// Mt-KaHyPar's V-cycles under the Steiner-tree objective (the X0 "k-way
// refinement from the tree's assignment" arm; the MtKaHyPar binary can
// only map from scratch).
//
//   improve_mapping HGR FIX TG PART_IN PART_OUT K VCYCLES THREADS SEED
//                   PRESET W_0 ... W_{K-1}
//
// HGR: hMETIS hypergraph, FIX: fixed-vertex file (-1 or block), TG: target
// graph (METIS), PART_IN: one block id per vertex, PRESET: default |
// quality | deterministic.  W_b: individual block weights.  Prints the
// Steiner-tree metric before and after; writes the refined partition.
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "mtkahypar.h"

static void fail(const char* what, mt_kahypar_error_t* err)
{
  std::fprintf(stderr, "%s: %s\n", what, err->msg ? err->msg : "unknown");
  std::exit(1);
}

int main(int argc, char** argv)
{
  if (argc < 12) {
    std::fprintf(stderr,
                 "usage: %s HGR FIX TG PART_IN PART_OUT K VCYCLES THREADS "
                 "SEED PRESET W_0 ... W_{K-1}\n",
                 argv[0]);
    return 2;
  }
  const char* hgr = argv[1];
  const char* fix = argv[2];
  const char* tg_file = argv[3];
  const char* part_in = argv[4];
  const char* part_out = argv[5];
  const int k = std::atoi(argv[6]);
  const int vcycles = std::atoi(argv[7]);
  const int threads = std::atoi(argv[8]);
  const int seed = std::atoi(argv[9]);
  const std::string preset_name = argv[10];
  if (argc != 11 + k) {
    std::fprintf(stderr, "expected %d block weights, got %d\n", k, argc - 11);
    return 2;
  }
  std::vector<mt_kahypar_hypernode_weight_t> weights(k);
  for (int b = 0; b < k; ++b) {
    weights[b] = std::atoll(argv[11 + b]);
  }
  mt_kahypar_preset_type_t preset = DEFAULT;
  if (preset_name == "quality") {
    preset = QUALITY;
  } else if (preset_name == "deterministic") {
    preset = DETERMINISTIC;
  }

  mt_kahypar_error_t err{};
  mt_kahypar_initialize(threads, false);
  mt_kahypar_context_t* ctx = mt_kahypar_context_from_preset(preset);
  mt_kahypar_set_partitioning_parameters(ctx, k, 0.03, KM1);
  mt_kahypar_set_seed(seed);
  if (std::getenv("IMPROVE_MAPPING_VERBOSE") != nullptr) {
    mt_kahypar_set_context_parameter(ctx, VERBOSE, "1", &err);
  }
  mt_kahypar_set_individual_target_block_weights(ctx, k, weights.data());

  mt_kahypar_hypergraph_t hg
      = mt_kahypar_read_hypergraph_from_file(hgr, ctx, HMETIS, &err);
  if (hg.hypergraph == nullptr) {
    fail("read hypergraph", &err);
  }
  const mt_kahypar_hypernode_id_t n = mt_kahypar_num_hypernodes(hg);
  std::vector<mt_kahypar_partition_id_t> fixed(n);
  if (mt_kahypar_read_fixed_vertices_from_file(fix, n, fixed.data(), &err)
      != SUCCESS) {
    fail("read fixed vertices", &err);
  }
  if (mt_kahypar_add_fixed_vertices(hg, fixed.data(), k, &err) != SUCCESS) {
    fail("add fixed vertices", &err);
  }
  mt_kahypar_target_graph_t* tg
      = mt_kahypar_read_target_graph_from_file(tg_file, ctx, &err);
  if (tg == nullptr) {
    fail("read target graph", &err);
  }

  std::vector<mt_kahypar_partition_id_t> part(n);
  {
    FILE* f = std::fopen(part_in, "r");
    if (f == nullptr) {
      std::perror(part_in);
      return 1;
    }
    for (mt_kahypar_hypernode_id_t v = 0; v < n; ++v) {
      int b = 0;
      if (std::fscanf(f, "%d", &b) != 1) {
        std::fprintf(stderr, "%s: short partition file\n", part_in);
        return 1;
      }
      part[v] = b;
    }
    std::fclose(f);
  }
  mt_kahypar_partitioned_hypergraph_t phg
      = mt_kahypar_create_partitioned_hypergraph(hg, ctx, k, part.data(),
                                                 &err);
  if (phg.partitioned_hg == nullptr) {
    fail("create partitioned hypergraph", &err);
  }
  std::printf("before: steiner_tree %lld imbalance %.4f\n",
              static_cast<long long>(mt_kahypar_steiner_tree(phg, tg)),
              mt_kahypar_imbalance(phg, ctx));
  if (mt_kahypar_improve_mapping(phg, tg, ctx, vcycles, &err) != SUCCESS) {
    fail("improve mapping", &err);
  }
  std::printf("after:  steiner_tree %lld imbalance %.4f\n",
              static_cast<long long>(mt_kahypar_steiner_tree(phg, tg)),
              mt_kahypar_imbalance(phg, ctx));
  mt_kahypar_get_partition(phg, part.data());
  {
    long long num_fixed = 0, violated = 0;
    for (mt_kahypar_hypernode_id_t v = 0; v < n; ++v) {
      if (mt_kahypar_is_fixed_vertex(hg, v)) {
        ++num_fixed;
        violated += mt_kahypar_fixed_vertex_block(hg, v) != part[v];
      }
    }
    std::printf("fixed vertices %lld, in a wrong block after refinement %lld\n",
                num_fixed, violated);
  }
  FILE* f = std::fopen(part_out, "w");
  if (f == nullptr) {
    std::perror(part_out);
    return 1;
  }
  for (mt_kahypar_hypernode_id_t v = 0; v < n; ++v) {
    std::fprintf(f, "%d\n", part[v]);
  }
  std::fclose(f);
  mt_kahypar_free_partitioned_hypergraph(phg);
  mt_kahypar_free_target_graph(tg);
  mt_kahypar_free_hypergraph(hg);
  mt_kahypar_free_context(ctx);
  return 0;
}
