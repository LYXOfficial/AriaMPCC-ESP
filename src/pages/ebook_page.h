#pragma once
#include "page.h"
#include <vector>
#include <Preferences.h>

class EBookPage : public Page {
public:
  EBookPage();
  void render(bool full) override;
  bool onLeft() override;
  bool onRight() override;
  bool onCenter() override;
  const char *name() const override { return "ebook"; }

  // open a text file from SD by absolute path and paginate it
  bool openFromFile(const String &absPath);
  // pagination helpers exposed for async precompute
  bool ensurePageIndexUpTo(int idx);
  unsigned long computeNextPageOffset(unsigned long startOffset);
  void startPrecomputeAsync(int centerIndex);
  // approximate total pages based on file size and first-page byte length
  int estimateTotalPagesApprox();
  int getPageIndex() const { return pageIndex; }

private:
  // store page start offsets instead of full-page contents to avoid loading
  // entire file into RAM
  std::vector<unsigned long> pageOffsets;
  int pageIndex = 0;
  Preferences prefs;
  bool hasFile = false;
  // prompt state
  bool promptVisible = false;
  unsigned long promptShownAt = 0;
  // store original inactivity timeout so we can restore it on exit
  unsigned long origInactivityTimeout = 30000;
  // absolute path of opened file
  String openedPath;

  // pagination config (visual lines per page)
  // limit to 6 lines so content area stays above divider; spacing handled in render
  const int linesPerPage = 6;

  // build page offset index by streaming the file (no full-load)
  bool buildPageIndex(const String &absPath);
  // load a single page's content by page index
  String loadPageContent(int idx);
  void showPromptPartial();
  void hidePromptFull();
  void exitToFiles();
};
