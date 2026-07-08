#include "compiler.hpp"
#include <cctype>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <map>
#include <memory>
#include <optional>
#include <set>
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
    string reg;
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
    map<string, string> paramRegs;
    vector<string> savedRegs;
    bool hasCall = false;
    bool savesRa = false;
    bool usesFramePointer = true;
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
    string reg;
};

static int alignTo(int value, int align) {
    return (value + align - 1) / align * align;
}

static unique_ptr<Expr> makeNumber(int32_t value) {
    auto e = make_unique<Expr>();
    e->kind = Expr::Number;
    e->value = value;
    return e;
}

static unique_ptr<Expr> makeVar(string name) {
    auto e = make_unique<Expr>();
    e->kind = Expr::Var;
    e->name = std::move(name);
    return e;
}

static bool exprHasCall(const Expr& e) {
    if (e.kind == Expr::Call) return true;
    if (e.lhs && exprHasCall(*e.lhs)) return true;
    if (e.rhs && exprHasCall(*e.rhs)) return true;
    for (const auto& arg : e.args) {
        if (exprHasCall(*arg)) return true;
    }
    return false;
}

static bool exprSame(const Expr& a, const Expr& b) {
    if (a.kind != b.kind || a.value != b.value || a.name != b.name || a.op != b.op ||
        a.args.size() != b.args.size()) {
        return false;
    }
    for (size_t i = 0; i < a.args.size(); ++i) {
        if (!exprSame(*a.args[i], *b.args[i])) return false;
    }
    if (static_cast<bool>(a.lhs) != static_cast<bool>(b.lhs)) return false;
    if (static_cast<bool>(a.rhs) != static_cast<bool>(b.rhs)) return false;
    if (a.lhs && !exprSame(*a.lhs, *b.lhs)) return false;
    if (a.rhs && !exprSame(*a.rhs, *b.rhs)) return false;
    return true;
}

class Optimizer {
public:
    explicit Optimizer(Program& program) : prog(program) {}

    void run() {
        prepareGlobalConsts();
        for (auto& fn : prog.funcs) {
            optimizeFunction(*fn);
            set<string> live;
            removeDeadStores(*fn->body, live);
        }
    }

private:
    struct Known {
        enum Kind { Const, Copy } kind = Const;
        int32_t value = 0;
        string name;
    };

    Program& prog;
    vector<map<string, Known>> env;
    set<string> globalNames;

    void prepareGlobalConsts() {
        env.clear();
        globalNames.clear();
        env.emplace_back();
        for (auto& d : prog.globals) {
            globalNames.insert(d->name);
            optimizeExpr(d->init);
            if (auto v = evalKnownConst(*d->init)) {
                Known k;
                k.kind = Known::Const;
                k.value = *v;
                if (d->isConst) env.back()[d->name] = k;
            }
        }
    }

    void optimizeFunction(Func& fn) {
        env.clear();
        env.emplace_back();
        for (auto& d : prog.globals) {
            if (!d->isConst) continue;
            if (auto v = evalKnownConst(*d->init)) {
                Known k;
                k.kind = Known::Const;
                k.value = *v;
                env.back()[d->name] = k;
            }
        }
        for (const string& p : fn.params) forgetName(p);
        optimizeStmt(fn.body);
    }

    void optimizeBlock(Stmt& block) {
        env.emplace_back();
        vector<unique_ptr<Stmt>> kept;
        kept.reserve(block.stmts.size());
        bool terminated = false;
        for (auto& child : block.stmts) {
            if (terminated) continue;
            optimizeStmt(child);
            if (child->kind == Stmt::Empty) continue;
            terminated = stmtAlwaysTerminates(*child);
            kept.push_back(std::move(child));
        }
        block.stmts = std::move(kept);
        env.pop_back();
    }

    void optimizeStmt(unique_ptr<Stmt>& s) {
        switch (s->kind) {
            case Stmt::Block:
                optimizeBlock(*s);
                break;
            case Stmt::ExprStmt:
                optimizeExpr(s->expr);
                if (!exprHasCall(*s->expr)) s->kind = Stmt::Empty;
                break;
            case Stmt::DeclStmt:
                optimizeDecl(*s->decl);
                break;
            case Stmt::Assign:
                optimizeExpr(s->rhs);
                assignKnown(s->name, *s->rhs);
                break;
            case Stmt::If:
                optimizeIf(s);
                break;
            case Stmt::While:
                optimizeWhile(s);
                break;
            case Stmt::Return:
                if (s->expr) optimizeExpr(s->expr);
                break;
            default:
                break;
        }
    }

    void optimizeDecl(Decl& d) {
        optimizeExpr(d.init);
        forgetAliasesTo(d.name);
        if (d.isConst) {
            if (auto v = evalKnownConst(*d.init)) {
                Known k;
                k.kind = Known::Const;
                k.value = *v;
                env.back()[d.name] = k;
            }
            return;
        }
        assignKnownInCurrentScope(d.name, *d.init);
    }

    void optimizeIf(unique_ptr<Stmt>& s) {
        optimizeExpr(s->expr);
        if (auto cond = evalKnownConst(*s->expr)) {
            if (*cond) {
                optimizeStmt(s->thenStmt);
                s = std::move(s->thenStmt);
            } else if (s->elseStmt) {
                optimizeStmt(s->elseStmt);
                s = std::move(s->elseStmt);
            } else {
                s->kind = Stmt::Empty;
            }
            return;
        }

        auto before = env;
        optimizeStmt(s->thenStmt);
        auto thenEnv = env;
        env = before;
        if (s->elseStmt) optimizeStmt(s->elseStmt);
        auto elseEnv = env;
        env = mergeEnv(thenEnv, elseEnv);
    }

    void optimizeWhile(unique_ptr<Stmt>& s) {
        set<string> assigned;
        collectAssigned(*s->thenStmt, assigned);
        for (const string& name : assigned) forgetName(name);
        optimizeExpr(s->expr);
        if (auto cond = evalKnownConst(*s->expr); cond && *cond == 0) {
            s->kind = Stmt::Empty;
            return;
        }
        optimizeStmt(s->thenStmt);
        for (const string& name : assigned) forgetName(name);
    }

    void optimizeExpr(unique_ptr<Expr>& e) {
        if (!e) return;
        switch (e->kind) {
            case Expr::Number:
                return;
            case Expr::Var: {
                if (auto known = lookupKnown(e->name)) {
                    if (known->kind == Known::Const) {
                        e = makeNumber(known->value);
                    } else {
                        e = makeVar(known->name);
                    }
                }
                return;
            }
            case Expr::Call:
                for (auto& arg : e->args) optimizeExpr(arg);
                return;
            case Expr::Unary:
                optimizeExpr(e->lhs);
                foldUnary(e);
                return;
            case Expr::Binary:
                optimizeExpr(e->lhs);
                optimizeExpr(e->rhs);
                foldBinary(e);
                return;
        }
    }

    void foldUnary(unique_ptr<Expr>& e) {
        auto v = evalKnownConst(*e->lhs);
        if (!v) {
            if (e->op == "+") e = std::move(e->lhs);
            return;
        }
        if (e->op == "+") e = makeNumber(*v);
        else if (e->op == "-") e = makeNumber(static_cast<int32_t>(-*v));
        else if (e->op == "!") e = makeNumber(static_cast<int32_t>(*v == 0));
    }

    void foldBinary(unique_ptr<Expr>& e) {
        auto l = evalKnownConst(*e->lhs);
        auto r = evalKnownConst(*e->rhs);
        const string op = e->op;
        if (op == "&&") {
            if (l && *l == 0) {
                e = makeNumber(0);
                return;
            }
            if (l && *l != 0) {
                e = std::move(e->rhs);
                normalizeBool(e);
                return;
            }
            if (r && *r != 0 && !exprHasCall(*e->rhs)) {
                e = std::move(e->lhs);
                normalizeBool(e);
                return;
            }
        }
        if (op == "||") {
            if (l && *l != 0) {
                e = makeNumber(1);
                return;
            }
            if (l && *l == 0) {
                e = std::move(e->rhs);
                normalizeBool(e);
                return;
            }
            if (r && *r == 0 && !exprHasCall(*e->rhs)) {
                e = std::move(e->lhs);
                normalizeBool(e);
                return;
            }
        }
        if (l && r) {
            if (auto v = evalKnownConst(*e)) e = makeNumber(*v);
            return;
        }
        if (op == "+" && r && *r == 0) e = std::move(e->lhs);
        else if (op == "+" && l && *l == 0) e = std::move(e->rhs);
        else if (op == "-" && r && *r == 0) e = std::move(e->lhs);
        else if (op == "*" && r && *r == 1) e = std::move(e->lhs);
        else if (op == "*" && l && *l == 1) e = std::move(e->rhs);
        else if (op == "*" && r && *r == 0 && !exprHasCall(*e->lhs)) e = makeNumber(0);
        else if (op == "*" && l && *l == 0 && !exprHasCall(*e->rhs)) e = makeNumber(0);
        else if (op == "/" && r && *r == 1) e = std::move(e->lhs);
        else if (op == "%" && r && *r == 1 && !exprHasCall(*e->lhs)) e = makeNumber(0);
        else if (!exprHasCall(*e->lhs) && !exprHasCall(*e->rhs) && exprSame(*e->lhs, *e->rhs)) {
            if (op == "-") e = makeNumber(0);
            else if (op == "==") e = makeNumber(1);
            else if (op == "!=" || op == "<" || op == ">") e = makeNumber(0);
            else if (op == "<=" || op == ">=") e = makeNumber(1);
        }
    }

