// Copyright 2020 Intel Corporation

#include "pmlc/target/x86/heatmap.h"

#include <array>
#include <cassert>
#include <map>

#include "pmlc/util/logging.h"

namespace pmlc::target::x86 {

using dialect::pxa::StencilCost;

extern uint64_t kHeatmapSize;
extern uint16_t kHeatmapKeys[][3];
extern float kHeatmapValues[];

static const unsigned kStartupCost = 32;

using Tile = std::tuple<unsigned, unsigned, unsigned>;

static std::array<Tile, 1> specialStencils = {
    Tile{64, 16, 3},
};

struct Heatmap {
  Heatmap() {
    for (unsigned i = 0; i < kHeatmapSize; ++i) {
      byTile.emplace(
          Tile{kHeatmapKeys[i][0], kHeatmapKeys[i][1], kHeatmapKeys[i][2]},
          kHeatmapValues[i]);
    }
  }

  std::map<Tile, double> byTile;
};

static Heatmap heatmap;

StencilCost heatmapCost(llvm::ArrayRef<int64_t> ranges) {
  assert(ranges.size() == 3 && "heatmapCost expects a 3D tile");

  IVLOG(6, "Calculating heatmap cost for tile ["
               << ranges[0] << " " << ranges[1] << " " << ranges[2] << "]");
  auto tile = Tile{ranges[0], ranges[1], ranges[2]};
  auto it = heatmap.byTile.find(tile);
  if (it != heatmap.byTile.end()) {
    IVLOG(6, "Found tile in the heatmap with throughput = " << it->second);
    return StencilCost{it->second, kStartupCost};
  }

  // We mainly care about M and K. If both (m, n - 1, k) and (m, n + 1, k)
  // exist, we may use their average value for prediction.
  auto itLower = heatmap.byTile.find(Tile{ranges[0], ranges[1] - 1, ranges[2]});
  if (ranges[1] == 1 || itLower != heatmap.byTile.end()) {
    auto itUpper =
        heatmap.byTile.find(Tile{ranges[0], ranges[1] + 1, ranges[2]});
    if (itUpper != heatmap.byTile.end()) {
      auto throughput = (ranges[1] > 1)
                            ? ((itLower->second + itUpper->second) / 2)
                            : itUpper->second;
      IVLOG(
          6,
          "Found nearby tiles in the heatmap and calculated average throughput "
          "= " << throughput);
      return StencilCost{throughput, kStartupCost};
    }
  }

  // If we cannot find (m, n, k) in the heatmap, try the special cases.
  for (auto stencil : specialStencils) {
    if (stencil == tile) {
      IVLOG(6, "Found a special tile witih throughput = 0.001");
      return StencilCost{0.001, kStartupCost};
    }
  }

  IVLOG(6, "Unable to calculate heatmap cost for the specified tile");
  return StencilCost{0.0, 0};
}

} // namespace pmlc::target::x86
