# The oldest CMake version this file works with.
cmake_minimum_required(VERSION 3.20)

# The project's name. CLion uses it to label things.
project(cminus)

# Use C++17 (the 17 means "the 2017 version of the language").
set(CMAKE_CXX_STANDARD 17)

# Build a program called "cminus" from our source file.
# The quotes around the file name are there because it contains + and - characters.
add_executable(cminus "C-+-.cpp"
        lexer.h
        ast.h
        parser.h)