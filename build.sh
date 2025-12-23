#!/bin/bash
# Ta bort gammal build för att vara säker på att cachen rensas

echo "--- Configuring (DEBUG MODE) ---"
# Vi tvingar CMake att aktivera Debug-flaggor (-g)
cmake -DCMAKE_BUILD_TYPE=Debug -S . -B build

echo "--- Building ---"
cmake --build build