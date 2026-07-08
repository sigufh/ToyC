#include <cctype>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <map>
#include <memory>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

using namespace std;

struct Token {
    string text;
};

class Lexer {
public:
    explicit Lexer(string input) : src(std::move(input)) {}

    vector<Token> scan() {
        vector<Token> out;
        while (pos < src.size()) {
            skipIgnored();
            if (pos >= src.size()) break;
            char c = src[pos];
            if (isalpha(static_cast<unsigned char>(c)) || c == '_') {
                size_t start = pos++;
                while (pos < src.size() &&
                       (isalnum(static_cast<unsigned char>(src[pos])) || src[pos] == '_')) {
                    ++pos;
                }
                out.push_back({src.substr(start, pos - start)});
            } else if (isdigit(static_cast<unsigned char>(c))) {
                size_t start = pos++;
                while (pos < src.size() && isdigit(static_cast<unsigned char>(src[pos]))) ++pos;
                out.push_back({src.substr(start, pos - start)});
            } else {
                string two = pos + 1 < src.size() ? src.substr(pos, 2) : "";
                if (two == "&&" || two == "||" || two == "<=" || two == ">=" ||
                    two == "==" || two == "!=") {
                    out.push_back({two});
                    pos += 2;
                } else {
                    out.push_back({string(1, c)});
                    ++pos;
                }
            }
        }
        out.push_back({"<eof>"});
        return out;
    }

private:
    string src;
    size_t pos = 0;

    void skipIgnored() {
        while (pos < src.size()) {
            if (isspace(static_cast<unsigned char>(src[pos]))) {
                ++pos;
                continue;
            }
            if (pos + 1 < src.size() && src[pos] == '/' && src[pos + 1] == '/') {
                pos += 2;
                while (pos < src.size() && src[pos] != '\n') ++pos;
                continue;
            }
            if (pos + 1 < src.size() && src[pos] == '/' && src[pos + 1] == '*') {
                pos += 2;
                while (pos + 1 < src.size() && !(src[pos] == '*' && src[pos + 1] == '/')) ++pos;
                if (pos + 1 < src.size()) pos += 2;
                continue;
            }
            break;
        }
    }
};

struct Expr {
    enum Kind { Number, Var, Call, Unary, Binary } kind;
    int32_t value = 0;
    string name;
    string op;
    vector<unique_ptr<Expr>> args;
    unique_ptr<Expr> lhs;
    unique_ptr<Expr> rhs;
};

struct Decl {
    bool isConst = false;
    string name;
    unique_ptr<Expr> init;
    int offset = 0;
};

struct Stmt {
    enum Kind { Block, Empty, ExprStmt, Assign, DeclStmt, If, While, Break, Continue, Return } kind;
    vector<unique_ptr<Stmt>> stmts;
    unique_ptr<Expr> expr;
    unique_ptr<Expr> rhs;
    unique_ptr<Stmt> thenStmt;
    unique_ptr<Stmt> elseStmt;
    unique_ptr<Decl> decl;
    string name;
};

struct Func {
    bool returnsVoid = false;
    string name;
    vector<string> params;
    unique_ptr<Stmt> body;
    map<string, int> paramOffsets;
    int frameSize = 16;
};

struct Program {
    vector<unique_ptr<Decl>> globals;
    vector<unique_ptr<Func>> funcs;
};

static bool isNumberText(const string& s) {
    if (s.empty()) return false;
    for (char c : s) {
        if (!isdigit(static_cast<unsigned char>(c))) return false;
    }
    return true;
}

class Parser {
public:
    explicit Parser(vector<Token> tokens) : toks(std::move(tokens)) {}

