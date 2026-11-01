// SPDX-License-Identifier: GPL-3.0-or-later
// platform/dxvk patch 0007: DxvkLocalShareTable (src/dxvk/dxvk_local_share.h), the table through which a Direct3D 9Ex
// device and a Direct3D 11 device that run on one Vulkan device share a texture without Win32 external memory. The
// header is cut from the patch, and DXVK's own Rc and RcObject are used as they are; the image is a stand-in.
#include <atomic>
#include <cstdio>
#include <set>
#include <thread>
#include <vector>

#include "dxvk/dxvk_local_share.h"

using namespace dxvk;

struct FakeImage : RcObject {
  static inline std::atomic<int> alive = { 0 };
  FakeImage() { alive++; }
  ~FakeImage() { alive--; }
};

using Table = DxvkLocalShareTable<FakeImage, std::mutex>;

static int bad = 0;
static int reported = 0;
#define CHECK(condition, what) do { if (!(condition)) { if (reported++ < 8) std::printf("FAIL: %s (line %d)\n", what, __LINE__); bad = 1; } } while (0)

static DxvkLocalShareInfo info(uint32_t width) {
  DxvkLocalShareInfo i;
  i.width = width; i.height = 2 * width; i.mipLevels = 1; i.arraySize = 1;
  i.dxgiFormat = 87; i.bindFlags = 0x28; i.miscFlags = 2;
  return i;
}

static void basics() {
  Table table;
  Rc<FakeImage> image = new FakeImage;
  void* a = table.add(image, info(64));
  void* b = table.add(image, info(128));
  CHECK(a != Table::invalidHandle() && b != Table::invalidHandle() && a != b, "two handles that differ");
  CHECK(Table::isLocalHandle(a) && Table::isLocalHandle(b), "handles are in the local range");
  CHECK(table.count() == 2, "two entries");

  Rc<FakeImage> found; DxvkLocalShareInfo got;
  CHECK(table.find(a, &found, &got) && found.ptr() == image.ptr() && got.width == 64 && got.height == 128, "the first is found with its info");
  CHECK(table.find(b, nullptr, &got) && got.width == 128, "the second is found, image not asked for");
  CHECK(table.find(b, &found, nullptr), "info not asked for");

  // handles that are not the table's
  for (uintptr_t value : { uintptr_t(0), uintptr_t(4), uintptr_t(0x40000002u), uintptr_t(0x7da50000u), ~uintptr_t(0) })
    CHECK(!table.find(reinterpret_cast<void*>(value), &found, &got), "a handle of another kind is not found");
  CHECK(!Table::isLocalHandle(nullptr) && !Table::isLocalHandle(Table::invalidHandle()) && !Table::isLocalHandle(reinterpret_cast<void*>(0x40000002u)),
        "other handles are outside the local range");

  table.remove(a);
  CHECK(!table.find(a, &found, &got) && table.find(b, &found, &got) && table.count() == 1, "remove takes one entry off");
  table.remove(a);  // twice: harmless
  table.remove(nullptr);
  CHECK(table.count() == 1, "removing what is not there changes nothing");
  table.remove(b);
  found = nullptr;
  image = nullptr;
  CHECK(FakeImage::alive == 0 && table.count() == 0, "the table's references are given back");
}

static void references() {
  Table table;
  void* h;
  {
    Rc<FakeImage> image = new FakeImage;
    h = table.add(image, info(8));
  }
  CHECK(FakeImage::alive == 1, "the table keeps the image alive after its maker let go (an opened texture can outlive the maker)");
  Rc<FakeImage> opened;
  CHECK(table.find(h, &opened, nullptr), "found");
  table.remove(h);
  CHECK(FakeImage::alive == 1, "an opened texture keeps it after the entry is removed");
  opened = nullptr;
  CHECK(FakeImage::alive == 0, "and the last reference ends it");
}

static void full_and_reuse() {
  Table table;
  Rc<FakeImage> image = new FakeImage;
  std::vector<void*> handles;
  std::set<void*> seen;
  void* h;
  while ((h = table.add(image, info(1))) != Table::invalidHandle()) {
    CHECK(seen.insert(h).second, "no handle twice while listed");
    CHECK(Table::isLocalHandle(h), "in range");
    handles.push_back(h);
    if (handles.size() > 20000) break;
  }
  CHECK(handles.size() == 0x3fff, "the table holds 16383 shares");
  CHECK(table.add(image, info(1)) == Table::invalidHandle(), "a full table refuses");
  table.remove(handles[100]);
  void* again = table.add(image, info(2));
  CHECK(again != Table::invalidHandle() && table.count() == 0x3fff, "room again after one removal");
  CHECK(again != handles[5] && again != handles[200], "the new handle is not one still listed");
  // churn past the handle space (with one place free): no duplicates among the live ones
  table.remove(handles[7]);
  for (int round = 0; round < 40000; round++) {
    void* mine = table.add(image, info(3));
    CHECK(mine != Table::invalidHandle(), "room in the churn");
    table.remove(mine);
  }
  CHECK(table.count() == 0x3fff - 1, "churn leaves the count alone");
  for (void* each : handles) table.remove(each);
  table.remove(again);
  CHECK(table.count() == 0, "emptied");
}

static void threads() {
  Table table;
  Rc<FakeImage> image = new FakeImage;
  std::atomic<int> failures = { 0 };
  std::vector<std::thread> workers;
  for (int t = 0; t < 4; t++)
    workers.emplace_back([&, t] {
      std::vector<void*> mine;
      for (int i = 0; i < 4000; i++) {
        void* h = table.add(image, info(10 + t));
        DxvkLocalShareInfo got;
        if (h == Table::invalidHandle() || !table.find(h, nullptr, &got) || got.width != uint32_t(10 + t)) failures++;
        mine.push_back(h);
        if (mine.size() > 50) { table.remove(mine.front()); mine.erase(mine.begin()); }
      }
      for (void* h : mine) table.remove(h);
    });
  for (auto& w : workers) w.join();
  CHECK(failures == 0, "four threads adding, finding and removing");
  CHECK(table.count() == 0, "and the table is empty after them");
}

int main() {
  basics(); references(); full_and_reuse(); threads();
  if (!bad) std::puts("dxvk local share passed");
  return bad;
}
