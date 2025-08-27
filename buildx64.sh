SRC_DIR="C:/Users/TrippR/OneDrive/Documents/REPOS/thread_pool"
BUILD_DIR="$SRC_DIR/build"

mkdir -p "$BUILD_DIR"
cd "$BUILD_DIR" || exit 1

cmake -DTHREAD_POOL_BUILD_AS_X32=TRUE "$SRC_DIR"
cmake --build .
mdir binx64
echo "Press any key to continue..."
read -n 1 -s