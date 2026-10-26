
#include <iostream>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>
#include <map>
#include <cstdint>
#include <iomanip>
#include <bitset>
#include <stdexcept>

using namespace std;

const uint32_t TEXT_START = 0x00400000;
const uint32_t DATA_START = 0x10010000;

struct Item {
    int line;
    bool data;
    uint32_t address;
    string text;
};

map<string, int> regs;
map<string, uint32_t> labels;
vector<Item> items;
vector<uint32_t> instructions;
vector<unsigned char> dataBytes;

void initRegisters() {
    string names[] = {
        "zero", "at", "v0", "v1", "a0", "a1", "a2", "a3",
        "t0", "t1", "t2", "t3", "t4", "t5", "t6", "t7",
        "s0", "s1", "s2", "s3", "s4", "s5", "s6", "s7",
        "t8", "t9", "k0", "k1", "gp", "sp", "fp", "ra"
    };

    for (int i = 0; i < 32; i++) {
        regs[names[i]] = i;
    }
}

string trim(string s) {
    size_t a = s.find_first_not_of(" \t\r\n");
    if (a == string::npos) return "";
    size_t b = s.find_last_not_of(" \t\r\n");
    return s.substr(a, b - a + 1);
}

vector<string> splitOperands(string s) {
    vector<string> result;
    if (trim(s).empty()) return result;

    string part;
    stringstream ss(s);

    while (getline(ss, part, ',')) {
        result.push_back(trim(part));
    }

    return result;
}

void error(int line, const string& message) {
    throw runtime_error(
        "Line " + to_string(line) + ": " + message
    );
}

int getReg(string s, int line) {
    s = trim(s);

    if (!s.empty() && s[0] == '$') {
        s = s.substr(1);
    }

    if (regs.count(s)) return regs[s];

    try {
        size_t pos;
        int n = stoi(s, &pos);

        if (pos == s.size() && n >= 0 && n <= 31) {
            return n;
        }
    } catch (...) {
    }

    error(line, "Invalid register: " + s);
    return 0;
}

uint32_t getValue(string s, int line) {
    s = trim(s);

    if (s.rfind("hi(", 0) == 0 && s.back() == ')') {
        string name = s.substr(3, s.size() - 4);

        if (!labels.count(name)) {
            error(line, "Unknown label: " + name);
        }

        return (labels[name] >> 16) & 0xFFFF;
    }

    if (s.rfind("lo(", 0) == 0 && s.back() == ')') {
        string name = s.substr(3, s.size() - 4);

        if (!labels.count(name)) {
            error(line, "Unknown label: " + name);
        }

        return labels[name] & 0xFFFF;
    }

    try {
        size_t pos;
        long long n = stoll(s, &pos, 0);

        if (pos == s.size() &&
            n >= -2147483648LL &&
            n <= 4294967295LL) {
            return static_cast<uint32_t>(n);
        }
    } catch (...) {
    }

    if (labels.count(s)) return labels[s];

    error(line, "Invalid number or label: " + s);
    return 0;
}

uint32_t makeI(int opcode, int rs, int rt, int imm) {
    return (uint32_t(opcode) << 26)
         | (uint32_t(rs) << 21)
         | (uint32_t(rt) << 16)
         | (uint32_t(imm) & 0xFFFF);
}

uint32_t makeR(int rs, int rt, int rd, int funct) {
    return (uint32_t(rs) << 21)
         | (uint32_t(rt) << 16)
         | (uint32_t(rd) << 11)
         | uint32_t(funct);
}

string decodeString(string s, int line) {
    s = trim(s);

    if (s.size() < 2 || s.front() != '"' || s.back() != '"') {
        error(line, ".asciiz requires a quoted string");
    }

    string out;

    for (size_t i = 1; i < s.size() - 1; i++) {
        char c = s[i];

        if (c == '\\') {
            if (i + 1 >= s.size() - 1) {
                error(line, "Invalid string escape");
            }

            c = s[++i];

            switch (c) {
                case 'n': out += '\n'; break;
                case 't': out += '\t'; break;
                case 'r': out += '\r'; break;
                case '\\': out += '\\'; break;
                case '"': out += '"'; break;
                case '0': out += '\0'; break;
                default: error(line, "Unsupported string escape");
            }
        } else {
            out += c;
        }
    }

    return out;
}

