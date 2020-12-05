#include <algorithm>
#include <mutex>

#include "hazard_ptr.hpp"

#ifndef HAZARD_FLUSH_SIZE
#define HAZARD_FLUSH_SIZE 16
#endif

namespace hazard {
namespace detail {

namespace {

class HazardRoot {
 public:
  HazardRoot() : hazard_record_head_(nullptr), hazard_bucket_head_(nullptr) {
  }

  HazardRoot(const HazardRoot&) = delete;
  HazardRoot& operator=(const HazardRoot&) = delete;

  ~HazardRoot();

  HazardRecord* allocateRecord();

  void deallocateRecord(HazardRecord* record);

  HazardBucket* allocateBucket();

  void flushRetired(HazardRecord* record);

 private:
  bool scanHp(ScanedSet& scaned);

  static void deleteItems(const ScanedSet& scaned, RetiredItems& retired);

  static void deleteAllItems(RetiredItems& retired);

  std::atomic<HazardRecord*> hazard_record_head_;
  std::atomic<HazardBucket*> hazard_bucket_head_;
};

HazardRoot::~HazardRoot() {
  HazardRecord* record = hazard_record_head_;
  while (record) {
    deleteAllItems(record->retired);
    HazardRecord* next = record->next;
    delete record;
    record = next;
  }

  HazardBucket* bucket = hazard_bucket_head_;
  while (bucket) {
    HazardBucket* next = bucket->next;
    delete bucket;
    bucket = next;
  }
}

HazardRecord*
HazardRoot::allocateRecord() {
  // record->active == 0 なものがあるか調べ、あれば active を 1 にして返す。
  HazardRecord* record = std::atomic_load_explicit(&hazard_record_head_, std::memory_order_acquire);
  while (record) {
    int active = std::atomic_load_explicit(&record->active, std::memory_order_relaxed);
    if (active == 0 && atomic_compare_and_set_wrap(&record->active, 0, 1)) {
      assert(record->buckets_in_use == 0);
      assert(record->hp_buckets.empty());
      return record;
    }
    record = record->next;
  }

  // 既存の HazardRecord は全て使用中だったので、
  // 新しい HazardRecord を生成し hazard_record_head_ に繋げてから返す。
  record = new HazardRecord();
  HazardRecord* head;
  do {
    head = std::atomic_load_explicit(&hazard_record_head_, std::memory_order_relaxed);
    record->next = head;
  } while (!atomic_compare_and_set_wrap(&hazard_record_head_, head, record));
  return record;
}

void
HazardRoot::deallocateRecord(HazardRecord* record) {
  assert(record->buckets_in_use == 0);

  // hp_buckets内の全てのHazardBucketを空き状態(active == 0)としてマークする。
  std::atomic_thread_fence(std::memory_order_release);
  for (HazardBucket* bucket : record->hp_buckets) {
    std::atomic_store_explicit(&bucket->active, 0, std::memory_order_relaxed);
  }
  record->hp_buckets.clear();

  // retired 内のオブジェクトをできる限りdeleteする。
  // (deleteしきれずに残ってしまってもよい。)
  if (!record->retired.empty()) {
    flushRetired(record);
  }

  // record を空き状態(active == 0)としてマークする。
  std::atomic_store_explicit(&record->active, 0, std::memory_order_release);
}

HazardBucket*
HazardRoot::allocateBucket() {
  // bucket->active == 0 なものがあるか調べ、あれば active を 1 にして返す。
  HazardBucket* bucket = std::atomic_load_explicit(&hazard_bucket_head_, std::memory_order_acquire);
  while (bucket) {
    int active = std::atomic_load_explicit(&bucket->active, std::memory_order_relaxed);
    if (active == 0 && atomic_compare_and_set_wrap(&bucket->active, 0, 1)) {
      return bucket;
    }
    bucket = bucket->next;
  }

  // 既存の HazardBucket は全て使用中だったので、
  // 新しい HazardBucket を生成し hazard_bucket_head_ に繋げてから返す。
  bucket = new HazardBucket();
  HazardBucket* head;
  do {
    head = std::atomic_load_explicit(&hazard_bucket_head_, std::memory_order_relaxed);
    bucket->next = head;
  } while (!atomic_compare_and_set_wrap(&hazard_bucket_head_, head, bucket));
  return bucket;
}

void
HazardRoot::flushRetired(HazardRecord* record) {
  try {
    if (scanHp(record->scaned)) {
      deleteItems(record->scaned, record->retired);
    } else {
      deleteAllItems(record->retired);
    }
  } catch(...) {
  }
}

bool
HazardRoot::scanHp(ScanedSet& scaned) {
  std::atomic_thread_fence(std::memory_order_seq_cst);
  scaned.clear();
  HazardBucket* bucket = std::atomic_load_explicit(&hazard_bucket_head_, std::memory_order_acquire);
  while (bucket) {
    for (const auto& h : bucket->hp) {
      const void* p = std::atomic_load_explicit(&h, std::memory_order_relaxed);
      if (p)
        scaned.push_back(p);
    }
    bucket = bucket->next;
  }
  std::atomic_thread_fence(std::memory_order_acquire);

  if (scaned.empty())
    return false;

  std::sort(scaned.begin(), scaned.end());
  scaned.erase(std::unique(scaned.begin(), scaned.end()), scaned.end());
  return true;
}

void
HazardRoot::deleteItems(const ScanedSet& scaned, RetiredItems& retired) {
  // この実装では、スキャンしたハザードポインタの内容を std::vector に格納し、
  // sort → unique → binary_search という手順でdelete可能かチェックしている。
  // しかし、代わりにOpen Addressing方式のHash Tableや、Bloom Filterを使うと
  // より効率的かもしれない。
  retired.erase(
      std::remove_if(
          retired.begin(), retired.end(),
          [&scaned] (RetiredItem& item) -> bool {
            if (std::binary_search(scaned.begin(), scaned.end(), item.object))
              return false;
            else {
              item.doDelete();
              return true;
            }
          }),
      retired.end());
}

void
HazardRoot::deleteAllItems(RetiredItems& retired) {
  for (auto item : retired)
    item.doDelete();
  retired.clear();
}

HazardRoot hazard_root;

__thread HazardRecord* local_record(nullptr);

}

HazardRecord::HazardRecord() : buckets_in_use(0), active(1) {
  retired.reserve(HAZARD_FLUSH_SIZE);
}

HazardRecord&
HazardRecord::getLocalRecord(std::size_t num_buckets) {
  HazardRecord* record = local_record;
  if (!record) {
    record = hazard_root.allocateRecord();
    local_record = record;
  }
  HazardBuckets& buckets = record->hp_buckets;
  num_buckets += record->buckets_in_use;
  if (buckets.size() < num_buckets) {
    buckets.reserve(num_buckets);
    for (std::size_t i = buckets.size(); i < num_buckets; i++)
      buckets.push_back(hazard_root.allocateBucket());
  }
  return *record;
}

void
HazardRecord::clearLocalRecord() {
  HazardRecord* record = local_record;
  if (record) {
    local_record = nullptr;
    hazard_root.deallocateRecord(record);
  }
}

void
HazardRecord::addRetired(void* obj, deleter_func del) {
  if (!obj) return;
  retired.push_back({obj, del});
  if (retired.size() >= HAZARD_FLUSH_SIZE)
    hazard_root.flushRetired(this);
}

}
}
