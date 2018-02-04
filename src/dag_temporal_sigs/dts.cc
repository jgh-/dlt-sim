#include "sim/dag.hh"
#include "sim/ui.hh"
#include "sim/sim.hh"

#include <sstream>
#include <limits>

//const int numberNodes = 5;
//const int numberObservers = numberNodes / 5;
//const int msPerStep = 50;
//const int stepsPerSecond = 1000 / msPerStep;
//const int stepsPer100ms = stepsPerSecond / 10;
//const std::pair<int, int> stepsPerTxRange { stepsPer100ms * 10, stepsPer100ms * 25 };
//const std::pair<int, int> latencyRange { stepsPer100ms, stepsPer100ms * 4 };

struct token {
    token() {};

    // obviously using a deterministic RNG in a real-world scenario is a complete no-no
    token(sim::engine& e) 
    {

        key[0] = e.rand_int<uint64_t>(0, std::numeric_limits<uint64_t>::max());
        key[1] = e.rand_int<uint64_t>(0, std::numeric_limits<uint64_t>::max());
        key[2] = e.rand_int<uint64_t>(0, std::numeric_limits<uint64_t>::max());
        key[3] = e.rand_int<uint64_t>(0, std::numeric_limits<uint64_t>::max());
    }
    

    uint64_t key[4]; // 256-bit key
    uint64_t time { std::numeric_limits<uint64_t>::max() };
    std::string alias;
    sim::sha256_t hash;
    sim::sha256_t recompute_hash() {
        std::vector<uint8_t> d;
        d.insert(d.end(), (uint8_t*)key, (uint8_t*)key+sizeof(key));
        d.insert(d.end(), alias.begin(), alias.end());
        d.insert(d.end(), (uint8_t*)&time, (uint8_t*)&time+sizeof(time));
        hash = sim::sha256(d.data(), d.size());
        return hash;
    }
    std::string to_string() {
        std::stringstream ss;
        // this is not a great way to represent these numbers, we'd probably want to use base-64 or base-58.
        ss << "key [0x" << std::hex << key[0] << key[1] << key[2] << key[3] << "] " << std::dec;
        ss << "time [" << time << "] alias [" << alias << "]";
        return ss.str();
    }
};

struct tx : public sim::tx {
    sim::sha256_t sig;

    enum class op_t : uint8_t {
        Announce = 0,
        CreateToken,
        RevealToken,
        Spend,
        Code,
        Data
    };

    enum class type_t : uint8_t {
        i32,
        i64,
        f32,
        f64,
        str,
        token
    };

    void recompute_hash() override {
        sim::tx::recompute_hash();
        std::vector<uint8_t> d;
        d.insert(d.end(), sha.data(), sha.data()+sha.size());
        d.insert(d.end(), sig.data(), sig.data()+sig.size());
        sha = sim::sha256(d.data(), d.size());
    }

    template <typename T>
    void addOp(op_t op, T& data) {

        payload.push_back(static_cast<std::underlying_type<op_t>::type>(op));

        if constexpr(std::is_integral<T>()) {
            if constexpr(sizeof(T) == sizeof(int32_t)) {
                payload.push_back(static_cast<std::underlying_type<type_t>::type>(type_t::i32));
            } else {
                payload.push_back(static_cast<std::underlying_type<type_t>::type>(type_t::i64));
            }
            payload.insert(payload.end(), (uint8_t*)&data, (uint8_t*)&data+sizeof(data));
        } else if constexpr(std::is_floating_point<T>()) {
            if constexpr(sizeof(T) == sizeof(float)) {
                payload.push_back(static_cast<std::underlying_type<type_t>::type>(type_t::f32));
            } else {
                payload.push_back(static_cast<std::underlying_type<type_t>::type>(type_t::f64));
            }
            payload.insert(payload.end(), (uint8_t*)&data, (uint8_t*)&data+sizeof(data));
        } else if constexpr(std::is_same<T, std::string>()) {
            payload.push_back(static_cast<std::underlying_type<type_t>::type>(type_t::str));
            uint16_t size = static_cast<uint16_t>(data.size());
            payload.insert(payload.end(), (uint8_t*)&size, (uint8_t*)&size+sizeof(size));
            payload.insert(payload.end(), data.begin(), data.end());
        } else if constexpr(std::is_same<T, token>()) {
            payload.push_back(static_cast<std::underlying_type<type_t>::type>(type_t::token));
            data.recompute_hash();
            payload.insert(payload.end(), data.hash.data(), data.hash.data()+data.hash.size());
        } else {
            static_assert("unsupported");
        }
    }
};

struct packet {

};

struct node : public sim::node<packet>, public sim::component {

    std::string alias;
};

int main(int argc, char* argv[]) {

    int64_t seed = time(NULL);
    if(argc > 1) {
        seed = std::strtol(argv[1],0,10);
    }
    sim::engine engine(seed);
    std::atomic<bool> run {true};

    {
        sim::ui ui {};
        ui.log("Using seed " + std::to_string(seed));
        
        {
            auto g0 = std::make_shared<tx>();
            auto g1 = std::make_shared<tx>();
            token t(engine);
            t.alias = "ONETIME";
            t.time = 2;
            g0->addOp(tx::op_t::Announce, "treasury");
            g0->addOp(tx::op_t::CreateToken, t);
            g0->sig = t.hash;
            g0->recompute_hash();
            std::string identity = "treasury@" + sim::sha_shortcode(g0->sha);
            ui.log(t.to_string());
            ui.log("ident: " + identity);
            for(int i = 0 ; i < 3 ; i++) {
                token tx(engine); // expires never
                tx.alias = identity;
                g1->addOp(tx::op_t::CreateToken, tx);
                ui.log(tx.to_string());
            }
            g1->sig = t.hash;
            g1->trunk = g0->sha;
            g1->recompute_hash();
            ui.log("g1:" + sim::sha_shortcode(g0->sha) + " <- " + sim::sha_shortcode(g1->sha));
        }

        std::thread t([&]() {
            auto next_time = std::chrono::steady_clock::now() + std::chrono::milliseconds(100);
            int i = 0;
            while(!!run) {
                ui.set_step(i);
                engine.step();
                i++;
                if(std::chrono::steady_clock::now() < next_time) {
                    std::this_thread::sleep_until(next_time);
                }
                while(next_time < std::chrono::steady_clock::now()) {
                    next_time += std::chrono::milliseconds(100);
                }
            }
        });

        ui.run();
        
        run = false;
        t.join();
    }

    return 0;
}