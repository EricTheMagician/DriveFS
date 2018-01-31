//
// Created by eric on 29/01/18.
//

#include "adaptive_time_parser.h"
#include "time.h"
#include <vector>
#include <algorithm>
#include <cassert>
#include <cstring>
#include <iostream>

namespace {
    enum level { LOG_DEBUG };
    static std::ostream s_devnull { nullptr };

    struct {
        std::ostream& log(int) const {
#ifdef NDEBUG
            return s_devnull;
#else
            return std::cerr;
#endif
        };
    } s_trace;
}

namespace adaptive { namespace datetime {

    adaptive_parser::adaptive_parser(mode_t m)
        : _mode(m), _formats {
// use EOL_MARK to debug patterns when you suspect ambiguity or partial matches
#define EOL_MARK "" // " EOL_MARK"
// use %z before %Z to correctly handle [-+]hhmm POSIX time zone offsets
#if __GLIBC__ == 2 && __GLIBC_MINOR__ <= 15
    // ubuntu 12.04 used eglibc and doesn't parse all bells and whistles
#define WITH_TZ(prefix, suffix) prefix " %z" suffix, prefix " %Z" suffix, prefix " Z" suffix, prefix " (UTC)" suffix, prefix suffix
#else
#define WITH_TZ(prefix, suffix) prefix " %z" suffix, prefix " %Z" suffix, prefix suffix
#endif
            WITH_TZ("%Y-%m-%dT%H:%M:%S.%f", EOL_MARK),
            WITH_TZ("%Y-%m-%dT%H:%M:%S", EOL_MARK),
            WITH_TZ("%Y-%m-%dT%H:%M", EOL_MARK),
            //
            WITH_TZ("%Y-%m-%dT%I:%M:%S.%f %p", EOL_MARK),
            WITH_TZ("%Y-%m-%dT%I:%M:%S %p", EOL_MARK),
            WITH_TZ("%Y-%m-%dT%I:%M %p", EOL_MARK),
            //
            WITH_TZ("%Y-%m-%d%n%H:%M:%S", EOL_MARK),
            WITH_TZ("%Y-%m-%d%n%I:%M:%S %p", EOL_MARK),
            //
            WITH_TZ("%a %b %d %H:%M:%S %Y", EOL_MARK),
            WITH_TZ("%a %b %d %I:%M:%S %p %Y", EOL_MARK),
            //
            WITH_TZ("%a %d %b %H:%M:%S %Y", EOL_MARK),
            WITH_TZ("%a %d %b %I:%M:%S %p %Y", EOL_MARK),
            //
            WITH_TZ("%a, %b %d %H:%M:%S %Y", EOL_MARK),
            WITH_TZ("%a, %b %d %I:%M:%S %p %Y", EOL_MARK),
            //
            WITH_TZ("%a, %d %b %H:%M:%S %Y", EOL_MARK),
            WITH_TZ("%a, %d %b %I:%M:%S %p %Y", EOL_MARK),
            //////
            WITH_TZ("%a %d %b %Y %H:%M:%S", EOL_MARK),
            WITH_TZ("%a %d %b %Y %I:%M:%S %p", EOL_MARK),
            //
            WITH_TZ("%a, %d %b %Y %H:%M:%S", EOL_MARK),
            WITH_TZ("%a, %d %b %Y %I:%M:%S %p", EOL_MARK),
#undef WITH_TZ
            /*
             * HUMAN DATE:
             *
             * This pattern would ambiguate the "%s" one (sadly, because it
             * leads to obviously bogus results like parsing "1110871987" into
             * "2063-04-24 16:25:59" (because "1110-8-7T19:8:7" matches
             * "%Y-%m-%dT%H:%M:%S %Z" somehow...).
             *
             * We work around this issue by normalizing detected
             * 'yyyyMMddhhmmss' human dates into iso format as a preprocessing
             * step.
             */
            //"%Y %m %d %H %M %S" EOL_MARK,

            // epoch seconds
            "@%s" EOL_MARK,
            "%s" EOL_MARK,
           }
    { }

    adaptive_parser::adaptive_parser(mode_t m, list_t formats)
        : _mode(m), _formats(std::move(formats))
    { }

    std::chrono::seconds adaptive_parser::operator()(std::string input) {
        if (_formats.empty()) throw std::invalid_argument("No candidate patterns in datetime::adaptive_parser");
        if (input.empty()) throw std::invalid_argument("Empty input cannot be parsed as a date time");

        //detail::normalize_tz(input);
        //detail::normalize_tz_utc_w_offset_re(input);
        //detail::normalize_date_sep(input);
        //detail::normalize_human_date(input);
        //detail::normalize_redundant_timezone_description(input);
        input += EOL_MARK;

        std::vector<list_t::iterator> failed;

        bool matched = false;
        struct tm time_struct;

        auto pattern = _formats.begin();
        for (; !matched && pattern != _formats.end(); ++pattern) {
            memset(&time_struct, 0, sizeof(time_struct));
            auto tail = ::strptime(input.c_str(), pattern->c_str(), &time_struct);

            matched = tail;
            //if (matched) s_trace.log(LOG_DEBUG) << "Input '" << input << "' successfully matched pattern '" << *pattern << "' leaving '" << tail << "'\n";

            if (_mode & full_match) {
                while (tail && *tail && std::isspace(*tail))
                    ++tail; // skip trailing whitespace
                matched &= tail && !*tail;
            }

            if (matched)
                break;

            if (_mode & ban_failed)
                failed.push_back(pattern);
        }

        if (matched) {
            for (auto to_ban : failed) {
                s_trace.log(LOG_DEBUG) << "Banning failed datetime pattern: " << *to_ban << "\n";
                _formats.erase(to_ban);
            }

            if (_mode & sticky) {
                s_trace.log(LOG_DEBUG) << "Made succeeding datetime pattern sticky: " << *pattern << "\n";
                _formats = { *pattern };
            }

            if ((_mode & mru) && pattern != _formats.begin()) {
                assert(pattern != _formats.end()); // inconsistent with `matched==true`

                s_trace.log(LOG_DEBUG) << "Promote succeeding datetime pattern to the top: " << *pattern << "\n";
                std::rotate(_formats.begin(), pattern, std::next(pattern));
            }
#ifdef __FreeBSD__
            auto raw = (time_struct.tm_gmtoff)? mktime(&time_struct) : timegm(&time_struct);
            return std::chrono::seconds(raw);
#else
            long offset = time_struct.tm_gmtoff;
            return std::chrono::seconds(timegm (&time_struct) - offset);
#endif
        }

        s_trace.log(LOG_DEBUG) << "Failed to parse datetime input '" << input << "' with " << _formats.size() << " patterns\n";
        throw std::runtime_error("Input cannot be parsed as a date time");
    }

} }