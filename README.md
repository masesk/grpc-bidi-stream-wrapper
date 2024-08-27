# grpc-bidi-wrapper
A wrapper utility to simplify bidirectional grpc stream with a server/client wrapper.

## Features
1. Asynchronous callback API with bidirectional stream.
2. Client detect connection loss with server and attempts to re-establish connection every 2 seconds.
3. Reliable writes from clients that store messages in the buffer until the server comes back online.

## Requirements
1. C++ Compiler (g++, clang, msbuild) with C++ 14 or higher support. 

## Setup
1. Add `grpc_bidi_wrapper.hpp` to your project's include directory.
> **Warning**
>
> grpc_bidi_wrapper uses the gRPC includes and assumes gRPC is properly linked with the target it is building with.

2. The `grpc_bidi_wrapper.hpp` uses `proto/bid-chat.proto`, and overrides the corresponding `Chat` rpc.
https://github.com/masesk/grpc-bidi-stream-wrapper/blob/cc544c6f265f165eddca0becc7a8f6d2cb069385/proto/bidi_chat.proto#L6
If you rename the RPC, make sure to override

   https://github.com/masesk/grpc-bidi-stream-wrapper/blob/e4989ef5a491fbd220b1777acb4c3754b243f554/example/chat_server.cc#L18
   
   As well as changing the `initialize_callback` in `chat_client.cc` to use the correct RPC name instead of Chat:
   https://github.com/masesk/grpc-bidi-stream-wrapper/blob/d592041e4cf7e015409a32f4ed0baf8e8a8737ab/example/chat_client.cc#L44


## Examples

> **Warning**
>
> Examples uses conan to install or build dependencies and any nested dependencies

### Requirements
1. Python 3.6 or higher
2. g++ with C++ 14 or higher support
3. CMake
4. make

### Build
1. Create Python Virtual Environment (this can be skipped if you don't mind installing conan on your main pip)
```
python3 -m venv .venv
```

2. Activate Python Virtual Environment (this can be skipped if you don't mind installing conan on your main pip)
```
. .venv/bin/activate
```

3. Install conan
```
pip install conan==2.2.2
```

4. Detect build environment
```
conan profile detect --force
```

5. Install dependencies
```
conan install . --output-folder=build --build=missing
```

6. Run CMake
```
cd build && cmake .. -DCMAKE_TOOLCHAIN_FILE=conan_toolchain.cmake -DCMAKE_BUILD_TYPE=Release
```

7. Run Make
```
make -j 4
```

### Run

1. Run server
```
build/chat_server
```

2. Run Client 1
```
build/chat_client
```

3. Run client 2
```
build/chat_client
```

For each client, input a username and press enter.
Afterwards, you can type and press enter to send messages.
