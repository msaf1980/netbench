Build

```$
mkdir _release
cd _release 
cmake -DCMAKE_BUIL_TYPE=Release ..
make
```

Run ping-pong benchmark

```../benchmark/pingpong_bench.sh ./libevent/pingpong_buffered/event_pp ./boost_asio/pingpong/asio_pp```
