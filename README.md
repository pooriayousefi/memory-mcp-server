# memory-mcp-server

A standalone C++20 Model Context Protocol (MCP) server for persistent JSON key/value memory.

## Tools

- `recall_memory`
- `list_keys`
- `save_memory`

## Dependency

The shared MCP protocol, server, transport, process, JSON, and HTTP implementation is provided by [mcpxx](https://github.com/pooriayousefi/mcpxx), pinned as a Git submodule at commit `8d3e971072c263dd5132c5b48e44c15926d21190` under `external/mcpxx`.

## Clone and build

```sh
git clone --recurse-submodules https://github.com/pooriayousefi/memory-mcp-server.git
cd memory-mcp-server
make
```

Or compile directly:

```sh
clang++ -std=c++20 -O2 -pthread -I external/mcpxx/include src/memory_mcp_tool_server.cpp -o bin/memory_mcp_tool_server
```

On Windows with MinGW, add `-lws2_32`.

## Run

Stdio (default): `./bin/memory_mcp_tool_server`

Streamable HTTP: `./bin/memory_mcp_tool_server http 127.0.0.1 8097`

The server stores its JSON state in `sauron_memory.json` in the working directory. If that file is absent, it can load the legacy `mcpxx_memory.json`; subsequent writes use `sauron_memory.json`.

The HTTP endpoint is `/mcp`. Bind to loopback unless suitable network controls are configured; HTTP transport is not an authentication boundary.

## License

Apache-2.0. See [LICENSE](LICENSE).
