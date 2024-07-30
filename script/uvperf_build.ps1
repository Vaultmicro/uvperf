#!/usr/bin/env pwsh

cd ../build
cmake .. -G "MinGW Makefiles"
cmake --build .
#at the very first
#mingw32-make
cd ../script