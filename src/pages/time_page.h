#pragma once
#include "page.h"
#include <functional>

class HomeTimePage : public Page {
public:
  using SwitchPageFn = std::function<void(int)>;
  HomeTimePage(SwitchPageFn switcher);
  void render(bool full) override;
  void onLeft() override;
  void onRight() override;
  void onCenter() override;
  const char *name() const override { return "home"; }

private:
  SwitchPageFn switchPage;
  // internal helpers
  void renderFull();
  void renderPartial();
};