    Program parseProgram() {
        Program p;
        while (!at("<eof>")) {
            if (match("const")) {
                expect("int");
                p.globals.push_back(parseDeclRest(true));
                continue;
            }
            bool isVoid = false;
            if (match("void")) {
                isVoid = true;
            } else {
                expect("int");
            }
            string name = expectId();
            if (match("(")) {
                auto fn = make_unique<Func>();
                fn->returnsVoid = isVoid;
                fn->name = name;
                if (!match(")")) {
                    do {
                        expect("int");
                        fn->params.push_back(expectId());
                    } while (match(","));
                    expect(")");
                }
                fn->body = parseBlock();
                p.funcs.push_back(std::move(fn));
            } else {
                if (isVoid) fail("void variable declaration");
                p.globals.push_back(parseDeclRestAfterName(false, name));
            }
        }
        return p;
    }

private:
    vector<Token> toks;
    size_t pos = 0;

    const string& peek(size_t off = 0) const { return toks[pos + off].text; }
    bool at(const string& s) const { return peek() == s; }
    bool match(const string& s) {
        if (!at(s)) return false;
        ++pos;
        return true;
    }
    void expect(const string& s) {
        if (!match(s)) fail("expected `" + s + "`, got `" + peek() + "`");
    }
    [[noreturn]] void fail(const string& msg) const { throw runtime_error("parse error: " + msg); }

    string expectId() {
        string s = peek();
        if (s == "<eof>" || isNumberText(s) || isKeyword(s) || isPunct(s)) {
            fail("expected identifier, got `" + s + "`");
        }
        ++pos;
        return s;
    }

    static bool isKeyword(const string& s) {
        return s == "const" || s == "int" || s == "void" || s == "if" || s == "else" ||
               s == "while" || s == "break" || s == "continue" || s == "return";
    }

    static bool isPunct(const string& s) {
        static const vector<string> punct = {
            "(", ")", "{", "}", ";", ",", "=", "+", "-", "*", "/", "%", "!",
            "<", ">", "<=", ">=", "==", "!=", "&&", "||"};
        for (const auto& p : punct) {
            if (s == p) return true;
        }
        return false;
    }

    unique_ptr<Decl> parseDeclRest(bool isConst) {
        string name = expectId();
        return parseDeclRestAfterName(isConst, name);
    }

    unique_ptr<Decl> parseDeclRestAfterName(bool isConst, string name) {
        auto d = make_unique<Decl>();
        d->isConst = isConst;
        d->name = std::move(name);
        expect("=");
        d->init = parseExpr();
        expect(";");
        return d;
    }

    unique_ptr<Stmt> parseBlock() {
        expect("{");
        auto s = make_unique<Stmt>();
        s->kind = Stmt::Block;
        while (!match("}")) {
            s->stmts.push_back(parseStmt());
        }
        return s;
    }

    unique_ptr<Stmt> parseStmt() {
        if (at("{")) return parseBlock();
        if (match(";")) {
            auto s = make_unique<Stmt>();
            s->kind = Stmt::Empty;
            return s;
        }
        if (match("const")) {
            expect("int");
            auto s = make_unique<Stmt>();
            s->kind = Stmt::DeclStmt;
            s->decl = parseDeclRest(true);
            return s;
        }
        if (match("int")) {
            auto s = make_unique<Stmt>();
            s->kind = Stmt::DeclStmt;
            s->decl = parseDeclRest(false);
            return s;
        }
        if (match("if")) {
            auto s = make_unique<Stmt>();
            s->kind = Stmt::If;
            expect("(");
            s->expr = parseExpr();
            expect(")");
            s->thenStmt = parseStmt();
            if (match("else")) s->elseStmt = parseStmt();
            return s;
        }
        if (match("while")) {
            auto s = make_unique<Stmt>();
            s->kind = Stmt::While;
            expect("(");
            s->expr = parseExpr();
            expect(")");
            s->thenStmt = parseStmt();
            return s;
        }
        if (match("break")) {
            expect(";");
            auto s = make_unique<Stmt>();
            s->kind = Stmt::Break;
            return s;
        }
        if (match("continue")) {
            expect(";");
            auto s = make_unique<Stmt>();
            s->kind = Stmt::Continue;
            return s;
        }
        if (match("return")) {
            auto s = make_unique<Stmt>();
            s->kind = Stmt::Return;
            if (!match(";")) {
                s->expr = parseExpr();
                expect(";");
            }
            return s;
        }
        if (!isKeyword(peek()) && !isNumberText(peek()) && peek(1) == "=") {
            auto s = make_unique<Stmt>();
            s->kind = Stmt::Assign;
            s->name = expectId();
            expect("=");
            s->rhs = parseExpr();
            expect(";");
            return s;
        }
        auto s = make_unique<Stmt>();
        s->kind = Stmt::ExprStmt;
        s->expr = parseExpr();
        expect(";");
        return s;
    }

