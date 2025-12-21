#!/bin/bash
mkdir -p build

echo "--- Configuring ---"
cmake -S . -B build

echo "--- Building ---"
cmake --build build

echo "--- Running ---"
./build/MiniDaxa