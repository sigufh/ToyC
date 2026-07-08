#include "compiler.hpp"

#include <exception>
#include <iostream>
#include <iterator>
#include <string>

using namespace std;

int main(int argc, char** argv) {
    ios::sync_with_stdio(false);
    cin.tie(nullptr);

    bool optimize = false;
    for (int i = 1; i < argc; ++i) {
        if (string(argv[i]) == "-opt") optimize = true;
    }

    string input((istreambuf_iterator<char>(cin)), istreambuf_iterator<char>());
    try {
        cout << compileToyC(input, optimize);
    } catch (const exception& ex) {
        cerr << ex.what() << '\n';
        return 1;
    }
    return 0;
}
