#include <memory>
#include <string>
#include <utility>

#include "benchmark/benchmark.h"
#include "harness.h"
#include "umbra/ui/input.h"
#include "umbra/ui/types.h"

namespace umbra::ui {
namespace {

std::shared_ptr<const MenuNode> Menu() {
  MenuNode root;
  for (int i = 0; i < 4; ++i) {
    MenuNode item{.id = "item" + std::to_string(i),
                  .label_key = "menu.item",
                  .kind = MenuItemKind::kSubmenu,
                  .selected = false,
                  .children = {}};
    for (int j = 0; j < 6; ++j) {
      item.children.push_back(MenuNode{.id = "choice",
                                       .label_key = "menu.choice",
                                       .kind = MenuItemKind::kChoice,
                                       .selected = j == 0,
                                       .children = {}});
    }
    root.children.push_back(std::move(item));
  }
  return std::make_shared<const MenuNode>(std::move(root));
}

void InputThenSnapshot(benchmark::State& state) {
  UiConfig config;
  config.layout = Layout::kTv;
  Harness harness(config);
  harness.Send(SetMenu{.root = Menu()});
  harness.Prepare(true);
  harness.Send(Navigate{.direction = Direction::kRight});
  bool right = false;
  for (auto _ : state) {
    static_cast<void>(_);
    harness.Send(
        Navigate{.direction = right ? Direction::kRight : Direction::kLeft});
    right = !right;
    benchmark::DoNotOptimize(harness.Snapshot());
  }
}
BENCHMARK(InputThenSnapshot);

void PlayheadCorrectionThenSnapshot(benchmark::State& state) {
  Harness harness;
  harness.Prepare(true);
  Micros position = 0;
  for (auto _ : state) {
    static_cast<void>(_);
    position += 1'000'000;
    harness.Send(TimeUpdate{.position_us = position, .anchor_us = 1});
    benchmark::DoNotOptimize(harness.Snapshot());
  }
}
BENCHMARK(PlayheadCorrectionThenSnapshot);

void MenuNavigationThenSnapshot(benchmark::State& state) {
  Harness harness;
  harness.Send(SetMenu{.root = Menu()});
  harness.Prepare(true);
  harness.Send(OpenMenu{});
  for (auto _ : state) {
    static_cast<void>(_);
    harness.Send(MenuNavigate{.direction = Direction::kDown});
    benchmark::DoNotOptimize(harness.Snapshot());
  }
}
BENCHMARK(MenuNavigationThenSnapshot);

}
}