    unique_ptr<Expr> parseExpr() { return parseLOr(); }

    unique_ptr<Expr> parseLOr() {
        auto e = parseLAnd();
        while (match("||")) e = binary("||", std::move(e), parseLAnd());
        return e;
    }

    unique_ptr<Expr> parseLAnd() {
        auto e = parseRel();
        while (match("&&")) e = binary("&&", std::move(e), parseRel());
        return e;
    }

    unique_ptr<Expr> parseRel() {
        auto e = parseAdd();
        while (at("<") || at(">") || at("<=") || at(">=") || at("==") || at("!=")) {
            string op = peek();
            ++pos;
            e = binary(op, std::move(e), parseAdd());
        }
        return e;
    }

    unique_ptr<Expr> parseAdd() {
        auto e = parseMul();
        while (at("+") || at("-")) {
            string op = peek();
            ++pos;
            e = binary(op, std::move(e), parseMul());
        }
        return e;
    }

    unique_ptr<Expr> parseMul() {
        auto e = parseUnary();
        while (at("*") || at("/") || at("%")) {
            string op = peek();
            ++pos;
            e = binary(op, std::move(e), parseUnary());
        }
        return e;
    }

    unique_ptr<Expr> parseUnary() {
        if (at("+") || at("-") || at("!")) {
            string op = peek();
            ++pos;
            auto e = make_unique<Expr>();
            e->kind = Expr::Unary;
            e->op = op;
            e->lhs = parseUnary();
            return e;
        }
        return parsePrimary();
    }

    unique_ptr<Expr> parsePrimary() {
        if (match("(")) {
            auto e = parseExpr();
            expect(")");
            return e;
        }
        if (isNumberText(peek())) {
            auto e = make_unique<Expr>();
            e->kind = Expr::Number;
            e->value = static_cast<int32_t>(stoll(peek()));
            ++pos;
            return e;
        }
        string name = expectId();
        if (match("(")) {
            auto e = make_unique<Expr>();
            e->kind = Expr::Call;
            e->name = name;
            if (!match(")")) {
                do {
                    e->args.push_back(parseExpr());
                } while (match(","));
                expect(")");
            }
            return e;
        }
        auto e = make_unique<Expr>();
        e->kind = Expr::Var;
        e->name = name;
        return e;
    }

    unique_ptr<Expr> binary(string op, unique_ptr<Expr> lhs, unique_ptr<Expr> rhs) {
        auto e = make_unique<Expr>();
        e->kind = Expr::Binary;
        e->op = std::move(op);
        e->lhs = std::move(lhs);
        e->rhs = std::move(rhs);
        return e;
    }
};

struct Symbol {
    bool isConst = false;
    bool isGlobal = false;
    int32_t constValue = 0;
    int offset = 0;
    string label;
};

static int alignTo(int value, int align) {
    return (value + align - 1) / align * align;
}

class CodeGen {
public:
    explicit CodeGen(Program& program, bool optimize) : prog(program), optimize(optimize) {}

    string emit() {
        prepareGlobals();
        prepareFunctions();

        out << "    .option nopic\n";
        emitData();
        out << "    .text\n";
        for (auto& fn : prog.funcs) emitFunction(*fn);
        return out.str();
    }

private:
    Program& prog;
    bool optimize = false;
    ostringstream out;
    map<string, Symbol> globals;
    vector<map<string, Symbol>> scopes;
    vector<pair<string, string>> loopLabels;
    string currentEndLabel;
    int labelId = 0;
    int slotCount = 0;

