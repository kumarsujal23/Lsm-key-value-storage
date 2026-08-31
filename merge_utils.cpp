#include "merge_utils.h"
#include <queue>
#include <vector>
#include <string>

struct HeapEntry {
    std::shared_ptr<LSMEntry> entry;
    int list_index;
    int idx;
};

// The Corrected Comparator
struct SSTableMergeCmp {
    bool operator()(const HeapEntry& a, const HeapEntry& b) const {
        // 1. Primary Sort: Ascending by Key (Smallest/Alphabetical first)
        // In C++ max-heaps, returning true pushes 'a' down. 
        // So if a > b, 'a' goes down, keeping the smaller string at the top.
        if (a.entry->key != b.entry->key) {
            return a.entry->key > b.entry->key; 
        }
        
        // 2. Secondary Sort: Descending by Timestamp (Newest first)
        // If keys are identical, we want the LARGEST timestamp at the top.
        // So if a < b, 'a' goes down, keeping the larger timestamp at the top.
        return a.entry->timestamp < b.entry->timestamp;
    }
};

std::vector<std::shared_ptr<LSMEntry>> MergeRanges(
    const std::vector<std::vector<std::shared_ptr<LSMEntry>>>& ranges) {
    
    // Initialize our priority queue with the new key-based comparator
    std::priority_queue<HeapEntry, std::vector<HeapEntry>, SSTableMergeCmp> heap;

    // Phase 1: Push the first element of every SSTable into the heap
    for (int i = 0; i < static_cast<int>(ranges.size()); i++) {
        if (!ranges[i].empty()) {
            heap.push({ranges[i][0], i, 0});
        }
    }

    std::vector<std::shared_ptr<LSMEntry>> results;
    std::string last_processed_key = "";
    bool is_first_key = true;

    // Phase 2: The Streaming Merge Loop
    while (!heap.empty()) {
        HeapEntry current = heap.top();
        heap.pop();

        // Immediately push the next item from the same SSTable into the heap
        // to keep the stream flowing.
        int next_idx = current.idx + 1;
        if (next_idx < static_cast<int>(ranges[current.list_index].size())) {
            heap.push({ranges[current.list_index][next_idx], current.list_index, next_idx});
        }

        // Phase 3: Deduplication
        // If this key matches the one we just processed, it's an older version. Skip it.
        if (!is_first_key && current.entry->key == last_processed_key) {
            continue;
        }

        // We have a brand new key! Update our tracker.
        last_processed_key = current.entry->key;
        is_first_key = false;

        // Phase 4: Tombstone Filtering
        // If it's a DELETE command, we drop it. Otherwise, it goes into the final output.
        if (current.entry->command != Command::DELETE) {
            results.push_back(current.entry);
        }
    }

    return results;
}