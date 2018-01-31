
#ifndef SIM_HH
#define SIM_HH
#include <experimental/optional>
#include <cstdint>
#include <queue>
#include <map>
#include <set>
#include <future>

namespace sim {
    namespace stx = std::experimental;
    
    
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
    
    
    struct engine;
    
    //
    //  component interface
    //
    struct component {
        virtual void step() = 0;
    protected:
        friend struct engine;
        void set_current_step(int64_t current_step) {
            current_step_ = current_step;
        };
        
        int64_t current_step_ {0};
    };
    
    
    //
    // engine will call step on all registered components, increasing
    // time by 1 step.
    //
    struct engine {
        void register_component(component& c) {
            c.set_current_step(current_step_);
            components_.insert(&c);
        };
        
        void unregister_component(component& c) {
            components_.erase(&c);
        };
        
        void step() {
            current_step_++;
            std::list<std::future<void>> futures;
            for(auto& it : components_) {
                futures.emplace_back(std::async(std::launch::async, [it, this]() {
                    it->set_current_step(current_step_);
                    it->step();
                }));
            }
        }
        
    private:
        int64_t current_step_ {0};
        std::set<component*> components_;
    };
    
    
    //
    // link, connect two or more nodes with a specified latency.
    // sending a packet will send to all other nodes.
    // for a proper tcp simulation, limit links to two peers.
    //
    // packets will be queued for n steps of latency where n is specified in
    // the ctor.
    //
    template <typename PacketType>
    struct link : public component {
        
        static_assert(std::is_copy_constructible<PacketType>(), "PacketType must be copy-constructible");
        
        using packet_callback_f = std::function<void(const PacketType&)>;
        
        //
        // Ctor. Specify latency in number of steps. Default is 1.
        link(int64_t latency = 1) : latency_(latency) {};
        link(const link& other)
        : packet_callbacks_(other.packet_callbacks_)
        , packets_(other.packets_)
        , latency_(other.latency_)
        , cur_peerid_(other.cur_peerid_) {};
        
        void step() override {
            mut_.lock();
            auto cb = packet_callbacks_;
            mut_.unlock();
            for(auto& it : cb) {
                mut_.lock();
                auto iit = packets_.find(it.first);
                if(iit != packets_.end()) {
                    mut_.unlock();
                    while(!iit->second.empty() && (current_step_ - iit->second.front().start_step) >= latency_) {
                        mut_.lock();
                        auto payload = iit->second.front().payload;
                        iit->second.pop();
                        mut_.unlock();
                        it.second(payload);
                    }
                } else {
                    mut_.unlock();
                }
            }
        }
        
        //
        // send a packet to all other peers
        void send_packet(int peerid, const PacketType& payload) {
            mut_.lock();
            packet p(current_step_, payload);
            for(auto& it : packet_callbacks_) {
                if(it.first != peerid) {
                    packets_[it.first].push(p);
                }
            }
            mut_.unlock();
        }
        
        //
        //
        // set callback for packet received
        void set_packet_callback(int peerid, packet_callback_f func) {
            mut_.lock();
            packet_callbacks_.emplace(peerid, func);
            mut_.unlock();
        }
    
        int next_peerid() {
            return ++cur_peerid_;
        }
        
    private:
        struct packet {
            packet(int64_t step, const PacketType& data) : start_step(step), payload(data) {};
            const int64_t start_step;
            PacketType payload;
        };
        
        std::mutex mut_;
        
        std::map<int, packet_callback_f>  packet_callbacks_;
        std::map<int, std::queue<packet>> packets_;
        const int64_t latency_ {1};
        int cur_peerid_{0};
    };
    
    template <typename PacketType>
    struct node {
        node(engine& engine) : engine_(engine) {}
        
        virtual void packet_callback(const PacketType& pkt) {};
        virtual void send_packet(const PacketType& pkt) {
            for(auto& it : link_) {
                if(it.second) {
                    it.second->send_packet(peerid_[it.first], pkt);
                }
            }
        }
        
        virtual void disconnect(node<PacketType>& other) {
            if(link_.find(&other) != link_.end() && this != &other) {
                other.disconnect(*this);
                link_.erase(&other);
                peerid_.erase(&other);
            }
        }
        virtual void connect(node<PacketType>& other, int latency = 1) {
            if(link_.find(&other) == link_.end() && this != &other) {
                auto l = std::make_shared<link<PacketType>>(latency);
                engine_.register_component(*l);
                connect(&other, l);
                other.connect(this, l);
            }
        }
        bool connected() const { return !link_.empty(); }
        size_t connections() const { return link_.size(); }
        bool has_peer(node<PacketType>& other) const {
            return link_.find(&other) != link_.end();
        }
    protected:
        void connect(void* ptr, std::shared_ptr<link<PacketType>>& lk) {
            link_.emplace(ptr, lk);
            peerid_.emplace(ptr, lk->next_peerid());
            lk->set_packet_callback(peerid_[ptr], [this](const PacketType& pkt) {
                packet_callback(pkt);
            });
        }
    protected:
        engine& engine_;
        std::map<void*, std::shared_ptr<link<PacketType>>> link_;
        std::map<void*, const int> peerid_;
    };
}

#endif /* SIM_HH */