    string newLabel(const string& base) {
        return ".L" + base + "_" + to_string(labelId++);
    }

    static string mem(int offset, const string& base = "s0") {
        return to_string(offset) + "(" + base + ")";
    }

    void prepareGlobals() {
        scopes.clear();
        for (auto& d : prog.globals) {
            Symbol sym;
            sym.isConst = d->isConst;
            sym.isGlobal = true;
            sym.label = d->name;
            if (auto v = evalConst(*d->init)) sym.constValue = *v;
            globals[d->name] = sym;
        }
    }

    void prepareFunctions() {
        for (auto& fn : prog.funcs) {
            slotCount = 0;
            for (const string& p : fn->params) {
                int off = allocSlot();
                fn->paramOffsets[p] = off;
            }
            assignOffsets(*fn->body);
            fn->frameSize = alignTo(slotCount * 4 + 8, 16);
        }
    }

    int allocSlot() {
        int offset = -12 - slotCount * 4;
        ++slotCount;
        return offset;
    }

    void assignOffsets(Stmt& s) {
        switch (s.kind) {
            case Stmt::Block:
                for (auto& child : s.stmts) assignOffsets(*child);
                break;
            case Stmt::DeclStmt:
                if (!s.decl->isConst) s.decl->offset = allocSlot();
                break;
            case Stmt::If:
                assignOffsets(*s.thenStmt);
                if (s.elseStmt) assignOffsets(*s.elseStmt);
                break;
            case Stmt::While:
                assignOffsets(*s.thenStmt);
                break;
            default:
                break;
        }
    }

    void emitData() {
        bool hasData = false;
        for (auto& d : prog.globals) {
            if (d->isConst) continue;
            if (!hasData) {
                out << "    .data\n";
                hasData = true;
            }
            int32_t init = evalConst(*d->init).value_or(0);
            out << "    .globl " << d->name << "\n";
            out << "    .align 2\n";
            out << d->name << ":\n";
            out << "    .word " << init << "\n";
        }
    }

    void emitFunction(Func& fn) {
        scopes.clear();
        scopes.emplace_back();
        for (const string& p : fn.params) {
            scopes.back()[p] = Symbol{false, false, 0, fn.paramOffsets[p], ""};
        }

        currentEndLabel = newLabel(fn.name + "_end");
        out << "    .globl " << fn.name << "\n";
        out << fn.name << ":\n";
        out << "    addi sp, sp, -" << fn.frameSize << "\n";
        out << "    sw ra, " << fn.frameSize - 4 << "(sp)\n";
        out << "    sw s0, " << fn.frameSize - 8 << "(sp)\n";
        out << "    addi s0, sp, " << fn.frameSize << "\n";
        for (size_t i = 0; i < fn.params.size(); ++i) {
            int off = fn.paramOffsets[fn.params[i]];
            if (i < 8) {
                out << "    sw a" << i << ", " << mem(off) << "\n";
            } else {
                out << "    lw t0, " << (static_cast<int>(i) - 8) * 4 << "(s0)\n";
                out << "    sw t0, " << mem(off) << "\n";
            }
        }
        emitBlock(*fn.body);
        if (fn.returnsVoid) out << "    li a0, 0\n";
        out << currentEndLabel << ":\n";
        out << "    lw ra, " << fn.frameSize - 4 << "(sp)\n";
        out << "    lw s0, " << fn.frameSize - 8 << "(sp)\n";
        out << "    addi sp, sp, " << fn.frameSize << "\n";
        out << "    ret\n";
    }

    void emitBlock(Stmt& s) {
        scopes.emplace_back();
        for (auto& child : s.stmts) emitStmt(*child);
        scopes.pop_back();
    }

