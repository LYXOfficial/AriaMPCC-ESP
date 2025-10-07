#pragma once
#include "page.h"
#include <functional>

class HomeTimePage : public Page {
public:
  using SwitchPageFn = std::function<void(int)>;
  HomeTimePage(SwitchPageFn switcher);
  void render(bool full) override;
  bool onLeft() override;
  bool onRight() override;
  bool onCenter() override;
  const char *name() const override { return "home"; }

private:
  SwitchPageFn switchPage;
  // internal helpers
  void renderFull();
  void renderPartial();
};
