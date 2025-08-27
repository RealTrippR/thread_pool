cmake -S . -B build-debug -DCMAKE_BUILD_TYPE=Debug


BUILD_DIR="./build"
mkdir -p "$BUILD_DIR"
cd "$BUILD_DIR" || exit 1

cmake -DTHREAD_POOL_BUILD_AS_X32=TRUE "$SRC_DIR"
cmake --build .
mdir binx64
echo "Press any key to continue..."
read -n 1 -s