    void emitStmt(Stmt& s) {
        switch (s.kind) {
            case Stmt::Block:
                emitBlock(s);
                break;
            case Stmt::Empty:
                break;
            case Stmt::ExprStmt:
                emitExpr(*s.expr);
                break;
            case Stmt::Assign: {
                emitExpr(*s.rhs);
                Symbol sym = lookup(s.name);
                if (sym.isGlobal) {
                    out << "    la t0, " << sym.label << "\n";
                    out << "    sw a0, 0(t0)\n";
                } else {
                    out << "    sw a0, " << mem(sym.offset) << "\n";
                }
                break;
            }
            case Stmt::DeclStmt:
                emitDecl(*s.decl);
                break;
            case Stmt::If:
                emitIf(s);
                break;
            case Stmt::While:
                emitWhile(s);
                break;
            case Stmt::Break:
                out << "    j " << loopLabels.back().second << "\n";
                break;
            case Stmt::Continue:
                out << "    j " << loopLabels.back().first << "\n";
                break;
            case Stmt::Return:
                if (s.expr) emitExpr(*s.expr);
                else out << "    li a0, 0\n";
                out << "    j " << currentEndLabel << "\n";
                break;
        }
    }

    void emitDecl(Decl& d) {
        if (d.isConst) {
            int32_t val = evalConst(*d.init).value_or(0);
            scopes.back()[d.name] = Symbol{true, false, val, 0, ""};
            return;
        }
        emitExpr(*d.init);
        out << "    sw a0, " << mem(d.offset) << "\n";
        scopes.back()[d.name] = Symbol{false, false, 0, d.offset, ""};
    }

    void emitIf(Stmt& s) {
        if (optimize) {
            if (auto cond = evalConst(*s.expr)) {
                if (*cond) {
                    emitStmt(*s.thenStmt);
                } else if (s.elseStmt) {
                    emitStmt(*s.elseStmt);
                }
                return;
            }
        }
        string elseLabel = newLabel("else");
        string endLabel = newLabel("ifend");
        emitExpr(*s.expr);
        out << "    beqz a0, " << elseLabel << "\n";
        emitStmt(*s.thenStmt);
        out << "    j " << endLabel << "\n";
        out << elseLabel << ":\n";
        if (s.elseStmt) emitStmt(*s.elseStmt);
        out << endLabel << ":\n";
    }

    void emitWhile(Stmt& s) {
        if (optimize) {
            if (auto cond = evalConst(*s.expr); cond && *cond == 0) {
                return;
            }
        }
        string begin = newLabel("while_begin");
        string end = newLabel("while_end");
        loopLabels.push_back({begin, end});
        out << begin << ":\n";
        emitExpr(*s.expr);
        out << "    beqz a0, " << end << "\n";
        emitStmt(*s.thenStmt);
        out << "    j " << begin << "\n";
        out << end << ":\n";
        loopLabels.pop_back();
    }

    void emitExpr(Expr& e) {
        if (optimize) {
            if (auto v = evalConst(e)) {
                out << "    li a0, " << *v << "\n";
                return;
            }
        }
        switch (e.kind) {
            case Expr::Number:
                out << "    li a0, " << e.value << "\n";
                break;
            case Expr::Var:
                emitLoadVar(e.name);
                break;
            case Expr::Call:
                emitCall(e);
                break;
            case Expr::Unary:
                emitUnary(e);
                break;
            case Expr::Binary:
                emitBinary(e);
                break;
        }
    }

    void emitLoadVar(const string& name) {
        Symbol sym = lookup(name);
        if (sym.isConst) {
            out << "    li a0, " << sym.constValue << "\n";
        } else if (sym.isGlobal) {
            out << "    la t0, " << sym.label << "\n";
            out << "    lw a0, 0(t0)\n";
        } else {
            out << "    lw a0, " << mem(sym.offset) << "\n";
        }
    }

