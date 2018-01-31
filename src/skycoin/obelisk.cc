
#include <deque>
#include <random>
#include <utility>
#include <cassert>
#include <sstream>
#include <iomanip>

#include "sim/ui.hh"
#include "sim/sim.hh"
#include "sim/log.hh"
#include "sim/blockchain.hh"

const int msPerStep = 50;
const int stepsPerSecond = 1000 / msPerStep;
const int stepsPer100ms = stepsPerSecond / 10;
const int numberPeers = 3;
const int blockTimeSteps = stepsPerSecond * 10; // make a new block every 10 "seconds"
const int N = 50; // number of nodes
//const int B = N; // number of blockmaking nodes
const int Z = N * 9 / 10 ; // number of block candidates before forming opinion
const int observerCount = N / 5;
const std::pair<int, int> stepsPerTxRange { stepsPer100ms * 10, stepsPer100ms * 20 };
const std::pair<int, int> latencyRange { stepsPer100ms, stepsPer100ms * 4 };
//static int tx_seqno = 0;
static int next_nodeid = 0;


struct opinion {
    int nodeid;
    int seq;
    sim::sha256_t block_sha;
};

struct packet {
    std::shared_ptr<sim::tx> txn;
    std::shared_ptr<sim::block> blk;
    std::shared_ptr<opinion> op;
    std::shared_ptr<sim::sha256_t> give;
};

struct node : public sim::node<packet>, public sim::component {
    node(sim::engine& e, sim::ui& ui, int steps, int tx_steps, bool observer)
    : sim::node<packet>(e)
    , ui(ui)
    , blocksteps(steps)
    , txsteps(tx_steps)
    , id(++next_nodeid)
    , observer(observer)
    {}; // id would be replaced by a public key
    
    void packet_callback(const packet& pkt) override {
        std::unique_lock<std::recursive_mutex> lk(mut);
        if(pkt.txn) {
            addTx(*pkt.txn);
        }
        if(pkt.op && cur_seq > -1) {
            //printf("[%d] cur_seq=%d opinion_ct=%zu\n", id, cur_seq, opinions.size());
            if(opinions.find(pkt.op->nodeid) == opinions.end()) {
                opinions.emplace(pkt.op->nodeid, pkt.op);
                send_packet(pkt);
            }
        }
        if(pkt.blk) {
            // got a block.
            auto sha = pkt.blk->hash();
            bool have = std::find_if(blocks.begin(), blocks.end(), [sha](std::shared_ptr<sim::block>& b) {
                return sha == b->hash();
            }) != blocks.end();
            
            if(!have) {
                blocks.push_back(pkt.blk);
                send_packet(pkt);
                if(observer) {
                    std::string str = std::to_string(id) + "-chain: ";
                    for(auto& it : blocks) {
                        auto sha = it->hash();
                        str += sim::sha_shortcode(sha) + " ";
                    }
                    ui.log(str);    
                }
            }
        }
        if(pkt.give) {
            auto sha = *pkt.give;
            auto it = std::find_if(blocks.begin(), blocks.end(), [sha](std::shared_ptr<sim::block>& b) {
                return sha == b->hash();
            });
            if(it != blocks.end()) {
                packet p;
                p.blk = *it;
                send_packet(p);
            }
        }
    }
    
    void step() override {
        std::unique_lock<std::recursive_mutex> lk(mut);
        if(current_step_ - last_blockstep > blocksteps) {
            last_blockstep = current_step_;
            createBlock();
        }
        if(current_step_ - last_txstep > txsteps) {
            last_txstep = current_step_;
            addTx(sim::tx {});
        }
        if(opinions.size() >= Z) {
            // decide on block.
            std::vector<sim::sha256_t> hashes;
            for(auto it = opinions.begin(); it != opinions.end(); ) {
                if(it->second->seq != cur_seq) {
                    it = opinions.erase(it);
                } else {
                    hashes.push_back(it->second->block_sha);
                    ++it;
                }
            }
            std::sort(hashes.begin(), hashes.end(), [](sim::sha256_t& lhs, sim::sha256_t& rhs) -> bool {
                return lhs < rhs;
            });
            sim::sha256_t cur_run{};
            sim::sha256_t long_run{};
            int cur_run_ct {0};
            int long_run_ct {0};
            
            for(auto& hash : hashes) {
                //printf("  hash: %s\n", sim::BytesToStr(hash.data(), hash.size()).c_str());
                if (hash != cur_run) {
                    if(cur_run_ct > long_run_ct) {
                        long_run_ct = cur_run_ct;
                        long_run = cur_run;
                    }
                    cur_run = hash;
                    cur_run_ct = 1;
                } else {
                    cur_run_ct++;
                }
            }
            if(cur_run_ct > long_run_ct) {
                long_run_ct = cur_run_ct;
                long_run = cur_run;
            }
            //sim::log().info("consensus looks like {}", sim::sha_shortcode(long_run));
            if(current_block && long_run == current_block->hash()) {
                // we got this one right
                blocks.push_back(current_block);
                if(observer) {
                    std::string str = std::to_string(id) + "-chain: ";
                    for(auto& it : blocks) {
                        auto sha = it->hash();
                        str += sim::sha_shortcode(sha) + " ";
                    }
                    ui.log(str);    
                }
                
            } else {
                // we didnt, request from someone who did.
                packet p ;
                p.give = std::make_shared<sim::sha256_t>(long_run);
                send_packet(p);
            }
            cur_seq = -1;
            opinions.clear();
            current_block.reset();
        }
    };
    
