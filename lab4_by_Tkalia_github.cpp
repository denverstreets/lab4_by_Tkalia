#include <iostream>
#include <fstream>
#include <string>
#include <vector>
#include <mutex>
#include <thread>
#include <chrono>
#include <random>
#include <sstream>
#include <iomanip>
#include <array>

using namespace std;
using hr_clock = chrono::high_resolution_clock;

struct DataStructure {
    array<int, 3> fields;
    mutable array<mutex, 3> field_mtx;
    mutable mutex str_mtx;

    DataStructure() {
        fields.fill(0);
    }

    int get(size_t i) const {
        if (i >= fields.size()) throw out_of_range("get idx");
        lock_guard<mutex> lk(field_mtx[i]);
        return fields[i];
    }

    void set(size_t i, int v) {
        if (i >= fields.size()) throw out_of_range("set idx");
        lock_guard<mutex> lk(field_mtx[i]);
        fields[i] = v;
    }

    operator std::string() const {
        lock_guard<mutex> guard(str_mtx);

        scoped_lock lk(field_mtx[0], field_mtx[1], field_mtx[2]);

        ostringstream oss;
        oss << "DataStructure{";
        oss << "0:" << fields[0] << ", ";
        oss << "1:" << fields[1] << ", ";
        oss << "2:" << fields[2];
        oss << "}";
        return oss.str();
    }

    string to_string_locked() const {
        return static_cast<string>(*this);
    }
};

struct Command {
    enum Type { READ, WRITE, STRING } type;
    int idx;
    int val;

    static Command parse(const string& line) {
        istringstream iss(line);
        string t;
        iss >> t;
        if (t == "write") {
            int i, v; iss >> i >> v;
            return Command{ WRITE, i, v };
        }
        else if (t == "read") {
            int i; iss >> i;
            return Command{ READ, i, 0 };
        }
        else {
            return Command{ STRING, -1, 0 };
        }
    }
};

enum Variant { VAR_A = 0, VAR_B = 1, VAR_C = 2 };

vector<string> generate_sequence(Variant var, size_t ops_count, std::mt19937& rng) {
    vector<pair<string, int>> choices;
    if (var == VAR_A) {
        choices = {
            {"read 0", 10},
            {"write 0 1", 10},
            {"read 1", 10},
            {"write 1 1", 10},
            {"read 2", 40},
            {"write 2 1", 5},
            {"string", 15}
        };
    }
    else if (var == VAR_B) {
        choices = {
            {"read 0", 1},
            {"write 0 1", 1},
            {"read 1", 1},
            {"write 1 1", 1},
            {"read 2", 1},
            {"write 2 1", 1},
            {"string", 1}
        };
    }
    else {
        choices = {
            {"read 0", 60},
            {"write 0 1", 5},
            {"read 1", 5},
            {"write 1 1", 5},
            {"read 2", 5},
            {"write 2 1", 5},
            {"string", 15}
        };
    }

    vector<int> weights;
    vector<string> cmds;
    for (auto& p : choices) { cmds.push_back(p.first); weights.push_back(p.second); }
    discrete_distribution<int> dist(weights.begin(), weights.end());

    vector<string> out;
    out.reserve(ops_count);
    for (size_t i = 0; i < ops_count; ++i) {
        int choice = dist(rng);
        out.push_back(cmds[choice]);
    }
    return out;
}

void write_lines_to_file(const string& fname, const vector<string>& lines) {
    ofstream fout(fname);
    for (auto& l : lines) fout << l << '\n';
    fout.close();
}

struct ExecResult {
    uint64_t reads = 0;
    uint64_t writes = 0;
    uint64_t strings = 0;
    long long acc = 0;
};

void execute_commands_on_ds(DataStructure& ds, const vector<string>& lines, ExecResult& res) {
    vector<Command> cmds;
    cmds.reserve(lines.size());
    for (auto& ln : lines) cmds.push_back(Command::parse(ln));
    for (auto& c : cmds) {
        switch (c.type) {
        case Command::READ: {
            int v = ds.get((size_t)c.idx);
            res.reads++;
            res.acc += v;
            break;
        }
        case Command::WRITE: {
            ds.set((size_t)c.idx, c.val);
            res.writes++;
            break;
        }
        case Command::STRING: {
            string s = ds.to_string_locked();
            res.strings++;
            res.acc += (long long)s.size();
            break;
        }
        }
    }
}

