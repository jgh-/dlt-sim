
#include <utility>
#include <cassert>
#include <random>
#include <limits>
#include <deque>

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
const int Z = N * 9 / 10; // number of block candidates before forming opinion
const int observerCount = N / 5;
const std::pair<int, int> stepsPerTxRange { stepsPer100ms * 10, stepsPer100ms * 25 };
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
    node(sim::engine& e, sim::ui* ui, int steps, int tx_steps, bool observer)
    : sim::node<packet>(e)
    , ui(ui)
    , blocksteps(steps)
    , txsteps(tx_steps)
    , id(++next_nodeid)
    , observer(observer)
    {

        auto genesis = std::make_shared<sim::block>();
        genesis->txs.emplace_back(new sim::tx(0xD34DBEEF));
        genesis->recompute_hash();
        blocks.push_back(genesis);

    }; // id would be replaced by a public key
    
    void packet_callback(const packet& pkt) override {
        std::unique_lock<std::recursive_mutex> lk(mut);
        if(pkt.txn) {
            addTx(*pkt.txn);
        }
        if(pkt.op && cur_seq > -1) {
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
            
            if(!have && pkt.blk->prev_block == blocks.back()->hash()) {
                blocks.push_back(pkt.blk);
                if(pkt.blk->hash() != curr_winner) {
                    log(std::to_string(id) + ":: conflict: " + sim::sha_shortcode(curr_winner) + " != " + sim::sha_shortcode(pkt.blk->hash()));
                }
                if(observer) {
                    std::string str = std::to_string(id) + "-chain: ";
                    for(auto& it : blocks) {
                        auto sha = it->hash();
                        str += sim::sha_shortcode(sha) + " ";
                    }
                    log(str);    
                }
                send_packet(pkt);
                int t = 0;
                curr_winner = sim::sha256((uint8_t*)&t, sizeof(t));
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
            auto txn = sim::tx { engine_.rand_int<int64_t>(std::numeric_limits<int64_t>::min(), std::numeric_limits<int64_t>::max()) };
            addTx(txn);
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
                    log(str);    
                }
                
            } else {
                // we didnt, request from someone who did.
                curr_winner = long_run;
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
    bool addTx(const sim::tx& t) {
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


            // move to staging in case we are the winner.
            current_block = std::make_shared<sim::block>();
            std::move(txs.begin(), txs.end(), std::back_inserter(current_block->txs));
            txs.erase(txs.begin(), txs.end());
            
            if(!blocks.empty()) {
                current_block->prev_block = blocks.back()->hash();
            }
            
            current_block->recompute_hash();
            
            if(observer) {
                log(std::to_string(id) + ": created block candidate " + sim::sha_shortcode(current_block->hash()));
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
    void print_chain() {
        std::string str = std::to_string(id) + "-chain: ";
        for(auto& it : blocks) {
            auto sha = it->hash();
            str += sim::sha_shortcode(sha) + " ";
        }
        sim::log().info(str);
    }
    void log(std::string str) {
        if(ui) {
            ui->log(str);
        } else {
            sim::log().info(str);
        }
    }

    node(const node& other)
    : sim::node<packet>(other)
    , curr_winner(other.curr_winner)
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
    
    sim::sha256_t curr_winner;
    sim::ui* ui;
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


    int64_t seed = time(NULL);
    if(argc > 1) {
        seed = std::strtol(argv[1],0,10);
    }
    sim::engine engine(seed);
    std::atomic<bool> run {true};
    std::vector<node> nodes;
    {
        sim::ui ui {};
        ui.log("Using seed " + std::to_string(seed));
        
        int observers = 0;

        for(int i = 0 ; i < N; i++) {
            bool observer = false;
            if(observers < observerCount && engine.rand_int<>(1,10) > 5) {
                observer = true;
                observers++;
            }
            nodes.emplace_back(engine, &ui, blockTimeSteps, engine.rand_int<>(stepsPerTxRange.first, stepsPerTxRange.second), observer);
        }

        for(int i = 0 ; i < numberPeers ; i++) {
            for(int j = 0; j < N; j++) {
                if(i == 0) {
                    if(j > 0) {
                        int candidate = engine.rand_int<>(0, j-1);
                        nodes[j].connect(nodes[candidate], engine.rand_int<>(latencyRange.first, latencyRange.second));
                    }
                } else {
                    int candidate = 0;
                    while(j == (candidate = engine.rand_int<>(0, N-1)) || nodes[j].has_peer(nodes[candidate]));
                    nodes[j].connect(nodes[candidate], engine.rand_int<>(latencyRange.first, latencyRange.second));
                }
            }
        }

        for(auto& it : nodes) {
            engine.register_component(it); 
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
    
    for(auto& it : nodes) {
        it.print_chain();
    }

    return 0;
}
