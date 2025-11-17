#include <iostream>
#include <fstream>
#include <string>
#include <vector>
#include <random>
#include <thread>
#include <shared_mutex>
#include <chrono>
#include <algorithm>

using namespace std;

class DataStructure {
private:
    int fields[3];
    mutable shared_mutex mtx;

public:
    DataStructure() { fields[0] = fields[1] = fields[2] = 0; }
    int readField(int i) const { shared_lock<shared_mutex> lock(mtx); return fields[i]; }
    void writeField(int i, int value) { unique_lock<shared_mutex> lock(mtx); fields[i] = value; }
    string toString() const { shared_lock<shared_mutex> lock(mtx); return "[" + to_string(fields[0]) + ", " + to_string(fields[1]) + ", " + to_string(fields[2]) + "]"; }
};

struct ActionFrequency {
    double r0, w0, r1, w1, r2, w2, s;
};

void generateFile(const string& filename, const ActionFrequency& freq, int N) {
    ofstream out(filename);
    random_device rd; mt19937 gen(rd()); uniform_real_distribution<> dis(0.0, 1.0);
    for (int i = 0;i < N;i++) {
        double x = dis(gen); double sum = freq.r0 + freq.w0 + freq.r1 + freq.w1 + freq.r2 + freq.w2 + freq.s;
        double t = freq.r0 / sum; if (x < t) { out << "read 0\n"; continue; }
        t += freq.w0 / sum; if (x < t) { out << "write 0 1\n"; continue; }
        t += freq.r1 / sum; if (x < t) { out << "read 1\n"; continue; }
        t += freq.w1 / sum; if (x < t) { out << "write 1 1\n"; continue; }
        t += freq.r2 / sum; if (x < t) { out << "read 2\n"; continue; }
        t += freq.w2 / sum; if (x < t) { out << "write 2 1\n"; continue; }
        out << "string\n";
    }
}

long long executeFile(const string& filename, DataStructure& ds) {
    ifstream in(filename);
    vector<string> ops;
    string line;
    while (getline(in, line)) ops.push_back(line);
    auto start = chrono::high_resolution_clock::now();
    for (const string& cmd : ops) {
        if (cmd.compare(0, 4, "read") == 0) ds.readField(cmd[5] - '0');
        else if (cmd.compare(0, 5, "write") == 0) ds.writeField(cmd[6] - '0', 1);
        else if (cmd.compare(0, 6, "string") == 0) ds.toString();
    }
    auto end = chrono::high_resolution_clock::now();
    return chrono::duration_cast<chrono::microseconds>(end - start).count();
}

int main() {
    const int N = 200000;

    ActionFrequency freqA{ 0.20,0.05,0.20,0.05,0.10,0.20,0.40 };
    ActionFrequency freqB{ 0.10,0.10,0.10,0.10,0.10,0.10,0.40 };
    ActionFrequency freqC{ 0.01,0.30,0.25,0.01,0.05,0.08,0.30 };

    generateFile("a.txt", freqA, N);
    generateFile("b.txt", freqB, N);
    generateFile("c.txt", freqC, N);

    long long results[3][3]; // [threads][files]: threads 0=1,1=2,2=3; files 0=A,1=B,2=C

    // 1 Thread
    DataStructure ds1;
    results[0][0] = executeFile("a.txt", ds1);
    results[0][1] = executeFile("b.txt", ds1);
    results[0][2] = executeFile("c.txt", ds1);

    // 2 Threads
    DataStructure ds2;
    long long t2a[2], t2b[2], t2c[2];
    generateFile("a2_1.txt", freqA, N); generateFile("a2_2.txt", freqA, N);
    generateFile("b2_1.txt", freqB, N); generateFile("b2_2.txt", freqB, N);
    generateFile("c2_1.txt", freqC, N); generateFile("c2_2.txt", freqC, N);

    thread th2a[2]{ thread([&] { t2a[0] = executeFile("a2_1.txt",ds2); }),
                    thread([&] { t2a[1] = executeFile("a2_2.txt",ds2); }) };
    thread th2b[2]{ thread([&] { t2b[0] = executeFile("b2_1.txt",ds2); }),
                    thread([&] { t2b[1] = executeFile("b2_2.txt",ds2); }) };
    thread th2c[2]{ thread([&] { t2c[0] = executeFile("c2_1.txt",ds2); }),
                    thread([&] { t2c[1] = executeFile("c2_2.txt",ds2); }) };

    for (int i = 0;i < 2;i++) { th2a[i].join(); th2b[i].join(); th2c[i].join(); }

    results[1][0] = max(t2a[0], t2a[1]);
    results[1][1] = max(t2b[0], t2b[1]);
    results[1][2] = max(t2c[0], t2c[1]);

    // 3 Threads
    DataStructure ds3;
    long long t3a[3], t3b[3], t3c[3];
    generateFile("a3_1.txt", freqA, N); generateFile("a3_2.txt", freqA, N); generateFile("a3_3.txt", freqA, N);
    generateFile("b3_1.txt", freqB, N); generateFile("b3_2.txt", freqB, N); generateFile("b3_3.txt", freqB, N);
    generateFile("c3_1.txt", freqC, N); generateFile("c3_2.txt", freqC, N); generateFile("c3_3.txt", freqC, N);

    thread th3a[3]{ thread([&] { t3a[0] = executeFile("a3_1.txt",ds3); }),
                    thread([&] { t3a[1] = executeFile("a3_2.txt",ds3); }),
                    thread([&] { t3a[2] = executeFile("a3_3.txt",ds3); }) };
    thread th3b[3]{ thread([&] { t3b[0] = executeFile("b3_1.txt",ds3); }),
                    thread([&] { t3b[1] = executeFile("b3_2.txt",ds3); }),
                    thread([&] { t3b[2] = executeFile("b3_3.txt",ds3); }) };
    thread th3c[3]{ thread([&] { t3c[0] = executeFile("c3_1.txt",ds3); }),
                    thread([&] { t3c[1] = executeFile("c3_2.txt",ds3); }),
                    thread([&] { t3c[2] = executeFile("c3_3.txt",ds3); }) };

    for (int i = 0;i < 3;i++) { th3a[i].join(); th3b[i].join(); th3c[i].join(); }

    results[2][0] = max({ t3a[0],t3a[1],t3a[2] });
    results[2][1] = max({ t3b[0],t3b[1],t3b[2] });
    results[2][2] = max({ t3c[0],t3c[1],t3c[2] });

    // Print table
    cout << "\nExecution Time Table (us):\n";
    cout << "Threads\\File |   A       |   B       |   C\n";
    cout << "-------------|-----------|-----------|-----------\n";
    for (int i = 0;i < 3;i++) {
        cout << (i + 1) << "            | ";
        for (int j = 0;j < 3;j++) {
            cout << results[i][j];
            if (j < 2) cout << "    | ";
        }
        cout << "\n";
    }

    return 0;
}