chrono::milliseconds run_test_variant_threads(DataStructure& ds,
    const vector<vector<string>>& per_thread_lines) {
    size_t nthreads = per_thread_lines.size();
    vector<ExecResult> thread_results(nthreads);
    vector<thread> threads;
    threads.reserve(nthreads);
    auto t0 = hr_clock::now();
    for (size_t t = 0; t < nthreads; ++t) {
        threads.emplace_back([&, t]() {
            execute_commands_on_ds(ds, per_thread_lines[t], thread_results[t]);
            });
    }
    for (auto& th : threads) if (th.joinable()) th.join();
    auto t1 = hr_clock::now();
    chrono::milliseconds dur = chrono::duration_cast<chrono::milliseconds>(t1 - t0);
    uint64_t total_reads = 0, total_writes = 0, total_strings = 0;
    long long acc = 0;
    for (auto& r : thread_results) {
        total_reads += r.reads;
        total_writes += r.writes;
        total_strings += r.strings;
        acc += r.acc;
    }
    cout << "  -> total_reads=" << total_reads
        << " total_writes=" << total_writes
        << " total_string_ops=" << total_strings
        << " acc=" << acc
        << " time_ms=" << dur.count() << "\n";
    return dur;
}

int main() {
    ios::sync_with_stdio(false);
    cin.tie(nullptr);

    cout << "=== Multithreaded DataStructure demo (variant 9, m=3) ===\n";
    cout << "Using 3 mutex (for fields) + 1 mutex for string op = 4 mutexes total.\n\n";

    random_device rd;
    mt19937 rng(rd());

    const size_t OPS_PER_FILE = 120000;
    const int NUM_REPEATS = 3;
    const vector<Variant> variants = { VAR_A, VAR_B, VAR_C };
    const vector<string> variant_names = { "A_freq_given", "B_equal", "C_skewed" };

    cout << "Generating command files (in current directory)...\n";
    for (size_t vi = 0; vi < variants.size(); ++vi) {
        for (int t = 0; t < 3; ++t) {
            auto seq = generate_sequence(variants[vi], OPS_PER_FILE, rng);
            string fname = variant_names[vi] + "_t" + to_string(t) + ".txt";
            write_lines_to_file(fname, seq);
        }
    }
    cout << "Files generated: ";
    for (auto& n : variant_names) {
        cout << n << "_t0.txt, " << n << "_t1.txt, " << n << "_t2.txt; ";
    }
    cout << "\n\n";

    vector<vector<double>> avg_ms(3, vector<double>(3, 0.0));

    for (size_t vi = 0; vi < variants.size(); ++vi) {
        cout << "=== Variant " << variant_names[vi] << " ===\n";
        for (int nthreads = 1; nthreads <= 3; ++nthreads) {
            cout << "Running with " << nthreads << " thread(s) (averaging " << NUM_REPEATS << " runs)...\n";
            chrono::milliseconds total_ms(0);
            for (int rep = 0; rep < NUM_REPEATS; ++rep) {
                vector<vector<string>> per_thread_lines;
                per_thread_lines.reserve(nthreads);
                for (int t = 0; t < nthreads; ++t) {
                    string fname = variant_names[vi] + "_t" + to_string(t) + ".txt";
                    ifstream fin(fname);
                    vector<string> lines;
                    lines.reserve(OPS_PER_FILE);
                    string line;
                    while (getline(fin, line)) {
                        if (!line.empty()) lines.push_back(line);
                    }
                    fin.close();
                    per_thread_lines.push_back(move(lines));
                }

                DataStructure ds;

                cout << " Run " << (rep + 1) << "... ";
                auto ms = run_test_variant_threads(ds, per_thread_lines);
                total_ms += ms;
            }
            double mean_ms = double(total_ms.count()) / NUM_REPEATS;
            avg_ms[nthreads - 1][vi] = mean_ms;
            cout << "Average time for " << nthreads << " thread(s): " << mean_ms << " ms\n\n";
        }
    }

    cout << "\n=== Final averaged table (ms) ===\n";
    cout << "Rows = #threads (1..3), Cols = variants (A_freq_given, B_equal, C_skewed)\n";
    cout << fixed << setprecision(2);
    for (int r = 0; r < 3; ++r) {
        cout << (r + 1) << "T: ";
        for (int c = 0; c < 3; ++c) {
            cout << setw(10) << avg_ms[r][c] << " ";
        }
        cout << "\n";
    }

    cout << "\nAlso printing final DataStructure sample (single-threaded run on VAR_A) for sanity:\n";
    {
        string fname = "A_freq_given_t0.txt";
        ifstream fin(fname);
        vector<string> lines;
        string line;
        while (getline(fin, line)) if (!line.empty()) lines.push_back(line);
        DataStructure ds;
        ExecResult r;
        execute_commands_on_ds(ds, lines, r);
        cout << "After executing " << lines.size() << " ops (single-thread): " << ds.to_string_locked() << "\n";
    }

    cout << "\nDemo finished. Save screenshots of the FULL program output for your report.\n";
    return 0;
}
