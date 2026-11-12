// ============================================================================
//  src/memory_mcp_tool_server.cpp — Standalone MCP Memory Server
//  Shared MCP implementation: https://github.com/pooriayousefi/mcpxx
//  License: Apache-2.0
//
//  Protocol: MCP 2026-07-28 ONLY (stateless; every request carries _meta).
//  Transports: stdio (default) or Streamable HTTP.
//
//  Tools (read):
//    recall_memory, list_keys
//
//  Tools (write):
//    save_memory
//
//  Safety:
//    - Copy-on-write Json state shared via smart pointers (no raw pointers)
//    - Mutex-guarded snapshot reads and writes
//    - Observer watchers persist the full state to disk after every mutation
//    - Default store file: sauron_memory.json (working directory)
//    - Reads the previous mcpxx_memory.json file when the new store is absent
//    - Values are arbitrary Json (string, number, object, array, etc.)
//
//  Build:
//    clang++ -std=c++20 -O3 -pthread -I include src/memory_mcp_tool_server.cpp -o bin/memory_mcp_tool_server
//    clang++ -std=c++20 -O3 -pthread -I include src/memory_mcp_tool_server.cpp -o bin/memory_mcp_tool_server -lws2_32 (Windows)
//    g++     -std=c++20 -O3 -pthread -I include src/memory_mcp_tool_server.cpp -o bin/memory_mcp_tool_server
//
//  Usage:
//    ./bin/memory_mcp_tool_server                      (stdio; run from project root)
//    ./bin/memory_mcp_tool_server http 127.0.0.1 8097  (Streamable HTTP)
// ============================================================================

#include "pooriayousefi/utility/common.hpp"
#include "pooriayousefi/mcp/server.hpp"

#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <memory>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace pooriayousefi::mcp
{
    constexpr const char* DEFAULT_MEMORY_FILE = "sauron_memory.json";
    constexpr const char* LEGACY_MEMORY_FILE = "mcpxx_memory.json";

    Expected<std::string, Error> require_string_arg(const Json& args, const char* key)
    {
        Expected<std::string, Error> result{};

        if (!args.contains(key) || !args[key].is_string())
        {
            result.set_error(Error{
                error_code::INVALID_PARAMS,
                std::string("Missing or non-string '") + key + "' in arguments"
            });
        }
        else
        {
            result.set_value(args[key].get<std::string>());
        }

        return result;
    }

    class MemoryStore
    {
    public:
        using Watcher = std::function<bool(const Json&)>;

        explicit MemoryStore(const std::string& file_path)
            : file_path_{file_path}
            , data_{std::make_shared<Json>(Json::object())}
        {
            load();
        }

        void add_watch(Watcher w)
        {
            watchers_.push_back(std::move(w));
        }

        bool save_entry(const std::string& key, const Json& value)
        {
            std::lock_guard<std::mutex> lock(mutex_);
            auto new_state = std::make_shared<Json>(*data_);
            (*new_state)[key] = value;

            // Persist before publishing the new in-memory snapshot. A failed
            // write therefore leaves both the old disk and memory state intact.
            for (const auto& watcher : watchers_)
            {
                try
                {
                    if (!watcher(*new_state))
                    {
                        return false;
                    }
                }
                catch (const std::exception& failure)
                {
                    std::cerr << "Memory watcher error: " << failure.what() << std::endl;
                    return false;
                }
            }

            data_ = std::move(new_state);
            return true;
        }

        Json recall_entry(const std::string& key) const
        {
            std::shared_ptr<Json> state;
            {
                std::lock_guard<std::mutex> lock(mutex_);
                state = data_;
            }

            Json result = nullptr;
            if (state->contains(key))
            {
                result = (*state)[key];
            }

            return result;
        }

        std::vector<std::string> list_keys() const
        {
            std::shared_ptr<Json> state;
            {
                std::lock_guard<std::mutex> lock(mutex_);
                state = data_;
            }

            std::vector<std::string> keys;
            keys.reserve(state->size());
            for (const auto& [k, v] : state->items())
            {
                keys.push_back(k);
            }

            return keys;
        }

    private:
        void load()
        {
            std::ifstream file(file_path_);
            if (!file)
            {
                file.clear();
                file.open(LEGACY_MEMORY_FILE);
            }
            if (file)
            {
                Json loaded = Json::parse(file, nullptr, false);
                if (!loaded.is_discarded() && loaded.is_object())
                {
                    std::lock_guard<std::mutex> lock(mutex_);
                    data_ = std::make_shared<Json>(std::move(loaded));
                }
            }
        }

        std::string file_path_;
        mutable std::mutex mutex_;
        std::shared_ptr<Json> data_;
        std::vector<Watcher> watchers_;
    };

    // ------------------------------------------------------------------ tools

    Expected<Json, Error> tool_save_memory(MemoryStore& store, const Json& args)
    {
        Expected<Json, Error> result{};

        auto key_or_error = require_string_arg(args, "key");
        if (!key_or_error.has_value())
        {
            result.set_error(key_or_error.get_error());
        }
        else if (!args.contains("value"))
        {
            result.set_error(Error{error_code::INVALID_PARAMS, "Missing 'value' in arguments."});
        }
        else
        {
            if (!store.save_entry(key_or_error.get_value(), args["value"]))
            {
                result.set_error(Error{
                    error_code::INTERNAL_ERROR,
                    "Failed to persist memory snapshot; the previous state was retained."
                });
            }
            else
            {
                result.set_value(Json{
                    {"message", "Memory saved successfully."},
                    {"key", key_or_error.get_value()}
                });
            }
        }

        return result;
    }

    Expected<Json, Error> tool_recall_memory(MemoryStore& store, const Json& args)
    {
        Expected<Json, Error> result{};

        auto key_or_error = require_string_arg(args, "key");
        if (!key_or_error.has_value())
        {
            result.set_error(key_or_error.get_error());
        }
        else
        {
            Json value = store.recall_entry(key_or_error.get_value());
            result.set_value(Json{
                {"key", key_or_error.get_value()},
                {"found", !value.is_null()},
                {"value", value}
            });
        }

        return result;
    }

    Expected<Json, Error> tool_list_keys(MemoryStore& store, const Json& /*args*/)
    {
        Expected<Json, Error> result{};

        auto keys = store.list_keys();
        Json keys_array = Json::array();
        for (const auto& key : keys)
        {
            keys_array.push_back(key);
        }

        result.set_value(Json{
            {"keys", std::move(keys_array)},
            {"count", static_cast<std::int64_t>(keys.size())}
        });

        return result;
    }
}