    void emitUnary(Expr& e) {
        if (optimize && e.op == "+") {
            emitExpr(*e.lhs);
            return;
        }
        emitExpr(*e.lhs);
        if (e.op == "-") out << "    neg a0, a0\n";
        else if (e.op == "!") out << "    seqz a0, a0\n";
    }

    void emitBinary(Expr& e) {
        if (optimize && emitAlgebraic(e)) return;
        if (e.op == "&&") {
            string falseLabel = newLabel("land_false");
            string endLabel = newLabel("land_end");
            emitExpr(*e.lhs);
            out << "    beqz a0, " << falseLabel << "\n";
            emitExpr(*e.rhs);
            out << "    snez a0, a0\n";
            out << "    j " << endLabel << "\n";
            out << falseLabel << ":\n";
            out << "    li a0, 0\n";
            out << endLabel << ":\n";
            return;
        }
        if (e.op == "||") {
            string trueLabel = newLabel("lor_true");
            string endLabel = newLabel("lor_end");
            emitExpr(*e.lhs);
            out << "    bnez a0, " << trueLabel << "\n";
            emitExpr(*e.rhs);
            out << "    snez a0, a0\n";
            out << "    j " << endLabel << "\n";
            out << trueLabel << ":\n";
            out << "    li a0, 1\n";
            out << endLabel << ":\n";
            return;
        }

        emitExpr(*e.lhs);
        out << "    addi sp, sp, -4\n";
        out << "    sw a0, 0(sp)\n";
        emitExpr(*e.rhs);
        out << "    lw t0, 0(sp)\n";
        out << "    addi sp, sp, 4\n";
        if (e.op == "+") out << "    add a0, t0, a0\n";
        else if (e.op == "-") out << "    sub a0, t0, a0\n";
        else if (e.op == "*") out << "    mul a0, t0, a0\n";
        else if (e.op == "/") out << "    div a0, t0, a0\n";
        else if (e.op == "%") out << "    rem a0, t0, a0\n";
        else if (e.op == "<") out << "    slt a0, t0, a0\n";
        else if (e.op == ">") out << "    slt a0, a0, t0\n";
        else if (e.op == "<=") {
            out << "    slt a0, a0, t0\n";
            out << "    xori a0, a0, 1\n";
        } else if (e.op == ">=") {
            out << "    slt a0, t0, a0\n";
            out << "    xori a0, a0, 1\n";
        } else if (e.op == "==") {
            out << "    xor a0, t0, a0\n";
            out << "    seqz a0, a0\n";
        } else if (e.op == "!=") {
            out << "    xor a0, t0, a0\n";
            out << "    snez a0, a0\n";
        }
    }

    bool emitAlgebraic(Expr& e) {
        auto l = evalConst(*e.lhs);
        auto r = evalConst(*e.rhs);
        if (e.op == "+" && r && *r == 0) {
            emitExpr(*e.lhs);
            return true;
        }
        if (e.op == "+" && l && *l == 0) {
            emitExpr(*e.rhs);
            return true;
        }
        if (e.op == "-" && r && *r == 0) {
            emitExpr(*e.lhs);
            return true;
        }
        if (e.op == "*" && r && *r == 1) {
            emitExpr(*e.lhs);
            return true;
        }
        if (e.op == "*" && l && *l == 1) {
            emitExpr(*e.rhs);
            return true;
        }
        if (e.op == "/" && r && *r == 1) {
            emitExpr(*e.lhs);
            return true;
        }
        if ((e.op == "&&" || e.op == "||") && l) {
            if (e.op == "&&" && *l == 0) {
                out << "    li a0, 0\n";
                return true;
            }
            if (e.op == "||" && *l != 0) {
                out << "    li a0, 1\n";
                return true;
            }
        }
        return false;
    }

