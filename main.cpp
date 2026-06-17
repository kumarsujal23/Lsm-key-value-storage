#include <iostream>
#include <string>
#include <sstream>
#include "lsm.h"

// ── CLI ────────────────────────────────────────────────────────────────────
// Simple interactive CLI to demonstrate the storage engine.
// Commands:
//   put <key> <value>
//   get <key>
//   delete <key>
//   scan <startKey> <endKey>
//   exit

static void printHelp() {
    std::cout << "\nCommands:\n"
              << "  put <key> <value>          Insert or update a key\n"
              << "  get <key>                  Retrieve a value\n"
              << "  delete <key>               Delete a key\n"
              << "  scan <startKey> <endKey>   Range scan (inclusive)\n"
              << "  exit                       Close and exit\n\n";
}

int main(int argc, char* argv[]) {
    std::string directory        = "lsm_data";
    int64_t     memtable_size    = 64 * 1024 * 1024;  // 64MB default

    // Allow custom directory as first argument
    if (argc > 1) directory = argv[1];

    std::cout << "Log-Structured Merge-Tree Storage Engine\n";
    std::cout << "Directory: " << directory << "\n";
    std::cout << "Memtable size: " << memtable_size / (1024 * 1024) << "MB\n";

    // Open the engine — loads existing SSTables, recovers from WAL
    std::shared_ptr<LSMTree> lsm;
    try {
        lsm = LSMTree::Open(directory, memtable_size, true);
        std::cout << "Engine opened. WAL recovery complete.\n";
    } catch (const std::exception& e) {
        std::cerr << "Failed to open engine: " << e.what() << "\n";
        return 1;
    }

    printHelp();

    std::string line;
    while (true) {
        std::cout << "> ";
        if (!std::getline(std::cin, line)) break;
        if (line.empty()) continue;

        std::istringstream iss(line);
        std::string cmd;
        iss >> cmd;

        if (cmd == "exit") {
            break;

        } else if (cmd == "put") {
            std::string key, value;
            iss >> key;
            std::getline(iss >> std::ws, value);  // rest of line is value
            if (key.empty() || value.empty()) {
                std::cout << "Usage: put <key> <value>\n";
                continue;
            }
            try {
                lsm->Put(key, value);
                std::cout << "OK\n";
            } catch (const std::exception& e) {
                std::cout << "Error: " << e.what() << "\n";
            }

        } else if (cmd == "get") {
            std::string key;
            iss >> key;
            if (key.empty()) {
                std::cout << "Usage: get <key>\n";
                continue;
            }
            std::string value = lsm->Get(key);
            if (value.empty()) {
                std::cout << "(nil)\n";
            } else {
                std::cout << value << "\n";
            }

        } else if (cmd == "delete") {
            std::string key;
            iss >> key;
            if (key.empty()) {
                std::cout << "Usage: delete <key>\n";
                continue;
            }
            try {
                lsm->Delete(key);
                std::cout << "OK\n";
            } catch (const std::exception& e) {
                std::cout << "Error: " << e.what() << "\n";
            }

        } else if (cmd == "scan") {
            std::string start_key, end_key;
            iss >> start_key >> end_key;
            if (start_key.empty() || end_key.empty()) {
                std::cout << "Usage: scan <startKey> <endKey>\n";
                continue;
            }
            auto results = lsm->RangeScan(start_key, end_key);
            if (results.empty()) {
                std::cout << "(empty)\n";
            } else {
                for (const auto& [k, v] : results) {
                    std::cout << k << " → " << v << "\n";
                }
            }

        } else {
            std::cout << "Unknown command: " << cmd << "\n";
            printHelp();
        }
    }

    std::cout << "Closing engine...\n";
    lsm->Close();
    std::cout << "Done.\n";

    return 0;
}
