# Changelog

## v0.1.0 — TCP Networking & Kafka Client Compatibility

### New Features

- **TCP server with kqueue/epoll event loop** — Non-blocking, event-driven networking replaces the busy-spin placeholder. 0% CPU when idle.
  - `include/network/tcp_server.h` — TcpServer class with per-connection buffering
  - `src/network/tcp_server.cpp` — Platform-specific implementation (kqueue on macOS, epoll on Linux)

- **Full request routing** — `main.cpp` rewritten to wire TcpServer, RequestRouter, and PartitionLog together with produce and metadata handlers.

- **Metadata handler with auto-topic creation** — Topics are automatically created when requested via Metadata API, matching Kafka's `auto.create.topics.enable` behavior.

- **MetadataHandler callback** added to `RequestRouter` — Accepts requested topic names and returns broker/topic info.

### Bug Fixes

#### ApiVersions v3 Flexible Encoding (Critical)

**Problem:** librdkafka v2.13.0 sends `ApiVersions v3` which uses Kafka's "flexible versions" protocol (compact arrays, tagged fields). Our response included a header `tagged_fields` byte, but the Kafka protocol spec has a special exception: ApiVersions responses must **not** include header tagged_fields for backwards compatibility. This extra byte shifted all subsequent fields by 1, causing librdkafka to parse the compact array length as a garbage value (~34GB), triggering a `malloc` assertion crash (`rd_malloc` returned NULL).

**Fix:** Removed the header `tagged_fields` byte from ApiVersions v3 responses. Added version-aware encoding: v0-v2 uses standard format, v3+ uses compact arrays and per-entry tagged fields but no header tags.

**Root cause location:** `src/protocol/kafka_codec.cpp` — `encode_api_versions_response()`.

#### API Version Maximums Too High

**Problem:** The ApiVersions response advertised support for Metadata v0-v12, Produce v0-v9, etc. librdkafka would use the highest advertised version, but our encoder only produced v0 format responses. Metadata v5+ adds fields like `throttle_time_ms`, `rack`, `controller_id`, `is_internal`, and `offline_replicas` that we don't encode, causing "Partial response" parse errors.

**Fix:** Lowered advertised API version maximums to match what the encoder actually implements. Metadata is now v0 only. Added a full set of 13 consumer-group and admin APIs (FindCoordinator, JoinGroup, Heartbeat, LeaveGroup, SyncGroup, CreateTopics) to the ApiVersions response to satisfy librdkafka's feature detection.

#### IPv6 Connection Failure

**Problem:** The Metadata response returned `localhost` as the broker host. librdkafka resolved `localhost` to IPv6 `[::1]` first, but the broker only binds to IPv4 `0.0.0.0:9092`. Produce requests failed with "Connection refused" on the IPv6 address.

**Fix:** Changed the Metadata response to return `127.0.0.1` instead of `localhost` when the bind address is `0.0.0.0`.

#### Topic Auto-Creation Missing

**Problem:** kcat sends a Metadata request for the target topic before producing. Our Metadata handler only returned already-known topics (empty list for new topics). kcat waited indefinitely for the topic to appear in metadata, never sending the actual produce request.

**Fix:** The Metadata handler now auto-creates requested topics by calling `get_or_create_log()` for each topic in the Metadata request, then returns them in the response.

#### stdout Buffering

**Problem:** The startup banner was buffered and not visible when running the broker.

**Fix:** Added `std::flush` after the banner and all log output.

#### TopicPartitionHash libc++ ABI Issue

**Problem:** The `TopicPartitionHash` struct used `std::hash<std::string>` which, under Homebrew LLVM's clang, generates calls to `std::__1::__hash_memory()` — a symbol present in LLVM's libc++ but not in Apple's system libc++. This caused a linker error: "Undefined symbols: std::__1::__hash_memory".

**Fix:** Replaced `std::hash<std::string>` with a manual FNV-1a hash implementation that has no libc++ dependency.

### Files Changed

| File | Change |
|------|--------|
| `include/network/tcp_server.h` | **New** — TcpServer class |
| `src/network/tcp_server.cpp` | **New** — kqueue/epoll implementation |
| `src/main.cpp` | **Rewritten** — Broker orchestration with handlers |
| `include/protocol/kafka_codec.h` | Added MetadataInfo struct, MetadataHandler, api_version param |
| `src/protocol/kafka_codec.cpp` | v3 ApiVersions encoding, Metadata routing, version-aware dispatch |
| `include/core/types.h` | FNV-1a hash for TopicPartitionHash |
| `tests/kafka_codec_test.cpp` | Updated test for new api_version parameter |

### Verified With

- `kcat -b 127.0.0.1:9092 -L` — Lists broker and topics
- `echo "hello" | kcat -b 127.0.0.1:9092 -P -t test-topic` — Produces messages
- `lsof -i :9092` — Confirms listening socket
- `ps aux` — Confirms 0% CPU when idle
- Python raw socket test — Validates ApiVersions v0 and v3 response formats
