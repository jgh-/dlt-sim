#ifndef SIM_DAG_HH
#define SIM_DAG_HH

#include "sim/sha.hh"

namespace sim {

    struct tx {
        sha256_t trunk;
        sha256_t branch;
        sha256_t sha;
        std::vector<uint8_t> payload;

        virtual void recompute_hash() {
            std::vector<uint8_t> data;
            data.insert(data.end(), trunk.data(), trunk.data()+trunk.size());
            data.insert(data.end(), branch.data(), branch.data()+branch.size());
            data.insert(data.end(), payload.begin(), payload.end());
            sha = sha256(data.data(), data.size());
        }
    };

}

#endif