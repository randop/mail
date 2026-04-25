#pragma once

#include <chrono>
#include <cstdlib>
#include <random>
#include <seastar/core/lowres_clock.hh>
#include <seastar/core/sstring.hh>

namespace uuid_helpers {
seastar::sstring generate_v7();
}