    void normalizeBool(unique_ptr<Expr>& e) {
        if (auto v = evalKnownConst(*e)) {
            e = makeNumber(static_cast<int32_t>(*v != 0));
            return;
        }
        auto zero = makeNumber(0);
        auto old = std::move(e);
        e = make_unique<Expr>();
        e->kind = Expr::Binary;
        e->op = "!=";
        e->lhs = std::move(old);
        e->rhs = std::move(zero);
    }

    optional<int32_t> evalKnownConst(const Expr& e) const {
        switch (e.kind) {
            case Expr::Number:
                return e.value;
            case Expr::Var: {
                auto known = lookupKnown(e.name);
                if (known && known->kind == Known::Const) return known->value;
                return nullopt;
            }
            case Expr::Call:
                return nullopt;
            case Expr::Unary: {
                auto v = evalKnownConst(*e.lhs);
                if (!v) return nullopt;
                if (e.op == "+") return *v;
                if (e.op == "-") return static_cast<int32_t>(-*v);
                if (e.op == "!") return static_cast<int32_t>(*v == 0);
                return nullopt;
            }
            case Expr::Binary: {
                if (e.op == "&&") {
                    auto l = evalKnownConst(*e.lhs);
                    if (!l) return nullopt;
                    if (*l == 0) return 0;
                    auto r = evalKnownConst(*e.rhs);
                    if (!r) return nullopt;
                    return static_cast<int32_t>(*r != 0);
                }
                if (e.op == "||") {
                    auto l = evalKnownConst(*e.lhs);
                    if (!l) return nullopt;
                    if (*l != 0) return 1;
                    auto r = evalKnownConst(*e.rhs);
                    if (!r) return nullopt;
                    return static_cast<int32_t>(*r != 0);
                }
                auto l = evalKnownConst(*e.lhs);
                auto r = evalKnownConst(*e.rhs);
                if (!l || !r) return nullopt;
                int32_t a = *l, b = *r;
                if (e.op == "+") return static_cast<int32_t>(a + b);
                if (e.op == "-") return static_cast<int32_t>(a - b);
                if (e.op == "*") return static_cast<int32_t>(a * b);
                if (e.op == "/" && b != 0) return static_cast<int32_t>(a / b);
                if (e.op == "%" && b != 0) return static_cast<int32_t>(a % b);
                if (e.op == "<") return static_cast<int32_t>(a < b);
                if (e.op == ">") return static_cast<int32_t>(a > b);
                if (e.op == "<=") return static_cast<int32_t>(a <= b);
                if (e.op == ">=") return static_cast<int32_t>(a >= b);
                if (e.op == "==") return static_cast<int32_t>(a == b);
                if (e.op == "!=") return static_cast<int32_t>(a != b);
                return nullopt;
            }
        }
        return nullopt;
    }

    optional<Known> lookupKnown(const string& name) const {
        for (auto it = env.rbegin(); it != env.rend(); ++it) {
            auto found = it->find(name);
            if (found != it->end()) return found->second;
        }
        return nullopt;
    }

    void assignKnown(const string& name, const Expr& value) {
        forgetName(name);
        if (globalNames.count(name)) return;
        assignKnownInCurrentScope(name, value);
    }

    void assignKnownInCurrentScope(const string& name, const Expr& value) {
        if (auto v = evalKnownConst(value)) {
            Known k;
            k.kind = Known::Const;
            k.value = *v;
            env.back()[name] = k;
        } else if (value.kind == Expr::Var) {
            Known k;
            k.kind = Known::Copy;
            k.name = value.name;
            env.back()[name] = k;
        } else {
            env.back().erase(name);
        }
    }

    void forgetName(const string& name) {
        for (auto& scope : env) scope.erase(name);
        forgetAliasesTo(name);
    }

    void forgetAliasesTo(const string& name) {
        for (auto& scope : env) {
            for (auto it = scope.begin(); it != scope.end();) {
                if (it->second.kind == Known::Copy && it->second.name == name) it = scope.erase(it);
                else ++it;
            }
        }
    }

    static bool sameKnown(const Known& a, const Known& b) {
        if (a.kind != b.kind) return false;
        if (a.kind == Known::Const) return a.value == b.value;
        return a.name == b.name;
    }

    vector<map<string, Known>> mergeEnv(const vector<map<string, Known>>& a,
                                        const vector<map<string, Known>>& b) {
        vector<map<string, Known>> merged;
        size_t n = min(a.size(), b.size());
        merged.resize(n);
        for (size_t i = 0; i < n; ++i) {
            for (const auto& [name, known] : a[i]) {
                auto found = b[i].find(name);
                if (found != b[i].end() && sameKnown(known, found->second)) {
                    merged[i][name] = known;
                }
            }
        }
        return merged;
    }

    static void collectAssigned(const Stmt& s, set<string>& names) {
        if (s.kind == Stmt::Assign) names.insert(s.name);
        if (s.thenStmt) collectAssigned(*s.thenStmt, names);
        if (s.elseStmt) collectAssigned(*s.elseStmt, names);
        for (const auto& child : s.stmts) collectAssigned(*child, names);
    }

    static bool stmtAlwaysTerminates(const Stmt& s) {
        if (s.kind == Stmt::Return || s.kind == Stmt::Break || s.kind == Stmt::Continue) return true;
        if (s.kind == Stmt::Block) {
            for (auto it = s.stmts.rbegin(); it != s.stmts.rend(); ++it) {
                if ((*it)->kind == Stmt::Empty || (*it)->kind == Stmt::DeclStmt ||
                    (*it)->kind == Stmt::ExprStmt) {
                    continue;
                }
                return stmtAlwaysTerminates(**it);
            }
        }
        if (s.kind == Stmt::If && s.elseStmt) {
            return stmtAlwaysTerminates(*s.thenStmt) && stmtAlwaysTerminates(*s.elseStmt);
        }
        return false;
    }

    void removeDeadStores(Stmt& s, set<string>& live) {
        switch (s.kind) {
            case Stmt::Block:
                for (auto it = s.stmts.rbegin(); it != s.stmts.rend(); ++it) {
                    removeDeadStores(**it, live);
                }
                break;
            case Stmt::Return:
                if (s.expr) collectVars(*s.expr, live);
                break;
            case Stmt::ExprStmt:
                if (s.expr) collectVars(*s.expr, live);
                break;
            case Stmt::Assign:
                if (!globalNames.count(s.name) && !live.count(s.name) && s.rhs) {
                    if (exprHasCall(*s.rhs)) {
                        s.kind = Stmt::ExprStmt;
                        s.expr = std::move(s.rhs);
                    } else {
                        s.kind = Stmt::Empty;
                        s.rhs.reset();
                    }
                    break;
                }
                live.erase(s.name);
                if (s.rhs) collectVars(*s.rhs, live);
                break;
            case Stmt::DeclStmt:
                if (!live.count(s.decl->name) && s.decl->init) {
                    if (exprHasCall(*s.decl->init)) {
                        s.kind = Stmt::ExprStmt;
                        s.expr = std::move(s.decl->init);
                        s.decl.reset();
                    } else {
                        s.kind = Stmt::Empty;
                        s.decl.reset();
                    }
                    break;
                }
                live.erase(s.decl->name);
                if (s.decl->init) collectVars(*s.decl->init, live);
                break;
            case Stmt::If: {
                set<string> thenLive = live;
                set<string> elseLive = live;
                removeDeadStores(*s.thenStmt, thenLive);
                if (s.elseStmt) removeDeadStores(*s.elseStmt, elseLive);
                live.insert(thenLive.begin(), thenLive.end());
                live.insert(elseLive.begin(), elseLive.end());
                if (s.expr) collectVars(*s.expr, live);
                break;
            }
            case Stmt::While: {
                set<string> loopLive = live;
                if (s.expr) collectVars(*s.expr, loopLive);
                removeDeadStores(*s.thenStmt, loopLive);
                live.insert(loopLive.begin(), loopLive.end());
                if (s.expr) collectVars(*s.expr, live);
                break;
            }
            default:
                break;
        }
    }

