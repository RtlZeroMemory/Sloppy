if(NOT DEFINED PROJECT_SOURCE_DIR)
    message(FATAL_ERROR "PROJECT_SOURCE_DIR is required")
endif()

function(require_substring haystack needle description)
    string(FIND "${haystack}" "${needle}" found_index)
    if(found_index EQUAL -1)
        message(FATAL_ERROR "${description}: ${needle}")
    endif()
endfunction()

set(redis_basic_app "${PROJECT_SOURCE_DIR}/examples/redis-basic/app.js")
set(redis_basic_readme "${PROJECT_SOURCE_DIR}/examples/redis-basic/README.md")
set(redis_cache_app "${PROJECT_SOURCE_DIR}/examples/redis-cache/app.js")
set(redis_cache_readme "${PROJECT_SOURCE_DIR}/examples/redis-cache/README.md")
set(redis_locks_app "${PROJECT_SOURCE_DIR}/examples/redis-locks/app.js")
set(redis_locks_readme "${PROJECT_SOURCE_DIR}/examples/redis-locks/README.md")
set(testservices_redis_readme "${PROJECT_SOURCE_DIR}/examples/testservices-redis/README.md")

foreach(required_file IN ITEMS
        "${redis_basic_app}" "${redis_basic_readme}"
        "${redis_cache_app}" "${redis_cache_readme}"
        "${redis_locks_app}" "${redis_locks_readme}"
        "${testservices_redis_readme}")
    if(NOT EXISTS "${required_file}")
        message(FATAL_ERROR "Missing Redis example file: ${required_file}")
    endif()
endforeach()

file(READ "${redis_basic_app}" redis_basic_source)
file(READ "${redis_basic_readme}" redis_basic_readme_source)
file(READ "${redis_cache_app}" redis_cache_source)
file(READ "${redis_cache_readme}" redis_cache_readme_source)
file(READ "${redis_locks_app}" redis_locks_source)
file(READ "${redis_locks_readme}" redis_locks_readme_source)
file(READ "${testservices_redis_readme}" testservices_redis_readme_source)

foreach(required_pattern IN ITEMS
        "Redis.client"
        "schema.object"
        "redis.set("
        "redis.get("
        "redis.setText"
        "redis.setBytes"
        "redis.script")
    require_substring("${redis_basic_source}" "${required_pattern}" "redis-basic app.js is missing expected Redis API shape")
endforeach()

foreach(required_pattern IN ITEMS
        "requires a Redis server"
        "Sloppy outbound network bridge"
        "no npm Redis client"
        "no cluster, sentinel, pub/sub, streams, or modules contract")
    require_substring("${redis_basic_readme_source}" "${required_pattern}" "redis-basic README.md is missing required boundary text")
endforeach()

foreach(required_pattern IN ITEMS
        "Cache.redis"
        "Redis.client"
        "getOrCreate"
        "tags:"
        "app.services.addCache"
        "ctx.services.get(\"cache.default\")")
    require_substring("${redis_cache_source}" "${required_pattern}" "redis-cache app.js is missing expected Cache API shape")
endforeach()

foreach(required_pattern IN ITEMS
        "Redis-backed cache"
        "does not provide an in-memory fallback"
        "same-process misses only"
        "cluster, sentinel, and Redis streams")
    require_substring("${redis_cache_readme_source}" "${required_pattern}" "redis-cache README.md is missing required boundary text")
endforeach()

foreach(required_pattern IN ITEMS
        "Redis.locks"
        "locks.acquire"
        "ttlMs:"
        "waitTimeoutMs:"
        "lease.extend")
    require_substring("${redis_locks_source}" "${required_pattern}" "redis-locks app.js is missing expected lock API shape")
endforeach()

foreach(required_pattern IN ITEMS
        "single-key Redis lease"
        "not Redlock"
        "owner tokens are internal and redacted"
        "cluster and sentinel behavior are not claimed")
    require_substring("${redis_locks_readme_source}" "${required_pattern}" "redis-locks README.md is missing required boundary text")
endforeach()

foreach(required_pattern IN ITEMS
        "TestServices.redis"
        "Docker CLI"
        "Sloppy outbound network bridge"
        "redis.env()"
        "redis.reset()")
    require_substring("${testservices_redis_readme_source}" "${required_pattern}" "testservices-redis README.md is missing required TestServices Redis shape")
endforeach()
