# StrikeMQ

Sub-millisecond, Kafka-compatible message broker written in C++20.
Zero dependencies. Single binary. Drop-in Kafka replacement for development and testing.

## Quick Start

```bash
docker run -p 9092:9092 -p 8080:8080 awneesh/strikemq
```

Any Kafka client can connect to `127.0.0.1:9092`. REST API available at `127.0.0.1:8080`.

## Why StrikeMQ?

| | Kafka | StrikeMQ |
|---|---|---|
| Binary size | ~200MB+ (JVM + libs) | 52KB |
| Startup time | 10-30 seconds | < 10ms |
| Idle CPU | 1-5% (JVM GC) | 0% |
| Memory | 1-2GB minimum | ~1MB |
| Dependencies | JVM, ZooKeeper/KRaft | None |

## Usage

```bash
# Produce messages
echo -e "hello\nworld" | kcat -b 127.0.0.1:9092 -P -t my-topic

# Consume messages
kcat -b 127.0.0.1:9092 -C -t my-topic -e

# Peek via REST API
curl localhost:8080/v1/topics/my-topic/messages?limit=10

# Produce via REST API
curl -X POST localhost:8080/v1/topics/my-topic/messages \
  -d '{"messages":[{"value":"hello"},{"key":"k1","value":"world"}]}'

# Broker info
curl localhost:8080/v1/broker
```

## REST API

| Method | Path | Description |
|--------|------|-------------|
| GET | `/v1/broker` | Broker info (version, uptime, config) |
| GET | `/v1/topics` | List all topics |
| GET | `/v1/topics/{name}` | Topic detail |
| DELETE | `/v1/topics/{name}` | Delete a topic |
| GET | `/v1/topics/{name}/messages` | Peek at messages |
| POST | `/v1/topics/{name}/messages` | Produce messages |
| GET | `/v1/groups` | List consumer groups |
| GET | `/v1/groups/{id}` | Group detail |

## Supported Kafka APIs

ApiVersions, Metadata, Produce, Fetch, ListOffsets, FindCoordinator, JoinGroup, SyncGroup, Heartbeat, LeaveGroup, OffsetCommit, OffsetFetch

## Ports

- **9092** — Kafka wire protocol
- **8080** — REST API

## Links

- [GitHub](https://github.com/awneesht/Strike-mq)
- [Documentation](https://github.com/awneesht/Strike-mq/blob/main/docs/PRODUCT.md)

## License

MIT
