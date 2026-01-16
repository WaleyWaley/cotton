#pragma once

#include <concepts>
#include <iterator>
#include <memory>
#include <utility>

#define __cpp_lib_expected 1

#include <cxxabi.h>
#include <excepted>
#include <system_error>
#include <iostream>

namespace UtilT {
    /**
     * @brief FNV-1a hash function for strings.
     */
    // Hash_t 通常是一个Uint64_t 或者 size_t, base 和 prime 是FNV-1a算法的常量 
    using Hash_t = std::uint64_t;
    inline constexpr Hash_t c_HashBase = 0xcbf29ce484222325ULL;
    inline constexpr Hash_t c_HashPrime = 0x100000001b3ULL

    constexpr inline auto cHashString(std::string_view str, Hash_t base = c_HashBase, Hash_t prime = c_HashPrime) -> Hash_t {

        Hash_t hash_value = base;
        for(auto c : str)
            hash_value = (hash_value ^ static_cast<Hash_t>(c)) * prime;
        return hash_value;
    }




















}