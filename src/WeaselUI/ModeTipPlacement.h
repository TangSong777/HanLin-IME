#pragma once

#include <windows.h>

#include <algorithm>
#include <cstdlib>
#include <optional>
#include <tuple>
#include <vector>

namespace weasel {

inline std::optional<RECT> PlaceModeTip(const RECT& caret,
                                        const SIZE& size,
                                        const RECT& work_area,
                                        std::vector<RECT> obstacles,
                                        LONG gap) {
  const LONG last_x = work_area.right - size.cx;
  const LONG last_y = work_area.bottom - size.cy;
  if (caret.bottom <= caret.top || size.cx <= 0 || size.cy <= 0 ||
      last_x < work_area.left || last_y < work_area.top)
    return std::nullopt;

  const LONG left = caret.left - gap - size.cx;
  const LONG below = caret.bottom + gap;
  const auto clamp_x = [&](LONG x) { return std::clamp(x, work_area.left, last_x); };
  const auto clamp_y = [&](LONG y) { return std::clamp(y, work_area.top, last_y); };
  // Include the caret so an edge of the monitor cannot clamp the tip over it.
  obstacles.push_back(caret);
  std::vector<LONG> xs{clamp_x(left), work_area.left, last_x};
  std::vector<LONG> ys{clamp_y(below), work_area.top, last_y};
  for (auto& obstacle : obstacles) {
    InflateRect(&obstacle, gap, gap);
    xs.push_back(clamp_x(obstacle.left - size.cx));
    xs.push_back(clamp_x(obstacle.right));
    ys.push_back(clamp_y(obstacle.top - size.cy));
    ys.push_back(clamp_y(obstacle.bottom));
  }

  std::optional<RECT> best;
  std::tuple<bool, bool, LONG, LONG> best_score;
  for (const LONG x : xs) {
    for (const LONG y : ys) {
      const RECT tip{x, y, x + size.cx, y + size.cy};
      if (std::any_of(obstacles.begin(), obstacles.end(), [&](const RECT& obstacle) {
            RECT overlap;
            return IntersectRect(&overlap, &tip, &obstacle) != FALSE;
          }))
        continue;
      const LONG vertical = std::abs(y - below);
      const auto score = std::make_tuple(y < below, x > left,
                                         std::abs(x - left) + vertical, vertical);
      if (!best || score < best_score) {
        best = tip;
        best_score = score;
      }
    }
  }
  return best;
}

}  // namespace weasel