int main(int argc, char* argv[])
{
    using namespace pooriayousefi;
    using namespace pooriayousefi::mcp;

    MemoryStore store{DEFAULT_MEMORY_FILE};

    store.add_watch(
        [](const Json& new_state) -> bool
        {
            namespace fs = std::filesystem;
            const fs::path destination{DEFAULT_MEMORY_FILE};
            const fs::path temporary{std::string(DEFAULT_MEMORY_FILE) + ".tmp"};

            {
                std::ofstream file(temporary, std::ios::binary | std::ios::trunc);
                if (!file)
                {
                    std::cerr << "Failed to open temporary memory snapshot: " << temporary << std::endl;
                    return false;
                }

                file << new_state.dump(2);
                file.flush();
                if (!file)
                {
                    std::cerr << "Failed to write memory snapshot: " << temporary << std::endl;
                    file.close();
                    std::error_code ignored;
                    fs::remove(temporary, ignored);
                    return false;
                }
            }

#ifdef _WIN32
            const bool replaced = ::MoveFileExW(
                temporary.c_str(),
                destination.c_str(),
                MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH
            ) != 0;
            if (!replaced)
            {
                const DWORD error = ::GetLastError();
                std::cerr << "Failed to replace memory snapshot (Windows error "
                          << error << ")." << std::endl;
                std::error_code ignored;
                fs::remove(temporary, ignored);
                return false;
            }
#else
            if (std::rename(temporary.c_str(), destination.c_str()) != 0)
            {
                std::cerr << "Failed to atomically replace memory snapshot: "
                          << destination << std::endl;
                std::error_code ignored;
                fs::remove(temporary, ignored);
                return false;
            }
#endif
            return true;
        }
    );

    Server server{"mcp-tool-memory-server", "1.0.0"};
    server.set_instructions(    "Memory tools: read (recall_memory, list_keys) and write (save_memory). Values are arbitrary Json; every mutation persists the full state to sauron_memory.json in the working directory, which is restored on startup. recall_memory returns null for unknown keys; list_keys reports every stored key with the total count.");

    const Json key_prop = {
        {"type", "string"},
        {"description", "The unique memory key."}
    };

    Tool recall_memory{};
    recall_memory.name = "recall_memory";
    recall_memory.title = "Recall memory";
    recall_memory.description = "Recalls a value from long-term memory using its key. value is null if not found.";
    recall_memory.input_schema = Json{
        {"type", "object"},
        {"properties", {{"key", key_prop}}},
        {"required", Json::array({"key"})}
    };
    recall_memory.handler = [&store](const Json& args) -> Expected<Json, Error>
    {
        return tool_recall_memory(store, args);
    };
    server.add_tool(std::move(recall_memory));

    Tool list_keys{};
    list_keys.name = "list_keys";
    list_keys.title = "List keys";
    list_keys.description = "Lists all keys currently saved in long-term memory.";
    list_keys.input_schema = Json{
        {"type", "object"},
        {"properties", Json::object()},
        {"required", Json::array()}
    };
    list_keys.handler = [&store](const Json& args) -> Expected<Json, Error>
    {
        return tool_list_keys(store, args);
    };
    server.add_tool(std::move(list_keys));

    Tool save_memory{};
    save_memory.name = "save_memory";
    save_memory.title = "Save memory";
    save_memory.description = "Saves a value (any Json type) to long-term memory using a specific key. Persists across sessions.";
    save_memory.input_schema = Json{
        {"type", "object"},
        {"properties", {
            {"key", key_prop},
            {"value", {{"description", "The Json value to save (string, number, object, array, etc.)."}}}
        }},
        {"required", Json::array({"key", "value"})}
    };
    save_memory.handler = [&store](const Json& args) -> Expected<Json, Error>
    {
        return tool_save_memory(store, args);
    };
    server.add_tool(std::move(save_memory));

    const int exit_code = server.run_cli(argc, argv, 8097);
    return exit_code;
}