    bool hasTx(const sim::tx& t) {
        std::unique_lock<std::recursive_mutex> lk(mut);
        bool res = false;
        auto search_hash = t.hash();
        if(txs.size() > 0) {
            
            res = std::find_if(txs.begin(), txs.end(), [search_hash](const std::shared_ptr<sim::tx>& t) {
                return search_hash == t->hash();
            }) != txs.end();
        }
        if(blocks.size() > 0) {
            res |= std::find_if(blocks.begin(), blocks.end(), [search_hash](const std::shared_ptr<sim::block>& b) {
                if(b->txs.empty()) {
                    return false;
                }
                return std::find_if(b->txs.begin(), b->txs.end(), [search_hash](const std::shared_ptr<sim::tx>& t) {
                    return search_hash == t->hash();
                }) != b->txs.end();
            }) != blocks.end();
        }
        return res;
    }
    bool addTx(sim::tx t) {
        std::unique_lock<std::recursive_mutex> lk(mut);
        if(!hasTx(t)) {
            auto next_t = std::make_shared<sim::tx>(t);
            txs.push_back(next_t);
            packet p;
            p.txn = next_t;
            send_packet(p);
            return true;
        }
        return false;
    }
    sim::sha256_t tx_merkle() {
        std::unique_lock<std::recursive_mutex> lk(mut);
        std::vector<sim::sha256_t> hashes;
        for(auto& it : txs) {
            hashes.emplace_back(it->hash());
        }
        return sim::merkle256(hashes);
    }
    
    void createBlock() {
        std::unique_lock<std::recursive_mutex> lk(mut);
        if(txs.size() > 0) {
            auto seqno = current_step_ / blocksteps;
            cur_seq = (int)seqno;
            std::sort(txs.begin(), txs.end(), [](std::shared_ptr<sim::tx>& lhs, std::shared_ptr<sim::tx>& rhs) {
                return *lhs < *rhs;
            });
            //auto merkle = tx_merkle();
            //auto h = sim::sha_shortcode(merkle);
            //sim::log().info("submitting block candidate {} ({})", h, txs.size());
            
            {
                // move to staging in case we are the winner.
                current_block = std::make_shared<sim::block>();
                std::move(txs.begin(), txs.end(), std::back_inserter(current_block->txs));
                current_block->recompute_hash();
                txs.erase(txs.begin(), txs.end());
            }
            {
                // send out our opinion (that we are the winner, naturally)
                auto op = std::make_shared<opinion>();
                op->block_sha = current_block->hash();
                op->nodeid = id;
                op->seq = (int)seqno;
                packet p ;
                p.op = op;
                opinions.emplace(id, op);
                send_packet(p);
            }
        }
    }
    
    node(const node& other)
    : sim::node<packet>(other)
    , ui(other.ui)
    , current_block(other.current_block)
    , txs(other.txs)
    , blocks(other.blocks)
    , opinions(other.opinions)
    , blocksteps(other.blocksteps)
    , txsteps(other.txsteps)
    , id(other.id)
    , observer(other.observer)
    , last_blockstep(other.last_blockstep)
    , last_txstep(other.last_txstep)
    , cur_seq(other.cur_seq) {};
    
    sim::ui& ui;
    std::recursive_mutex mut;
    std::shared_ptr<sim::block> current_block;
    std::deque<std::shared_ptr<sim::tx>> txs;
    std::vector<std::shared_ptr<sim::block>> blocks;
    std::map<int, std::shared_ptr<opinion>> opinions;
    const int blocksteps;
    const int txsteps;
    const int id;
    const bool observer {false};
    int64_t last_blockstep{0};
    int64_t last_txstep{0};
    int cur_seq{-1};
};

int main(int argc, const char * argv[]) {
    // insert code here...
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<> dis(0, N-1);
    std::uniform_int_distribution<> latency(latencyRange.first, latencyRange.second);
    std::uniform_int_distribution<> tx_steps(stepsPerTxRange.first, stepsPerTxRange.second);
    sim::engine engine;
    std::atomic<bool> run {true};
    sim::ui ui {};

    std::vector<node> nodes;
    int observers = 0;
    for(int i = 0 ; i < N ; i++) {
        bool observer = (observers++ < observerCount);
        nodes.emplace_back(engine, ui, blockTimeSteps, tx_steps(gen), observer);
    }

    for(int i = 0 ; i < N; i++) {
        if(nodes[i].connections() < numberPeers) {
            engine.register_component(nodes[i]);
             for(auto j = nodes[i].connections() ; j < numberPeers; j++) {
                 int candidate {0};
                 while(i != candidate && nodes[i].has_peer(nodes[(candidate = dis(gen))]));
                 nodes[i].connect(nodes[candidate], latency(gen));
             }
        }
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

    return 0;
}
