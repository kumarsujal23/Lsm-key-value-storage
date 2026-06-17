#include "wal.h"
#include <stdexcept>

// ── Constructor ────────────────────────────────────────────────────────────
// Opens the WAL file in append mode — every WriteEntry call lands at the
// end, never overwrites. If the file doesn't exist, it's created.
WAL::WAL(const std::string& path) : path_(path) {
    write_file_.open(path, std::ios::binary | std::ios::app);
    if (!write_file_) {
        throw std::runtime_error("Cannot open WAL file: " + path);
    }
}

WAL::~WAL() { Close(); }
void WAL::Close() { if (write_file_.is_open()) write_file_.close(); }

// ── WriteEntry ─────────────────────────────────────────────────────────────
// Appends one entry. Same byte format as SSTable entries — kept consistent
// for simplicity, nothing forces them to match but it avoids inventing a
// second format.
//
// flush() after every write is deliberate: it forces the OS to actually
// push these bytes to disk rather than sitting in a buffer. Without this,
// a crash could lose entries that were "written" but never left RAM.
void WAL::WriteEntry(const std::string& key,
                     const std::string& value,
                     Command command,
                     int64_t timestamp) {
    uint32_t key_len = key.size();
    uint32_t val_len = value.size();
    uint8_t  cmd     = static_cast<uint8_t>(command);

    write_file_.write(reinterpret_cast<const char*>(&key_len), sizeof(uint32_t));
    write_file_.write(key.data(), key_len);
    write_file_.write(reinterpret_cast<const char*>(&val_len), sizeof(uint32_t));
    write_file_.write(value.data(), val_len);
    write_file_.write(reinterpret_cast<const char*>(&cmd),       sizeof(uint8_t));
    write_file_.write(reinterpret_cast<const char*>(&timestamp), sizeof(int64_t));

    write_file_.flush();
}

// ── RecoverEntries ─────────────────────────────────────────────────────────
// Reads every entry in the WAL, start to finish. Safe to do in one shot
// because Clear() guarantees this file never holds more than one
// memtable's worth of writes.
std::vector<std::shared_ptr<LSMEntry>> WAL::RecoverEntries() {
    std::ifstream file(path_, std::ios::binary);
    if (!file) return {};  // WAL doesn't exist yet — nothing to recover

    std::vector<std::shared_ptr<LSMEntry>> entries;
    while (file) {
        uint32_t key_len;
        file.read(reinterpret_cast<char*>(&key_len), sizeof(uint32_t));
        if (!file || file.gcount() == 0) break;  // clean EOF

        std::string key(key_len, '\0');
        file.read(key.data(), key_len);

        uint32_t val_len;
        file.read(reinterpret_cast<char*>(&val_len), sizeof(uint32_t));
        std::string value(val_len, '\0');
        file.read(value.data(), val_len);

        uint8_t cmd;
        file.read(reinterpret_cast<char*>(&cmd), sizeof(uint8_t));

        int64_t timestamp;
        file.read(reinterpret_cast<char*>(&timestamp), sizeof(int64_t));
        if (!file) break;  // truncated/corrupt tail — stop here

        auto entry       = std::make_shared<LSMEntry>();
        entry->key       = key;
        entry->value     = value;
        entry->command   = static_cast<Command>(cmd);
        entry->timestamp = timestamp;
        entries.push_back(entry);
    }
    return entries;
}

// ── Clear ──────────────────────────────────────────────────────────────────
// Wipes the WAL file empty. Called immediately after a successful flush —
// every entry that was sitting in this file is now durable on disk inside
// an SSTable, so there's nothing left worth keeping.
//
// We close the append handle, reopen with std::ios::trunc (which wipes
// the file to zero length), then reopen again in append mode so future
// WriteEntry calls keep working normally.
void WAL::Clear() {
    write_file_.close();
    write_file_.open(path_, std::ios::binary | std::ios::trunc);
    write_file_.close();
    write_file_.open(path_, std::ios::binary | std::ios::app);
}
