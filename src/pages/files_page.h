#pragma once
#include "page.h"
#include <vector>

class FilesPage : public Page {
public:
  FilesPage();
  void refreshEntries();
  void render(bool full) override;
  bool onLeft() override;
  bool onRight() override;
  bool onCenter() override;
  const char *name() const override { return "files"; }
  
    // poll SD insertion/removal and update entries when changed
    void pollSd();
    // get the filename (with / for directories) at absolute index
    String getEntryNameAt(int absIndex);

private:
  // current directory
  String currentDir = "/";
  // all entries in current dir (directories end with '/')
  std::vector<String> allEntries;
  int totalEntries = 0;
  // visible window cache to avoid repeated full-directory scans
  std::vector<String> visibleCache;
  int visibleCacheStart = -1; // absolute index corresponding to visibleCache[0]
  const int visibleRows = 4;
  // top index of visible window (0..)
  int topIndex = 0;
  // highlighted row within visible window (0..3), -1 means no row selected
  int highlightedRow = -1;
  // whether an item is in selection mode (false = page navigation)
  bool selectionActive = false;
  // SD mounted flag
  bool sdAvailable = false;
  // last center tap time for double-tap detection
  unsigned long lastCenterTapMs = 0;
  // internal helper: detect SD state change and update entries accordingly
  void detectSdChange();
  void fillVisibleCache();
  // open the currently highlighted/selected entry (file or directory)
  void openSelected();
};
