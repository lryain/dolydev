msgbus_cpp - minimal C++ MessageBus client (prototype)

This folder contains a small prototype C++ MessageBus client library with:

- Message (simple struct + serialize/deserialize)
- Publisher (UDP) - send serialized messages
- Subscriber (UDP) - receive serialized messages and invoke handler
- Examples: publish_example, subscribe_example

Build:

mkdir build && cd build
cmake ..
make -j$(nproc)

Run subscriber in one terminal, publisher in another:

./subscribe_example
./publish_example

This is a minimal prototype. Replace the string serialization with msgpack in next iterations.