    static void collectVars(const Expr& e, set<string>& vars) {
        if (e.kind == Expr::Var) vars.insert(e.name);
        if (e.lhs) collectVars(*e.lhs, vars);
        if (e.rhs) collectVars(*e.rhs, vars);
        for (const auto& arg : e.args) collectVars(*arg, vars);
    }
};

class CodeGen {
public:
    explicit CodeGen(Program& program, bool optimize) : prog(program), optimize(optimize) {}

    string emit() {
        prepareGlobals();
        for (auto& fn : prog.funcs) funcMap[fn->name] = fn.get();
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
    map<string, Func*> funcMap;
    set<string> assignedNames;
    vector<map<string, Symbol>> scopes;
    vector<map<string, Expr*>> inlineSubsts;
    vector<pair<string, string>> loopLabels;
    string currentEndLabel;
    string currentTailLabel;
    Func* currentFunc = nullptr;
    int labelId = 0;
    int slotCount = 0;
    int savedRegCount = 0;
    int stackDepth = 0;

    string newLabel(const string& base) {
        return ".L" + base + "_" + to_string(labelId++);
    }

    static string mem(int offset, const string& base = "s0") {
        return to_string(offset) + "(" + base + ")";
    }

    static bool fitsImm12(int value) {
        return value >= -2048 && value <= 2047;
    }

    void emitAddRegImm(const string& dst, const string& base, int value) {
        if (fitsImm12(value)) {
            out << "    addi " << dst << ", " << base << ", " << value << "\n";
        } else {
            out << "    li t6, " << value << "\n";
            out << "    add " << dst << ", " << base << ", t6\n";
        }
    }

    void emitAddSp(int value) {
        emitAddRegImm("sp", "sp", value);
    }

    void emitStoreReg(const string& reg, int offset, const string& base = "s0") {
        if (fitsImm12(offset)) {
            out << "    sw " << reg << ", " << mem(offset, base) << "\n";
            return;
        }
        string scratch = reg == "t6" ? "t5" : "t6";
        out << "    li " << scratch << ", " << offset << "\n";
        out << "    add " << scratch << ", " << base << ", " << scratch << "\n";
        out << "    sw " << reg << ", 0(" << scratch << ")\n";
    }

    void emitLoadReg(const string& reg, int offset, const string& base = "s0") {
        if (fitsImm12(offset)) {
            out << "    lw " << reg << ", " << mem(offset, base) << "\n";
            return;
        }
        string scratch = reg == "t6" ? "t5" : "t6";
        out << "    li " << scratch << ", " << offset << "\n";
        out << "    add " << scratch << ", " << base << ", " << scratch << "\n";
        out << "    lw " << reg << ", 0(" << scratch << ")\n";
    }

    void prepareGlobals() {
        scopes.clear();
        assignedNames.clear();
        for (auto& fn : prog.funcs) collectAssignedNames(*fn->body);
        for (auto& d : prog.globals) {
            Symbol sym;
            auto initValue = evalConst(*d->init);
            sym.isConst = d->isConst || (optimize && initValue && !assignedNames.count(d->name));
            sym.isGlobal = true;
            sym.label = d->name;
            if (initValue) sym.constValue = *initValue;
            globals[d->name] = sym;
        }
    }

    void collectAssignedNames(Stmt& s) {
        if (s.kind == Stmt::Assign) assignedNames.insert(s.name);
        if (s.thenStmt) collectAssignedNames(*s.thenStmt);
        if (s.elseStmt) collectAssignedNames(*s.elseStmt);
        for (auto& child : s.stmts) collectAssignedNames(*child);
    }

    void prepareFunctions() {
        for (auto& fn : prog.funcs) {
            slotCount = 0;
            savedRegCount = 0;
            fn->paramOffsets.clear();
            fn->paramRegs.clear();
            fn->savedRegs.clear();
            fn->hasCall = optimize ? stmtContainsRuntimeCall(*fn->body, *fn)
                                   : stmtContainsCall(*fn->body);
            fn->savesRa = !optimize || fn->hasCall;
            for (const string& p : fn->params) {
                string reg = allocSavedReg();
                if (!reg.empty()) {
                    fn->paramRegs[p] = reg;
                } else {
                    fn->paramOffsets[p] = allocSlot();
                }
            }
            assignOffsets(*fn->body);
            for (int i = 1; i <= savedRegCount; ++i) {
                fn->savedRegs.push_back("s" + to_string(i));
            }
            fn->usesFramePointer = slotCount > 0 || fn->params.size() > 8;
            int fixedBytes = slotCount * 4 + static_cast<int>(fn->savedRegs.size()) * 4;
            if (fn->usesFramePointer) fixedBytes += 4;
            if (fn->savesRa) fixedBytes += 4;
            if (fixedBytes == 0) {
                fn->frameSize = 0;
            } else if (fn->hasCall) {
                fn->frameSize = alignTo(fixedBytes, 16);
            } else {
                fn->frameSize = alignTo(fixedBytes, 4);
            }
        }
    }

    static int framePointerSaveOffset(const Func& fn) {
        return fn.frameSize - (fn.savesRa ? 8 : 4);
    }

    static int returnAddressSaveOffset(const Func& fn) {
        return fn.frameSize - 4;
    }

    int allocSlot() {
        int offset = -12 - slotCount * 4;
        ++slotCount;
        return offset;
    }

    string allocSavedReg() {
        if (!optimize || savedRegCount >= 11) return "";
        ++savedRegCount;
        return "s" + to_string(savedRegCount);
    }

    void assignOffsets(Stmt& s) {
        switch (s.kind) {
            case Stmt::Block:
                for (auto& child : s.stmts) assignOffsets(*child);
                break;
            case Stmt::DeclStmt:
                if (!s.decl->isConst) {
                    if (optimize) {
                        if (auto val = evalConst(*s.decl->init); val && !assignedNames.count(s.decl->name)) {
                            break;
                        }
                    }
                    s.decl->reg = allocSavedReg();
                    if (s.decl->reg.empty()) s.decl->offset = allocSlot();
                }
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
        currentFunc = &fn;
        stackDepth = 0;
        scopes.clear();
        scopes.emplace_back();
        for (const string& p : fn.params) {
            Symbol sym;
            sym.offset = fn.paramOffsets[p];
            sym.reg = fn.paramRegs[p];
            scopes.back()[p] = sym;
        }

        currentEndLabel = newLabel(fn.name + "_end");
        currentTailLabel = newLabel(fn.name + "_tail");
        out << "    .globl " << fn.name << "\n";
        out << fn.name << ":\n";
        if (fn.frameSize > 0) emitAddSp(-fn.frameSize);
        for (size_t i = 0; i < fn.savedRegs.size(); ++i) {
            emitStoreReg(fn.savedRegs[i], static_cast<int>(i) * 4, "sp");
        }
        if (fn.savesRa) emitStoreReg("ra", returnAddressSaveOffset(fn), "sp");
        if (fn.usesFramePointer) {
            emitStoreReg("s0", framePointerSaveOffset(fn), "sp");
            emitAddRegImm("s0", "sp", fn.frameSize);
        }
        for (size_t i = 0; i < fn.params.size(); ++i) {
            string dstReg = fn.paramRegs[fn.params[i]];
            if (i < 8) {
                if (!dstReg.empty()) {
                    string srcReg = "a" + to_string(i);
                    if (dstReg != srcReg) out << "    mv " << dstReg << ", " << srcReg << "\n";
                } else {
                    emitStoreReg("a" + to_string(i), fn.paramOffsets[fn.params[i]]);
                }
            } else {
                emitLoadReg("t0", (static_cast<int>(i) - 8) * 4);
                if (!dstReg.empty()) out << "    mv " << dstReg << ", t0\n";
                else emitStoreReg("t0", fn.paramOffsets[fn.params[i]]);
            }
        }
        out << currentTailLabel << ":\n";
        emitBlock(*fn.body, true);
        if (fn.returnsVoid) out << "    li a0, 0\n";
        out << currentEndLabel << ":\n";
        if (fn.savesRa) emitLoadReg("ra", returnAddressSaveOffset(fn), "sp");
        if (fn.usesFramePointer) emitLoadReg("s0", framePointerSaveOffset(fn), "sp");
        for (size_t i = 0; i < fn.savedRegs.size(); ++i) {
            emitLoadReg(fn.savedRegs[i], static_cast<int>(i) * 4, "sp");
        }
        if (fn.frameSize > 0) emitAddSp(fn.frameSize);
        out << "    ret\n";
        currentFunc = nullptr;
    }

    void emitBlock(Stmt& s, bool tailPosition = false) {
        scopes.emplace_back();
        for (size_t i = 0; i < s.stmts.size(); ++i) {
            auto& child = s.stmts[i];
            bool childTail = tailPosition && i + 1 == s.stmts.size() &&
                             (child->kind == Stmt::Return || child->kind == Stmt::Block);
            emitStmt(*child, childTail);
            if (optimize && stmtTerminates(*child)) break;
        }
        scopes.pop_back();
    }

    bool stmtTerminates(Stmt& s) const {
        if (s.kind == Stmt::Return || s.kind == Stmt::Break || s.kind == Stmt::Continue) return true;
        if (s.kind == Stmt::Block) {
            for (auto it = s.stmts.rbegin(); it != s.stmts.rend(); ++it) {
                if ((*it)->kind == Stmt::Empty || (*it)->kind == Stmt::DeclStmt || (*it)->kind == Stmt::ExprStmt) {
                    continue;
                }
                return stmtTerminates(**it);
            }
            return false;
        }
        if (s.kind == Stmt::If && s.elseStmt) {
            return stmtTerminates(*s.thenStmt) && stmtTerminates(*s.elseStmt);
        }
        return false;
    }

    bool stmtContainsCall(Stmt& s) const {
        if (s.expr && hasCall(*s.expr)) return true;
        if (s.rhs && hasCall(*s.rhs)) return true;
        if (s.decl && s.decl->init && hasCall(*s.decl->init)) return true;
        if (s.thenStmt && stmtContainsCall(*s.thenStmt)) return true;
        if (s.elseStmt && stmtContainsCall(*s.elseStmt)) return true;
        for (auto& child : s.stmts) {
            if (stmtContainsCall(*child)) return true;
        }
        return false;
    }

    bool stmtContainsCallAfterTailOpt(Stmt& s, Func& fn) const {
        if (s.kind == Stmt::Return && s.expr && isOptimizableTailCall(*s.expr, fn)) {
            return false;
        }
        if (s.expr && hasCall(*s.expr)) return true;
        if (s.rhs && hasCall(*s.rhs)) return true;
        if (s.decl && s.decl->init && hasCall(*s.decl->init)) return true;
        if (s.thenStmt && stmtContainsCallAfterTailOpt(*s.thenStmt, fn)) return true;
        if (s.elseStmt && stmtContainsCallAfterTailOpt(*s.elseStmt, fn)) return true;
        for (auto& child : s.stmts) {
            if (stmtContainsCallAfterTailOpt(*child, fn)) return true;
        }
        return false;
    }

    bool stmtContainsRuntimeCall(Stmt& s, Func& fn) const {
        if (s.kind == Stmt::Return && s.expr && isOptimizableTailCall(*s.expr, fn)) {
            return false;
        }
        if (s.expr && exprContainsRuntimeCall(*s.expr, fn)) return true;
        if (s.rhs && exprContainsRuntimeCall(*s.rhs, fn)) return true;
        if (s.decl && s.decl->init && exprContainsRuntimeCall(*s.decl->init, fn)) return true;
        if (s.thenStmt && stmtContainsRuntimeCall(*s.thenStmt, fn)) return true;
        if (s.elseStmt && stmtContainsRuntimeCall(*s.elseStmt, fn)) return true;
        for (auto& child : s.stmts) {
            if (stmtContainsRuntimeCall(*child, fn)) return true;
        }
        return false;
    }

    bool exprContainsRuntimeCall(Expr& e, Func& current) const {
        if (e.kind == Expr::Call) {
            if (canInlineCall(e, &current)) return false;
            return true;
        }
        if (e.lhs && exprContainsRuntimeCall(*e.lhs, current)) return true;
        if (e.rhs && exprContainsRuntimeCall(*e.rhs, current)) return true;
        for (auto& arg : e.args) {
            if (exprContainsRuntimeCall(*arg, current)) return true;
        }
        return false;
    }

    bool isOptimizableTailCall(Expr& e, Func& fn) const {
        if (e.kind != Expr::Call || e.name != fn.name || e.args.size() != fn.params.size()) return false;
        for (auto& arg : e.args) {
            if (hasCall(*arg)) return false;
        }
        return true;
    }

    bool stmtContainsBreakContinue(Stmt& s) const {
        if (s.kind == Stmt::Break || s.kind == Stmt::Continue) return true;
        if (s.thenStmt && stmtContainsBreakContinue(*s.thenStmt)) return true;
        if (s.elseStmt && stmtContainsBreakContinue(*s.elseStmt)) return true;
        for (auto& child : s.stmts) {
            if (stmtContainsBreakContinue(*child)) return true;
        }
        return false;
    }

    void emitStmt(Stmt& s, bool tailPosition = false) {
        switch (s.kind) {
            case Stmt::Block:
                emitBlock(s, tailPosition);
                break;
            case Stmt::Empty:
                break;
            case Stmt::ExprStmt:
                emitExpr(*s.expr);
                break;
            case Stmt::Assign: {
                Symbol sym = lookup(s.name);
                if (optimize && !sym.reg.empty() && !hasCall(*s.rhs) && exprDepth(*s.rhs) <= 6) {
                    if (emitRegAssignment(s.name, sym.reg, *s.rhs)) break;
                    if (!exprReferencesName(*s.rhs, s.name)) {
                        emitExprInto(*s.rhs, sym.reg, 0);
                        break;
                    }
                }
                emitExpr(*s.rhs);
                if (sym.isGlobal) {
                    out << "    la t0, " << sym.label << "\n";
                    out << "    sw a0, 0(t0)\n";
                } else if (!sym.reg.empty()) {
                    out << "    mv " << sym.reg << ", a0\n";
                } else {
                    emitStoreReg("a0", sym.offset);
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
                if (optimize && s.expr && emitTailRecursiveReturn(*s.expr)) {
                    break;
                }
                if (s.expr) emitExpr(*s.expr);
                else out << "    li a0, 0\n";
                if (!tailPosition) out << "    j " << currentEndLabel << "\n";
                break;
        }
    }

    bool emitTailRecursiveReturn(Expr& e) {
        if (!currentFunc || e.kind != Expr::Call || e.name != currentFunc->name) return false;
        if (e.args.size() != currentFunc->params.size()) return false;
        int n = static_cast<int>(e.args.size());
        if (optimize && n <= 8) {
            bool direct = true;
            for (auto& arg : e.args) {
                if (hasCall(*arg) || exprDepth(*arg) > 6) {
                    direct = false;
                    break;
                }
            }
            if (direct) {
                for (int i = 0; i < n; ++i) {
                    emitExprInto(*e.args[i], "a" + to_string(i), 0);
                }
                for (int i = 0; i < n; ++i) {
                    Symbol sym = lookup(currentFunc->params[i]);
                    if (!sym.reg.empty()) {
                        out << "    mv " << sym.reg << ", a" << i << "\n";
                    } else {
                        emitStoreReg("a" + to_string(i), sym.offset);
                    }
                }
                out << "    j " << currentTailLabel << "\n";
                return true;
            }
        }
        for (auto& arg : e.args) {
            emitExpr(*arg);
            out << "    addi sp, sp, -4\n";
            out << "    sw a0, 0(sp)\n";
            stackDepth += 4;
        }
        for (int i = 0; i < n; ++i) {
            emitLoadReg("t0", (n - 1 - i) * 4, "sp");
            Symbol sym = lookup(currentFunc->params[i]);
            if (!sym.reg.empty()) {
                out << "    mv " << sym.reg << ", t0\n";
            } else {
                emitStoreReg("t0", sym.offset);
            }
        }
        if (n > 0) {
            emitAddSp(n * 4);
            stackDepth -= n * 4;
        }
        out << "    j " << currentTailLabel << "\n";
        return true;
    }

    void emitDecl(Decl& d) {
        if (d.isConst) {
            int32_t val = evalConst(*d.init).value_or(0);
            Symbol sym;
            sym.isConst = true;
            sym.constValue = val;
            scopes.back()[d.name] = sym;
            return;
        }
        if (optimize) {
            if (auto val = evalConst(*d.init); val && !assignedNames.count(d.name)) {
                Symbol sym;
                sym.isConst = true;
                sym.constValue = *val;
                scopes.back()[d.name] = sym;
                return;
            }
        }
        if (!d.reg.empty()) {
            if (optimize && !hasCall(*d.init) && exprDepth(*d.init) <= 6) {
                emitExprInto(*d.init, d.reg, 0);
            } else {
                emitExpr(*d.init);
                out << "    mv " << d.reg << ", a0\n";
            }
        } else {
            emitExpr(*d.init);
            emitStoreReg("a0", d.offset);
        }
        Symbol sym;
        sym.offset = d.offset;
        sym.reg = d.reg;
        scopes.back()[d.name] = sym;
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
        string endLabel = newLabel("ifend");
        if (!s.elseStmt) {
            emitBranchIfFalse(*s.expr, endLabel);
            emitStmt(*s.thenStmt);
            out << endLabel << ":\n";
            return;
        }
        string elseLabel = newLabel("else");
        emitBranchIfFalse(*s.expr, elseLabel);
        emitStmt(*s.thenStmt);
        if (!stmtTerminates(*s.thenStmt)) out << "    j " << endLabel << "\n";
        out << elseLabel << ":\n";
        emitStmt(*s.elseStmt);
        out << endLabel << ":\n";
    }

    void emitWhile(Stmt& s) {
        if (optimize) {
            if (auto cond = evalConst(*s.expr)) {
                if (*cond == 0) return;
                string begin = newLabel("while_begin");
                string end = newLabel("while_end");
                loopLabels.push_back({begin, end});
                out << begin << ":\n";
                emitStmt(*s.thenStmt);
                out << "    j " << begin << "\n";
                out << end << ":\n";
                loopLabels.pop_back();
                return;
            }
            if (!stmtContainsBreakContinue(*s.thenStmt)) {
                string begin = newLabel("while_begin");
                string end = newLabel("while_end");
                loopLabels.push_back({begin, end});
                out << begin << ":\n";
                for (int i = 0; i < 4; ++i) {
                    emitBranchIfFalse(*s.expr, end);
                    emitStmt(*s.thenStmt);
                    if (stmtTerminates(*s.thenStmt)) break;
                }
                out << "    j " << begin << "\n";
                out << end << ":\n";
                loopLabels.pop_back();
                return;
            }
        }
        string begin = newLabel("while_begin");
        string end = newLabel("while_end");
        loopLabels.push_back({begin, end});
        out << begin << ":\n";
        emitBranchIfFalse(*s.expr, end);
        emitStmt(*s.thenStmt);
        out << "    j " << begin << "\n";
        out << end << ":\n";
        loopLabels.pop_back();
    }

    void emitBranchIfFalse(Expr& e, const string& label) {
        if (optimize && emitDirectBranchIfFalse(e, label)) return;
        emitExpr(e);
        out << "    beqz a0, " << label << "\n";
    }

    void emitBranchIfTrue(Expr& e, const string& label) {
        if (optimize && emitDirectBranchIfTrue(e, label)) return;
        emitExpr(e);
        out << "    bnez a0, " << label << "\n";
    }

    string emitBranchOperand(Expr& e, const string& scratch) {
        if (e.kind == Expr::Var) {
            Symbol sym = lookup(e.name);
            if (sym.isConst) {
                out << "    li " << scratch << ", " << sym.constValue << "\n";
                return scratch;
            }
            if (!sym.reg.empty()) return sym.reg;
        }
        if (auto val = evalConst(e)) {
            out << "    li " << scratch << ", " << *val << "\n";
            return scratch;
        }
        emitExprInto(e, scratch, 0);
        return scratch;
    }

    bool emitDirectBranchIfFalse(Expr& e, const string& label) {
        if (auto val = evalConst(e)) {
            if (*val == 0) out << "    j " << label << "\n";
            return true;
        }
        if (hasCall(e) || exprDepth(e) > 6) return false;
        if (e.kind == Expr::Binary && e.op == "&&") {
            emitBranchIfFalse(*e.lhs, label);
            emitBranchIfFalse(*e.rhs, label);
            return true;
        }
        if (e.kind == Expr::Binary && e.op == "||") {
            string doneLabel = newLabel("lor_done");
            emitBranchIfTrue(*e.lhs, doneLabel);
            emitBranchIfFalse(*e.rhs, label);
            out << doneLabel << ":\n";
            return true;
        }
        if (e.kind == Expr::Unary && e.op == "!") {
            emitExprInto(*e.lhs, "t0", 0);
            out << "    bnez t0, " << label << "\n";
            return true;
        }
        if (emitImmediateRelationBranch(e, label, false)) return true;
        if (e.kind == Expr::Binary &&
            (e.op == "<" || e.op == ">" || e.op == "<=" || e.op == ">=" ||
             e.op == "==" || e.op == "!=")) {
            string lhs = emitBranchOperand(*e.lhs, "t0");
            string rhs = emitBranchOperand(*e.rhs, lhs == "t0" ? "t1" : "t0");
            if (e.op == "<") out << "    bge " << lhs << ", " << rhs << ", " << label << "\n";
            else if (e.op == ">") out << "    bge " << rhs << ", " << lhs << ", " << label << "\n";
            else if (e.op == "<=") out << "    blt " << rhs << ", " << lhs << ", " << label << "\n";
            else if (e.op == ">=") out << "    blt " << lhs << ", " << rhs << ", " << label << "\n";
            else if (e.op == "==") out << "    bne " << lhs << ", " << rhs << ", " << label << "\n";
            else if (e.op == "!=") out << "    beq " << lhs << ", " << rhs << ", " << label << "\n";
            return true;
        }
        if (e.kind == Expr::Var) {
            Symbol sym = lookup(e.name);
            if (!sym.reg.empty()) {
                out << "    beqz " << sym.reg << ", " << label << "\n";
                return true;
            }
        }
        return false;
    }

    bool emitDirectBranchIfTrue(Expr& e, const string& label) {
        if (auto val = evalConst(e)) {
            if (*val != 0) out << "    j " << label << "\n";
            return true;
        }
        if (hasCall(e) || exprDepth(e) > 6) return false;
        if (e.kind == Expr::Binary && e.op == "&&") {
            string skipLabel = newLabel("land_skip");
            emitBranchIfFalse(*e.lhs, skipLabel);
            emitBranchIfTrue(*e.rhs, label);
            out << skipLabel << ":\n";
            return true;
        }
        if (e.kind == Expr::Binary && e.op == "||") {
            emitBranchIfTrue(*e.lhs, label);
            emitBranchIfTrue(*e.rhs, label);
            return true;
        }
        if (e.kind == Expr::Unary && e.op == "!") {
            emitBranchIfFalse(*e.lhs, label);
            return true;
        }
        if (emitImmediateRelationBranch(e, label, true)) return true;
        if (e.kind == Expr::Binary &&
            (e.op == "<" || e.op == ">" || e.op == "<=" || e.op == ">=" ||
             e.op == "==" || e.op == "!=")) {
            string lhs = emitBranchOperand(*e.lhs, "t0");
            string rhs = emitBranchOperand(*e.rhs, lhs == "t0" ? "t1" : "t0");
            if (e.op == "<") out << "    blt " << lhs << ", " << rhs << ", " << label << "\n";
            else if (e.op == ">") out << "    blt " << rhs << ", " << lhs << ", " << label << "\n";
            else if (e.op == "<=") out << "    bge " << rhs << ", " << lhs << ", " << label << "\n";
            else if (e.op == ">=") out << "    bge " << lhs << ", " << rhs << ", " << label << "\n";
            else if (e.op == "==") out << "    beq " << lhs << ", " << rhs << ", " << label << "\n";
            else if (e.op == "!=") out << "    bne " << lhs << ", " << rhs << ", " << label << "\n";
            return true;
        }
        if (e.kind == Expr::Var) {
            Symbol sym = lookup(e.name);
            if (!sym.reg.empty()) {
                out << "    bnez " << sym.reg << ", " << label << "\n";
                return true;
            }
        }
        return false;
    }

    bool emitImmediateRelationBranch(Expr& e, const string& label, bool branchOnTrue) {
        if (e.kind != Expr::Binary) return false;
        if (!(e.op == "<" || e.op == ">" || e.op == "<=" || e.op == ">=" ||
              e.op == "==" || e.op == "!=")) {
            return false;
        }

        auto rhsConst = evalConst(*e.rhs);
        if (rhsConst) {
            string lhs = emitBranchOperand(*e.lhs, "t0");
            if ((e.op == "==" || e.op == "!=") && *rhsConst == 0) {
                bool useBnez = (e.op == "!=") == branchOnTrue;
                out << "    " << (useBnez ? "bnez " : "beqz ") << lhs << ", " << label << "\n";
                return true;
            }
            int32_t bound = *rhsConst;
            string flag = lhs == "t0" ? "t1" : "t0";
            if (e.op == "<" && fitsImm12(bound)) {
                out << "    slti " << flag << ", " << lhs << ", " << bound << "\n";
                out << "    " << (branchOnTrue ? "bnez " : "beqz ") << flag << ", " << label << "\n";
                return true;
            }
            if (e.op == ">=" && fitsImm12(bound)) {
                out << "    slti " << flag << ", " << lhs << ", " << bound << "\n";
                out << "    " << (branchOnTrue ? "beqz " : "bnez ") << flag << ", " << label << "\n";
                return true;
            }
            if (bound < INT32_MAX && fitsImm12(bound + 1)) {
                int32_t next = bound + 1;
                if (e.op == ">" || e.op == "<=") {
                    out << "    slti " << flag << ", " << lhs << ", " << next << "\n";
                    bool branchIfNonZero = (e.op == "<=") == branchOnTrue;
                    out << "    " << (branchIfNonZero ? "bnez " : "beqz ") << flag << ", " << label << "\n";
                    return true;
                }
            }
        }

        auto lhsConst = evalConst(*e.lhs);
        if (lhsConst && (e.op == "==" || e.op == "!=") && *lhsConst == 0) {
            string rhs = emitBranchOperand(*e.rhs, "t0");
            bool useBnez = (e.op == "!=") == branchOnTrue;
            out << "    " << (useBnez ? "bnez " : "beqz ") << rhs << ", " << label << "\n";
            return true;
        }
        return false;
    }

    void emitExpr(Expr& e) {
        if (optimize) {
            if (auto v = evalConst(e)) {
                out << "    li a0, " << *v << "\n";
                return;
            }
            if (!hasCall(e) && e.kind == Expr::Binary && emitLinearAddInto(e, "a0")) {
                return;
            }
            if (!hasCall(e) && exprDepth(e) <= 6) {
                emitExprInto(e, "a0", 0);
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
                if (optimize && emitInlineCall(e, "a0")) break;
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

    bool hasCall(Expr& e) const {
        if (e.kind == Expr::Call) return true;
        if (e.lhs && hasCall(*e.lhs)) return true;
        if (e.rhs && hasCall(*e.rhs)) return true;
        for (const auto& arg : e.args) {
            if (hasCall(*arg)) return true;
        }
        return false;
    }

    bool exprReferencesName(Expr& e, const string& name) const {
        if (e.kind == Expr::Var && e.name == name) return true;
        if (e.lhs && exprReferencesName(*e.lhs, name)) return true;
        if (e.rhs && exprReferencesName(*e.rhs, name)) return true;
        for (const auto& arg : e.args) {
            if (exprReferencesName(*arg, name)) return true;
        }
        return false;
    }

    bool emitRegAssignment(const string& name, const string& reg, Expr& rhs) {
        if (rhs.kind == Expr::Var && rhs.name == name) return true;
        if (rhs.kind != Expr::Binary || rhs.op == "&&" || rhs.op == "||") return false;

        bool lhsSelf = rhs.lhs->kind == Expr::Var && rhs.lhs->name == name;
        bool rhsSelf = rhs.rhs->kind == Expr::Var && rhs.rhs->name == name;
        if (!lhsSelf && !rhsSelf) return false;

        if (lhsSelf) {
            if (auto value = evalConst(*rhs.rhs)) {
                if (emitSelfBinaryConst(rhs.op, reg, *value, true)) return true;
            }
            string operand;
            if (!tryVarRegisterOperand(*rhs.rhs, operand)) {
                operand = tempReg(0, reg);
                emitExprInto(*rhs.rhs, operand, 1);
            }
            return emitRegBinaryOp(rhs.op, reg, reg, operand);
        }

        if (auto value = evalConst(*rhs.lhs)) {
            if (emitSelfBinaryConst(rhs.op, reg, *value, false)) return true;
        }
        string operand;
        if (!tryVarRegisterOperand(*rhs.lhs, operand)) {
            operand = tempReg(0, reg);
            emitExprInto(*rhs.lhs, operand, 1);
        }
        return emitRegBinaryOp(rhs.op, reg, operand, reg);
    }

    bool tryVarRegisterOperand(Expr& e, string& reg) const {
        if (e.kind != Expr::Var) return false;
        if (!inlineSubsts.empty() && inlineSubsts.back().count(e.name)) return false;
        auto sym = tryLookup(e.name);
        if (!sym || sym->reg.empty()) return false;
        reg = sym->reg;
        return true;
    }

    bool emitSelfBinaryConst(const string& op, const string& reg, int32_t value, bool selfOnLhs) {
        if (op == "+") {
            if (value == 0) return true;
            emitAddRegImm(reg, reg, value);
            return true;
        }
        if (op == "-" && selfOnLhs) {
            if (value == 0) return true;
            if (value == INT32_MIN) {
                out << "    li t6, " << value << "\n";
                out << "    sub " << reg << ", " << reg << ", t6\n";
                return true;
            }
            emitAddRegImm(reg, reg, -value);
            return true;
        }
        if (op == "-" && !selfOnLhs) {
            out << "    li t6, " << value << "\n";
            out << "    sub " << reg << ", t6, " << reg << "\n";
            return true;
        }
        if (op == "*") {
            if (value == 0) {
                out << "    li " << reg << ", 0\n";
            } else if (value == 1) {
                return true;
            } else if (value == -1) {
                out << "    neg " << reg << ", " << reg << "\n";
            } else if (value > 0 && (value & (value - 1)) == 0) {
                int shift = 0;
                while (shift < 31 && (1u << shift) != static_cast<uint32_t>(value)) ++shift;
                out << "    slli " << reg << ", " << reg << ", " << shift << "\n";
            } else {
                out << "    li t6, " << value << "\n";
                out << "    mul " << reg << ", " << reg << ", t6\n";
            }
            return true;
        }
        if (op == "/" && selfOnLhs && value == 1) return true;
        if (op == "%" && selfOnLhs && value == 1) {
            out << "    li " << reg << ", 0\n";
            return true;
        }
        if (op == "<" && selfOnLhs && fitsImm12(value)) {
            out << "    slti " << reg << ", " << reg << ", " << value << "\n";
            return true;
        }
        if (op == ">=" && selfOnLhs && fitsImm12(value)) {
            out << "    slti " << reg << ", " << reg << ", " << value << "\n";
            out << "    xori " << reg << ", " << reg << ", 1\n";
            return true;
        }

        out << "    li t6, " << value << "\n";
        if (selfOnLhs) return emitRegBinaryOp(op, reg, reg, "t6");
        return emitRegBinaryOp(op, reg, "t6", reg);
    }

    int exprDepth(Expr& e) const {
        int depth = 1;
        if (e.lhs) depth = max(depth, 1 + exprDepth(*e.lhs));
        if (e.rhs) depth = max(depth, 1 + exprDepth(*e.rhs));
        for (const auto& arg : e.args) {
            depth = max(depth, 1 + exprDepth(*arg));
        }
        return depth;
    }

    string tempReg(int depth, const string& avoid) const {
        static const vector<string> regs = {"t0", "t1", "t2", "t3", "t4", "t5", "t6"};
        for (size_t i = static_cast<size_t>(depth); i < regs.size(); ++i) {
            if (regs[i] != avoid) return regs[i];
        }
        for (const string& reg : regs) {
            if (reg != avoid) return reg;
        }
        return "t0";
    }

    void emitExprInto(Expr& e, const string& dest, int depth) {
        if (auto v = evalConst(e)) {
            out << "    li " << dest << ", " << *v << "\n";
            return;
        }
        switch (e.kind) {
            case Expr::Number:
                out << "    li " << dest << ", " << e.value << "\n";
                break;
            case Expr::Var:
                emitLoadVarInto(e.name, dest);
                break;
            case Expr::Unary:
                emitExprInto(*e.lhs, dest, depth + 1);
                if (e.op == "-") out << "    neg " << dest << ", " << dest << "\n";
                else if (e.op == "!") out << "    seqz " << dest << ", " << dest << "\n";
                break;
            case Expr::Binary:
                if (optimize && !hasCall(e) && emitLinearAddInto(e, dest)) break;
                if (exprDepth(e) > 6) {
                    emitExpr(e);
                    if (dest != "a0") out << "    mv " << dest << ", a0\n";
                    break;
                }
                emitBinaryInto(e, dest, depth);
                break;
            case Expr::Call:
                if (optimize && emitInlineCall(e, dest)) break;
                emitCall(e);
                if (dest != "a0") out << "    mv " << dest << ", a0\n";
                break;
        }
    }

    void emitLoadVarInto(const string& name, const string& dest) {
        if (!inlineSubsts.empty()) {
            auto found = inlineSubsts.back().find(name);
            if (found != inlineSubsts.back().end()) {
                emitExprInto(*found->second, dest, 0);
                return;
            }
        }
        Symbol sym = lookup(name);
        if (sym.isConst) {
            out << "    li " << dest << ", " << sym.constValue << "\n";
        } else if (sym.isGlobal) {
            out << "    la t6, " << sym.label << "\n";
            out << "    lw " << dest << ", 0(t6)\n";
        } else if (!sym.reg.empty()) {
            if (dest != sym.reg) out << "    mv " << dest << ", " << sym.reg << "\n";
        } else {
            emitLoadReg(dest, sym.offset);
        }
    }

    bool emitInlineCall(Expr& call, const string& dest) {
        if (!canInlineCall(call, currentFunc)) return false;
        Func* fn = funcMap[call.name];
        Stmt* ret = fn->body->stmts[0].get();

        map<string, Expr*> subst;
        for (size_t i = 0; i < fn->params.size(); ++i) {
            subst[fn->params[i]] = call.args[i].get();
        }
        inlineSubsts.push_back(std::move(subst));
        emitExprInto(*ret->expr, dest, 0);
        inlineSubsts.pop_back();
        return true;
    }

    bool canInlineCall(Expr& call, const Func* caller) const {
        auto found = funcMap.find(call.name);
        if (found == funcMap.end() || found->second == caller) return false;
        Func* fn = found->second;
        if (fn->returnsVoid || call.args.size() != fn->params.size()) return false;
        for (auto& arg : call.args) {
            if (hasCall(*arg)) return false;
        }
        if (fn->body->kind != Stmt::Block || fn->body->stmts.size() != 1) return false;
        Stmt* ret = fn->body->stmts[0].get();
        if (ret->kind != Stmt::Return || !ret->expr || hasCall(*ret->expr)) return false;
        set<string> params(fn->params.begin(), fn->params.end());
        return exprUsesOnlyNames(*ret->expr, params);
    }

    bool exprUsesOnlyNames(Expr& e, const set<string>& allowed) const {
        if (e.kind == Expr::Var) return allowed.count(e.name) > 0;
        if (e.lhs && !exprUsesOnlyNames(*e.lhs, allowed)) return false;
        if (e.rhs && !exprUsesOnlyNames(*e.rhs, allowed)) return false;
        for (const auto& arg : e.args) {
            if (!exprUsesOnlyNames(*arg, allowed)) return false;
        }
        return true;
    }

    void emitBinaryInto(Expr& e, const string& dest, int depth) {
        if (e.op == "&&") {
            string falseLabel = newLabel("land_false");
            string endLabel = newLabel("land_end");
            emitExprInto(*e.lhs, dest, depth + 1);
            out << "    beqz " << dest << ", " << falseLabel << "\n";
            emitExprInto(*e.rhs, dest, depth + 1);
            out << "    snez " << dest << ", " << dest << "\n";
            out << "    j " << endLabel << "\n";
            out << falseLabel << ":\n";
            out << "    li " << dest << ", 0\n";
            out << endLabel << ":\n";
            return;
        }
        if (e.op == "||") {
            string trueLabel = newLabel("lor_true");
            string endLabel = newLabel("lor_end");
            emitExprInto(*e.lhs, dest, depth + 1);
            out << "    bnez " << dest << ", " << trueLabel << "\n";
            emitExprInto(*e.rhs, dest, depth + 1);
            out << "    snez " << dest << ", " << dest << "\n";
            out << "    j " << endLabel << "\n";
            out << trueLabel << ":\n";
            out << "    li " << dest << ", 1\n";
            out << endLabel << ":\n";
            return;
        }
        if (emitImmediateBinary(e, dest, depth)) return;
        if (emitSameOperandBinary(e, dest, depth)) return;
        if (emitRegisterRhsBinary(e, dest, depth)) return;
        string rhsReg = tempReg(depth, dest);
        emitExprInto(*e.lhs, dest, depth + 1);
        emitExprInto(*e.rhs, rhsReg, depth + 1);
        emitRegBinaryOp(e.op, dest, dest, rhsReg);
    }

    bool emitSameOperandBinary(Expr& e, const string& dest, int depth) {
        if (exprHasCall(*e.lhs) || exprHasCall(*e.rhs) || !exprSame(*e.lhs, *e.rhs)) return false;
        if (e.op == "+") {
            emitExprInto(*e.lhs, dest, depth + 1);
            out << "    add " << dest << ", " << dest << ", " << dest << "\n";
            return true;
        }
        if (e.op == "*") {
            emitExprInto(*e.lhs, dest, depth + 1);
            out << "    mul " << dest << ", " << dest << ", " << dest << "\n";
            return true;
        }
        if (e.op == "-") {
            out << "    li " << dest << ", 0\n";
            return true;
        }
        if (e.op == "==" || e.op == "<=" || e.op == ">=") {
            out << "    li " << dest << ", 1\n";
            return true;
        }
        if (e.op == "!=" || e.op == "<" || e.op == ">") {
            out << "    li " << dest << ", 0\n";
            return true;
        }
        return false;
    }

    bool emitLinearAddInto(Expr& e, const string& dest) {
        vector<pair<int, Expr*>> terms;
        collectAddTerms(e, 1, terms);
        if (terms.size() < 3) return false;

        size_t index = 0;
        if (terms[0].first > 0) {
            emitExprInto(*terms[0].second, dest, 0);
            index = 1;
        } else {
            out << "    li " << dest << ", 0\n";
        }
        for (; index < terms.size(); ++index) {
            int sign = terms[index].first;
            Expr& term = *terms[index].second;
            if (auto v = evalConst(term)) {
                int64_t imm = sign > 0 ? static_cast<int64_t>(*v) : -static_cast<int64_t>(*v);
                emitAddRegImm64(dest, dest, imm);
                continue;
            }
            string reg;
            if (!tryVarRegisterOperand(term, reg)) {
                reg = tempReg(0, dest);
                emitExprInto(term, reg, 0);
            }
            out << "    " << (sign > 0 ? "add " : "sub ") << dest << ", " << dest << ", " << reg << "\n";
        }
        return true;
    }

    void collectAddTerms(Expr& e, int sign, vector<pair<int, Expr*>>& terms) {
        if (e.kind == Expr::Binary && e.op == "+") {
            collectAddTerms(*e.lhs, sign, terms);
            collectAddTerms(*e.rhs, sign, terms);
            return;
        }
        if (e.kind == Expr::Binary && e.op == "-") {
            collectAddTerms(*e.lhs, sign, terms);
            collectAddTerms(*e.rhs, -sign, terms);
            return;
        }
        terms.push_back({sign, &e});
    }

    void emitAddRegImm64(const string& dst, const string& base, int64_t value) {
        if (value >= -2048 && value <= 2047) {
            out << "    addi " << dst << ", " << base << ", " << value << "\n";
        } else {
            out << "    li t6, " << value << "\n";
            out << "    add " << dst << ", " << base << ", t6\n";
        }
    }

    bool emitRegBinaryOp(const string& op, const string& dest, const string& lhs, const string& rhs) {
        if (op == "+") out << "    add " << dest << ", " << lhs << ", " << rhs << "\n";
        else if (op == "-") out << "    sub " << dest << ", " << lhs << ", " << rhs << "\n";
        else if (op == "*") out << "    mul " << dest << ", " << lhs << ", " << rhs << "\n";
        else if (op == "/") out << "    div " << dest << ", " << lhs << ", " << rhs << "\n";
        else if (op == "%") out << "    rem " << dest << ", " << lhs << ", " << rhs << "\n";
        else if (op == "<") out << "    slt " << dest << ", " << lhs << ", " << rhs << "\n";
        else if (op == ">") out << "    slt " << dest << ", " << rhs << ", " << lhs << "\n";
        else if (op == "<=") {
            out << "    slt " << dest << ", " << rhs << ", " << lhs << "\n";
            out << "    xori " << dest << ", " << dest << ", 1\n";
        } else if (op == ">=") {
            out << "    slt " << dest << ", " << lhs << ", " << rhs << "\n";
            out << "    xori " << dest << ", " << dest << ", 1\n";
        } else if (op == "==") {
            out << "    xor " << dest << ", " << lhs << ", " << rhs << "\n";
            out << "    seqz " << dest << ", " << dest << "\n";
        } else if (op == "!=") {
            out << "    xor " << dest << ", " << lhs << ", " << rhs << "\n";
            out << "    snez " << dest << ", " << dest << "\n";
        } else {
            return false;
        }
        return true;
    }

    bool emitImmediateBinary(Expr& e, const string& dest, int depth) {
        auto rhs = evalConst(*e.rhs);
        auto lhs = evalConst(*e.lhs);
        if (rhs) {
            int32_t v = *rhs;
            if (e.op == "+" && v == 0) {
                emitExprInto(*e.lhs, dest, depth + 1);
                return true;
            }
            if (e.op == "-" && v == 0) {
                emitExprInto(*e.lhs, dest, depth + 1);
                return true;
            }
            if (e.op == "*" && v == 0) {
                out << "    li " << dest << ", 0\n";
                return true;
            }
            if (e.op == "*" && v == 1) {
                emitExprInto(*e.lhs, dest, depth + 1);
                return true;
            }
            if (e.op == "+" && v >= -2048 && v <= 2047) {
                emitExprInto(*e.lhs, dest, depth + 1);
                out << "    addi " << dest << ", " << dest << ", " << v << "\n";
                return true;
            }
            if (e.op == "-" && v >= -2047 && v <= 2048) {
                emitExprInto(*e.lhs, dest, depth + 1);
                out << "    addi " << dest << ", " << dest << ", " << -v << "\n";
                return true;
            }
            if (e.op == "<" && v >= -2048 && v <= 2047) {
                emitExprInto(*e.lhs, dest, depth + 1);
                out << "    slti " << dest << ", " << dest << ", " << v << "\n";
                return true;
            }
            if (e.op == ">=" && v >= -2048 && v <= 2047) {
                emitExprInto(*e.lhs, dest, depth + 1);
                out << "    slti " << dest << ", " << dest << ", " << v << "\n";
                out << "    xori " << dest << ", " << dest << ", 1\n";
                return true;
            }
            if (e.op == "==" && v == 0) {
                emitExprInto(*e.lhs, dest, depth + 1);
                out << "    seqz " << dest << ", " << dest << "\n";
                return true;
            }
            if (e.op == "!=" && v == 0) {
                emitExprInto(*e.lhs, dest, depth + 1);
                out << "    snez " << dest << ", " << dest << "\n";
                return true;
            }
            if (e.op == "*" && v > 0 && (v & (v - 1)) == 0) {
                int shift = 0;
                while (shift < 31 && (1u << shift) != static_cast<uint32_t>(v)) ++shift;
                emitExprInto(*e.lhs, dest, depth + 1);
                out << "    slli " << dest << ", " << dest << ", " << shift << "\n";
                return true;
            }
        }
        if (lhs) {
            int32_t v = *lhs;
            if (e.op == "+" && v == 0) {
                emitExprInto(*e.rhs, dest, depth + 1);
                return true;
            }
            if (e.op == "*" && v == 0) {
                out << "    li " << dest << ", 0\n";
                return true;
            }
            if (e.op == "*" && v == 1) {
                emitExprInto(*e.rhs, dest, depth + 1);
                return true;
            }
            if (e.op == "*" && v > 0 && (v & (v - 1)) == 0) {
                int shift = 0;
                while (shift < 31 && (1u << shift) != static_cast<uint32_t>(v)) ++shift;
                emitExprInto(*e.rhs, dest, depth + 1);
                out << "    slli " << dest << ", " << dest << ", " << shift << "\n";
                return true;
            }
        }
        return false;
    }

    bool emitRegisterRhsBinary(Expr& e, const string& dest, int depth) {
        if (e.rhs->kind != Expr::Var) return false;
        if (!inlineSubsts.empty() && inlineSubsts.back().count(e.rhs->name)) return false;
        auto rhsOpt = tryLookup(e.rhs->name);
        if (!rhsOpt) return false;
        Symbol rhs = *rhsOpt;
        if (rhs.reg.empty() || rhs.reg == dest) return false;
        emitExprInto(*e.lhs, dest, depth + 1);
        return emitRegBinaryOp(e.op, dest, dest, rhs.reg);
    }

    optional<Symbol> tryLookup(const string& name) const {
        for (auto it = scopes.rbegin(); it != scopes.rend(); ++it) {
            auto found = it->find(name);
            if (found != it->end()) return found->second;
        }
        auto g = globals.find(name);
        if (g != globals.end()) return g->second;
        return nullopt;
    }

    void emitLoadVar(const string& name) {
        if (!inlineSubsts.empty()) {
            auto found = inlineSubsts.back().find(name);
            if (found != inlineSubsts.back().end()) {
                emitExpr(*found->second);
                return;
            }
        }
        Symbol sym = lookup(name);
        if (sym.isConst) {
            out << "    li a0, " << sym.constValue << "\n";
        } else if (sym.isGlobal) {
            out << "    la t0, " << sym.label << "\n";
            out << "    lw a0, 0(t0)\n";
        } else if (!sym.reg.empty()) {
            out << "    mv a0, " << sym.reg << "\n";
        } else {
            emitLoadReg("a0", sym.offset);
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
        stackDepth += 4;
        emitExpr(*e.rhs);
        out << "    lw t0, 0(sp)\n";
        out << "    addi sp, sp, 4\n";
        stackDepth -= 4;
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
        if (optimize && n <= 8) {
            bool direct = true;
            for (auto& arg : e.args) {
                if (hasCall(*arg)) {
                    direct = false;
                    break;
                }
            }
            if (direct) {
                for (int i = 0; i < n; ++i) {
                    emitExprInto(*e.args[i], "a" + to_string(i), 0);
                }
                int pad = (16 - (stackDepth % 16)) % 16;
                if (pad > 0) {
                    emitAddSp(-pad);
                    stackDepth += pad;
                }
                out << "    call " << e.name << "\n";
                if (pad > 0) {
                    emitAddSp(pad);
                    stackDepth -= pad;
                }
                return;
            }
        }
        for (auto& arg : e.args) {
            emitExpr(*arg);
            out << "    addi sp, sp, -4\n";
            out << "    sw a0, 0(sp)\n";
            stackDepth += 4;
        }
        if (n > 0) out << "    mv t2, sp\n";
        int regArgs = min(n, 8);
        for (int i = 0; i < regArgs; ++i) {
            int off = (n - 1 - i) * 4;
            emitLoadReg("a" + to_string(i), off, "t2");
        }
        int stackArgs = max(0, n - 8);
        int pad = (16 - ((stackDepth + stackArgs * 4) % 16)) % 16;
        if (pad > 0) {
            emitAddSp(-pad);
            stackDepth += pad;
        }
        if (stackArgs > 0) {
            for (int j = n - 1; j >= 8; --j) {
                int off = (n - 1 - j) * 4;
                emitLoadReg("t0", off, "t2");
                out << "    addi sp, sp, -4\n";
                out << "    sw t0, 0(sp)\n";
                stackDepth += 4;
            }
        }
        out << "    call " << e.name << "\n";
        int bytesToPop = n * 4 + stackArgs * 4 + pad;
        if (bytesToPop > 0) {
            emitAddSp(bytesToPop);
            stackDepth -= bytesToPop;
        }
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
                if (!inlineSubsts.empty()) {
                    auto found = inlineSubsts.back().find(e.name);
                    if (found != inlineSubsts.back().end()) return evalConst(*found->second);
                }
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
                if (e.op == "&&") {
                    auto l = evalConst(*e.lhs);
                    if (!l) return nullopt;
                    if (*l == 0) return static_cast<int32_t>(0);
                    auto r = evalConst(*e.rhs);
                    if (!r) return nullopt;
                    return static_cast<int32_t>(*r != 0);
                }
                if (e.op == "||") {
                    auto l = evalConst(*e.lhs);
                    if (!l) return nullopt;
                    if (*l != 0) return static_cast<int32_t>(1);
                    auto r = evalConst(*e.rhs);
                    if (!r) return nullopt;
                    return static_cast<int32_t>(*r != 0);
                }
                auto l = evalConst(*e.lhs);
                auto r = evalConst(*e.rhs);
                if (!l || !r) return nullopt;
                int32_t a = *l, b = *r;
                if (e.op == "+") return static_cast<int32_t>(a + b);
                if (e.op == "-") return static_cast<int32_t>(a - b);
                if (e.op == "*") return static_cast<int32_t>(a * b);
                if (e.op == "/") {
                    if (b == 0) return nullopt;
                    return static_cast<int32_t>(a / b);
                }
                if (e.op == "%") {
                    if (b == 0) return nullopt;
                    return static_cast<int32_t>(a % b);
                }
                if (e.op == "<") return static_cast<int32_t>(a < b);
                if (e.op == ">") return static_cast<int32_t>(a > b);
                if (e.op == "<=") return static_cast<int32_t>(a <= b);
                if (e.op == ">=") return static_cast<int32_t>(a >= b);
                if (e.op == "==") return static_cast<int32_t>(a == b);
                if (e.op == "!=") return static_cast<int32_t>(a != b);
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


string compileToyC(const string& input, bool optimize) {
    Lexer lexer(input);
    Parser parser(lexer.scan());
    Program program = parser.parseProgram();
    if (optimize) {
        Optimizer optimizer(program);
        optimizer.run();
    }
    CodeGen codegen(program, optimize);
    return codegen.emit();
}
