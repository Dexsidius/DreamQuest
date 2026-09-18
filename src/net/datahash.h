#pragma once
#include <cstdint>
#include <string>

// ---------------------------------------------------------------------------
//  Do both ends mean the same thing by "orc"?
//
//  The server simulates and the client draws, and both read data/*.json and
//  maps/*.mx to know what they are looking at. A friend on yesterday's build
//  has an orc with different hit points and a map with a wall somewhere else,
//  and nothing about that shows until a fight goes differently on two
//  screens -- the one desync that cannot be debugged from inside the game. So
//  the two folders are hashed and the hashes travel in Hello: a mismatch is
//  refused at the door, by name, before anything has been simulated.
//
//  FNV-1a, 64 bits, over every file's name and contents in name order.
//  Carriage returns are skipped, so a clone with git's autocrlf on and a zip
//  unpacked from one with it off still agree.
// ---------------------------------------------------------------------------

namespace net {

struct DataHashes {
    uint64_t data = 0;   // data/*.json
    uint64_t maps = 0;   // maps/*.mx
    int      data_files = 0, map_files = 0;
};

// Every file with `extension` directly inside `dir`. 0 files hashes to the
// FNV offset basis, which is not a valid answer: callers check the count.
uint64_t HashFolder(const std::string& dir, const std::string& extension, int* files = nullptr);
DataHashes ComputeDataHashes(const std::string& root = ".");

// "9f3a61c2": the first eight hex digits, enough to read aloud to a friend.
std::string ShortHash(uint64_t h);

} // namespace net