int branchOffset(string target, uint32_t pc, int line) {
    if (!labels.count(target)) {
        error(line, "Unknown branch label: " + target);
    }

    int64_t diff =
        int64_t(labels[target]) - int64_t(pc + 4);

    if (diff % 4 != 0) {
        error(line, "Branch target is not word-aligned");
    }

    int64_t offset = diff / 4;

    if (offset < -32768 || offset > 32767) {
        error(line, "Branch target out of range");
    }

    return static_cast<int>(offset);
}

uint32_t encode(string text, uint32_t pc, int line) {
    string op;
    string rest;

    stringstream ss(text);
    ss >> op;
    getline(ss, rest);

    vector<string> a = splitOperands(rest);

    auto need = [&](int n) {
        if ((int)a.size() != n) {
            error(line, "Wrong number of operands for " + op);
        }
    };

    // R-type arithmetic and logical instructions.
    if (op == "add" || op == "addu" || op == "sub" ||
        op == "and" || op == "or" || op == "slt") {
        need(3);

        int funct =
            op == "add"  ? 0x20 :
            op == "addu" ? 0x21 :
            op == "sub"  ? 0x22 :
            op == "and"  ? 0x24 :
            op == "or"   ? 0x25 : 0x2A;

        return makeR(
            getReg(a[1], line),
            getReg(a[2], line),
            getReg(a[0], line),
            funct
        );
    }

    if (op == "div") {
        need(2);

        return makeR(
            getReg(a[0], line),
            getReg(a[1], line),
            0,
            0x1A
        );
    }

    if (op == "mfhi") {
        need(1);

        return makeR(0, 0, getReg(a[0], line), 0x10);
    }

    if (op == "syscall") {
        need(0);
        return 0x0000000C;
    }

    // Immediate instructions, including slti.
    if (op == "addi" || op == "addiu" ||
        op == "slti" || op == "ori") {
        need(3);

        int rt = getReg(a[0], line);
        int rs = getReg(a[1], line);
        uint32_t v = getValue(a[2], line);
        int imm;

        if (op == "ori") {
            if (v > 65535) {
                error(line, "ORI immediate out of range");
            }

            imm = int(v);
        } else {
            int64_t signedValue = static_cast<int32_t>(v);

            if (signedValue < -32768 || signedValue > 32767) {
                error(line, "Immediate out of range");
            }

            imm = int(signedValue);
        }

        int opcode =
            op == "addi"  ? 0x08 :
            op == "addiu" ? 0x09 :
            op == "slti"  ? 0x0A : 0x0D;

        return makeI(opcode, rs, rt, imm);
    }

    if (op == "lui") {
        need(2);

        uint32_t imm = getValue(a[1], line);

        if (imm > 65535) {
            error(line, "LUI immediate out of range");
        }

        return makeI(0x0F, 0, getReg(a[0], line), int(imm));
    }

    // Conditional branches.
    if (op == "beq" || op == "bne") {
        need(3);

        int offset = branchOffset(a[2], pc, line);

        return makeI(
            op == "beq" ? 0x04 : 0x05,
            getReg(a[0], line),
            getReg(a[1], line),
            offset
        );
    }

    // Jump.
    if (op == "j") {
        need(1);

        if (!labels.count(a[0])) {
            error(line, "Unknown jump label: " + a[0]);
        }

        uint32_t target = labels[a[0]];

        if ((target & 3) != 0 ||
            (target >> 28) != (pc >> 28)) {
            error(line, "Invalid jump target");
        }

        return (0x02u << 26) |
               ((target >> 2) & 0x03FFFFFF);
    }

    error(line, "Unsupported instruction or pseudoinstruction: " + op);
    return 0;
}

