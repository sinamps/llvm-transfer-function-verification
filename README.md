# llvm-transfer-function-verification
Code to verify the composite version of a transfer function is the same is decomposite version.


## Setup LLVM
- make sure you have llvm installed and accessible via `llvm-config --version`

## Compile and Run
- `clang++ -std=c++14 -I/opt/homebrew/opt/llvm/include -L/opt/homebrew/opt/llvm/lib main.cpp -o main $(llvm-config --cxxflags --ldflags --libs)`
- `./main`