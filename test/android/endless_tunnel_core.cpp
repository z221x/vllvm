#include "obstacle_generator.hpp"

#include <cstdint>
#include <cstdio>
#include <cstdlib>

// Link the exact APK objects; keep the test oracle itself unobfuscated.
int main() {
  uint64_t hash = 1469598103934665603ULL;
  unsigned cases = 0;
  auto add = [&](unsigned value) {
    hash = (hash ^ value) * 1099511628211ULL;
  };
  for (unsigned seed = 0; seed < 32; ++seed) {
    srand(seed);
    ObstacleGenerator generator;
    for (int difficulty = -2; difficulty <= 15; ++difficulty) {
      generator.SetDifficulty(difficulty);
      for (int iteration = 0; iteration < 100; ++iteration) {
        Obstacle obstacle;
        generator.Generate(&obstacle);
        if (obstacle.style < 1 || obstacle.style > 7)
          return 1;
        add(obstacle.style);
        add(obstacle.bonusRow + 1);
        add(obstacle.bonusCol + 1);
        for (int col = 0; col < OBS_GRID_SIZE; ++col) {
          for (int row = 0; row < OBS_GRID_SIZE; ++row)
            add(obstacle.grid[col][row]);
        }
        add(obstacle.HasBonus());
        ++cases;
      }
    }
  }
  printf("cases=%u checksum=%016llx\n", cases,
         static_cast<unsigned long long>(hash));
  return 0;
}
