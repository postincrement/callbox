cd ../opalvoip-opal/build
make 
make openphone
#cmake -S . -B build -DOPAL_PTLIB_DIR=../opalvoip-ptlib/build -DOPAL_BUILD_SAMPLES=ON
#cmake --build build -j4 
#cmake --build build --target openphone -j4
