BUILD_DIR="./build-debug-x32"
mkdir -p "$BUILD_DIR"
TO_DIR="bin/debug-x32"
mkdir -p "$TO_DIR"

mkdir -p debug
cmake -G "MinGW Makefiles" -S . -B "$BUILD_DIR" -DCMAKE_BUILD_TYPE=Debug -DTHREAD_POOL_BUILD_AS_X32=True
cmake --build "$BUILD_DIR"

cp "$BUILD_DIR/CMakeFiles/Thread_Pool.dir/thread_pool/src/threadpool.c.obj" "$TO_DIR"
cp "$BUILD_DIR/CMakeFiles/Thread_Pool.dir/thread_pool/src/threadpool.c.obj.d" "$TO_DIR"

echo "Press any key to continue..."
read -n 1 -s