void firstPass(const vector<string>& lines) {
    bool dataSection = false;

    uint32_t textPC = TEXT_START;
    uint32_t dataPC = DATA_START;

    for (int i = 0; i < (int)lines.size(); i++) {
        string s = lines[i];

        size_t comment = s.find('#');

        if (comment != string::npos) {
            s = s.substr(0, comment);
        }

        s = trim(s);

        if (s.empty()) continue;

        // Read labels, including labels followed by instructions.
        while (true) {
            size_t colon = s.find(':');

            if (colon == string::npos) break;

            size_t space = s.find_first_of(" \t");

            if (space != string::npos && space < colon) break;

            string name = trim(s.substr(0, colon));

            if (name.empty()) {
                error(i + 1, "Empty label");
            }

            uint32_t address = dataSection ? dataPC : textPC;

            if (labels.count(name)) {
                error(i + 1, "Duplicate label: " + name);
            }

            labels[name] = address;
            s = trim(s.substr(colon + 1));

            if (s.empty()) break;
        }

        if (s.empty()) continue;

        if (s == ".text") {
            dataSection = false;
            continue;
        }

        if (s == ".data") {
            dataSection = true;
            continue;
        }

        if (s.rfind(".globl ", 0) == 0 ||
            s.rfind(".global ", 0) == 0) {
            continue;
        }

        if (dataSection) {
            if (s.rfind(".asciiz ", 0) != 0) {
                error(i + 1, "Unsupported data directive: " + s);
            }

            string decoded = decodeString(s.substr(8), i + 1);

            items.push_back({i + 1, true, dataPC, s});
            dataPC += uint32_t(decoded.size() + 1);
        } else {
            items.push_back({i + 1, false, textPC, s});
            textPC += 4;
        }
    }
}

void secondPass() {
    for (const Item& item : items) {
        if (item.data) {
            string decoded =
                decodeString(item.text.substr(8), item.line);

            for (unsigned char c : decoded) {
                dataBytes.push_back(c);
            }

            // Null terminator for .asciiz.
            dataBytes.push_back(0);
        } else {
            instructions.push_back(
                encode(item.text, item.address, item.line)
            );
        }
    }
}

void writeOutputs() {
    ofstream ascii("fizzbuzz.mhc-ascii");
    ofstream binary("fizzbuzz.mhc-bin", ios::binary);

    if (!ascii || !binary) {
        throw runtime_error("Could not create output files");
    }

    // Write instruction records and big-endian machine code.
    for (size_t i = 0; i < instructions.size(); i++) {
        uint32_t word = instructions[i];
        uint32_t address = TEXT_START + uint32_t(i * 4);

        ascii << "I " << hex << uppercase << setw(8)
              << setfill('0') << address << " "
              << bitset<32>(word) << "\n";

        binary.put(char((word >> 24) & 0xFF));
        binary.put(char((word >> 16) & 0xFF));
        binary.put(char((word >> 8) & 0xFF));
        binary.put(char(word & 0xFF));
    }

    // Write data records.
    for (size_t i = 0; i < dataBytes.size(); i++) {
        uint32_t address = DATA_START + uint32_t(i);
        unsigned char b = dataBytes[i];

        ascii << "D " << hex << uppercase << setw(8)
              << setfill('0') << address << " "
              << bitset<8>(b) << "\n";

        binary.put(char(b));
    }

    if (!ascii || !binary) {
        throw runtime_error("Error writing output files");
    }
}

int main(int argc, char* argv[]) {
    initRegisters();

    if (argc != 2) {
        cerr << "Usage: MIPSAssembler fizzbuzz.asm\n";
        return 1;
    }

    try {
        ifstream input(argv[1]);

        if (!input) {
            throw runtime_error(
                "Cannot open source file: " + string(argv[1])
            );
        }

        vector<string> lines;
        string line;

        while (getline(input, line)) {
            lines.push_back(line);
        }

        firstPass(lines);
        secondPass();
        writeOutputs();

        cout << "Assembly successful.\n";
        cout << "Instructions: " << instructions.size() << "\n";
        cout << "Data bytes: " << dataBytes.size() << "\n";
        cout << "Created fizzbuzz.mhc-ascii\n";
        cout << "Created fizzbuzz.mhc-bin\n";

    } catch (const exception& e) {
        cerr << "Assembler error: " << e.what() << "\n";
        return 1;
    }

    return 0;
}
