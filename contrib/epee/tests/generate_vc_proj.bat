mkdir build
cd build
cmake "-DBoost_USE_STATIC_LIBS=TRUE" -G "Visual Studio 12 Win64" ../src
cd ..
pause