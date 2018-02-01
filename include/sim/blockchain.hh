#ifndef SIM_BLOCKCHAIN_HH
#define SIM_BLOCKCHAIN_HH

#include "sim/sha.hh"

#include <chrono>

namespace sim {


    struct tx {
        sha256_t pubkey;
        sha256_t sha;

        tx(int64_t num) {
            pubkey = sha256(reinterpret_cast<uint8_t*>(&num), sizeof(num));
            recompute_hash();
        }
        auto hash() const {
            return sha;
        }
        void recompute_hash() {
            std::vector<uint8_t> data;
            data.insert(data.end(), pubkey.data(), pubkey.data()+pubkey.size());
            sha = sha256(data.data(), data.size());
        }
        bool operator<(const tx& other) const {
            return hash() < other.hash();
        }
    };

    struct block {
        std::vector<std::shared_ptr<tx>> txs;
        sha256_t sha;
        sha256_t prev_block;
        sha256_t merkle;

        auto hash() const {
            return sha;
        }

        void recompute_hash() {
            std::vector<sha256_t> hashes;
            for(auto& it : txs) {
                hashes.emplace_back(it->hash());
            }
            merkle = sim::merkle256(hashes);
            hashes.clear();
            hashes.push_back(prev_block);
            hashes.push_back(merkle);
            sha = sha256(reinterpret_cast<uint8_t*>(hashes.data()), sizeof(sha256_t) * hashes.size());
        }
    };
}

#endif