    void emitCall(Expr& e) {
        int n = static_cast<int>(e.args.size());
        for (auto& arg : e.args) {
            emitExpr(*arg);
            out << "    addi sp, sp, -4\n";
            out << "    sw a0, 0(sp)\n";
        }
        if (n > 0) out << "    mv t2, sp\n";
        int regArgs = min(n, 8);
        for (int i = 0; i < regArgs; ++i) {
            int off = (n - 1 - i) * 4;
            out << "    lw a" << i << ", " << off << "(t2)\n";
        }
        int stackArgs = max(0, n - 8);
        if (stackArgs > 0) {
            for (int j = n - 1; j >= 8; --j) {
                int off = (n - 1 - j) * 4;
                out << "    lw t0, " << off << "(t2)\n";
                out << "    addi sp, sp, -4\n";
                out << "    sw t0, 0(sp)\n";
            }
        }
        out << "    call " << e.name << "\n";
        int bytesToPop = n * 4 + stackArgs * 4;
        if (bytesToPop > 0) out << "    addi sp, sp, " << bytesToPop << "\n";
    }

    Symbol lookup(const string& name) const {
        for (auto it = scopes.rbegin(); it != scopes.rend(); ++it) {
            auto found = it->find(name);
            if (found != it->end()) return found->second;
        }
        auto g = globals.find(name);
        if (g != globals.end()) return g->second;
        throw runtime_error("unknown symbol: " + name);
    }

    optional<int32_t> evalConst(Expr& e) {
        switch (e.kind) {
            case Expr::Number:
                return e.value;
            case Expr::Var: {
                Symbol sym = lookupConstScope(e.name);
                if (sym.isConst) return sym.constValue;
                return nullopt;
            }
            case Expr::Call:
                return nullopt;
            case Expr::Unary: {
                auto v = evalConst(*e.lhs);
                if (!v) return nullopt;
                if (e.op == "+") return *v;
                if (e.op == "-") return static_cast<int32_t>(-*v);
                if (e.op == "!") return static_cast<int32_t>(*v == 0);
                return nullopt;
            }
            case Expr::Binary: {
                auto l = evalConst(*e.lhs);
                auto r = evalConst(*e.rhs);
                if (!l || !r) return nullopt;
                int32_t a = *l, b = *r;
                if (e.op == "+") return static_cast<int32_t>(a + b);
                if (e.op == "-") return static_cast<int32_t>(a - b);
                if (e.op == "*") return static_cast<int32_t>(a * b);
                if (e.op == "/") return static_cast<int32_t>(a / b);
                if (e.op == "%") return static_cast<int32_t>(a % b);
                if (e.op == "<") return static_cast<int32_t>(a < b);
                if (e.op == ">") return static_cast<int32_t>(a > b);
                if (e.op == "<=") return static_cast<int32_t>(a <= b);
                if (e.op == ">=") return static_cast<int32_t>(a >= b);
                if (e.op == "==") return static_cast<int32_t>(a == b);
                if (e.op == "!=") return static_cast<int32_t>(a != b);
                if (e.op == "&&") return static_cast<int32_t>((a != 0) && (b != 0));
                if (e.op == "||") return static_cast<int32_t>((a != 0) || (b != 0));
                return nullopt;
            }
        }
        return nullopt;
    }

    Symbol lookupConstScope(const string& name) const {
        for (auto it = scopes.rbegin(); it != scopes.rend(); ++it) {
            auto found = it->find(name);
            if (found != it->end()) return found->second;
        }
        auto g = globals.find(name);
        if (g != globals.end()) return g->second;
        return {};
    }
};

int main(int argc, char** argv) {
    ios::sync_with_stdio(false);
    cin.tie(nullptr);

    bool optimize = false;
    for (int i = 1; i < argc; ++i) {
        if (string(argv[i]) == "-opt") optimize = true;
    }

    string input((istreambuf_iterator<char>(cin)), istreambuf_iterator<char>());
    try {
        Lexer lexer(input);
        Parser parser(lexer.scan());
        Program program = parser.parseProgram();
        CodeGen codegen(program, optimize);
        cout << codegen.emit();
    } catch (const exception& ex) {
        cerr << ex.what() << '\n';
        return 1;
    }
    return 0;
}
