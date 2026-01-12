#include "logger/LogEvent.h"
#include "logger/LogFormatter.h"

#include <algorithm>
#include <cassert>
#include <cstddef>
#include <unordered_map>
#include <functional>

namespace{

enum class ParseState{
    NORMAL,
    PATTERN,
    SUBPATTERN
};

}   //namespace

class PatternItemFacade;

using ItemFactoryFunc = std::function<Sptr<PatternItemFacade>()>;

auto RegisterItemFactoryFunc() -> std::unordered_map<std::string, ItemFactoryFunc>;


