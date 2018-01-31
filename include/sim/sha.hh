#ifndef SIM_SHA_HH
#define SIM_SHA_HH
#include <cryptopp/sha.h>
#include <cstdint>
#include <string>
#include <array>

namespace sim {

    //
    // Common DLT utils: sh256, merkle tree
    //
    using sha256_t = std::array<uint8_t, CryptoPP::SHA256::DIGESTSIZE>;
    
    inline std::string bytes_to_str(const uint8_t* data, const size_t size)
    {
        std::ostringstream oss;
        for (uint8_t* p = const_cast<uint8_t*>(data); p < data+size; p++)
            oss << std::hex << std::setw(2) << std::setfill('0') << static_cast<int>(*p);
        return oss.str();
    }
    
    inline sha256_t sha256(uint8_t* begin, size_t size) {
        CryptoPP::SHA256 hash;
        sha256_t digest;
        hash.CalculateDigest( digest.data(), begin, size  );
        return digest;
    };
    
    inline std::string sha_shortcode(const sha256_t& sha) {
        return bytes_to_str(sha.data(), sha.size()).substr(0,6);
    }
    
    inline sha256_t merkle256(const std::vector<sha256_t>& shas) {
        
        if(shas.size() > 0) {
            const auto nxpot = pow(2, ceil(log(shas.size())/log(2)));
            std::vector<sha256_t> h0 (shas.begin(), shas.end());
            for(int i = 0 ; i < nxpot - shas.size(); i++) {
                h0.emplace_back();
            }
            size_t round_start = 0;
            size_t round_end = h0.size();
            while(round_end - round_start > 1) {
                for(auto i = round_start; i < round_end; i+=2) {
                    h0.emplace_back(sha256(h0[i].data(), sizeof(sha256_t) * 2));
                }
                round_start = round_end;
                round_end = h0.size();
            }
            return h0.back();
        } else {
            return {};
        }
        
    };

}

#endif