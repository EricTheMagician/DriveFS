//https://stackoverflow.com/questions/44083239/using-boost-parse-datetime-string-with-single-digit-hour-format/44091087#44091087
#pragma once

#include <string>
#include <chrono>
#include <cstdint>
#include <list>

namespace adaptive { namespace datetime {

/*
 * Multi-format capable date time parser
 *
 * Intended to be seeded with a list of supported formats, in order of
 * preference. By default, parser is not adaptive (mode is `fixed`).
 *
 * In adaptive modes the format can be required to be
 *
 *  - sticky (consistently reuse the first matched format)
 *  - ban_failed (remove failed patterns from the list; banning only occurs
 *    on successful parse to avoid banning all patterns on invalid input)
 *  - mru (preserves the list but re-orders for performance)
 *
 * CAUTION:
 *   If formats are ambiguous (e.g. mm-dd-yyyy vs dd-mm-yyyy) allowing
 *   re-ordering results in unpredictable results.
 *   => Only use `mru` when there are no ambiguous formats
 *
 * NOTE:
 *   The function object is stateful. In algorithms, pass it by reference
 *   (`std::ref(obj)`) to avoid copying the patterns and to ensure correct
 *   adaptive behaviour
 *
 * NOTE:
 *   - use %z before %Z to correctly handle [-+]hhmm POSIX TZ indications
 *   - adaptive_parser is thread-safe as long as it's not in any adaptive
 *     mode (the only allowed flag is `full_match`)
 */
        class adaptive_parser {
        public:
            typedef std::list<std::string> list_t;

            enum mode_t {
                fixed      = 0, // not adapting; keep trying same formats in same order
                sticky     = 1, // re-use first successful format consistently
                ban_failed = 2, // forget formats that have failed
                mru        = 4, // optimize by putting last known good in front
                full_match = 8, // require full matches to be accepted
            };

            adaptive_parser(mode_t m = full_match);
            adaptive_parser(mode_t m, list_t formats);

            // returns seconds since epoch
            std::chrono::seconds operator()(std::string);

        private:
            mode_t _mode;
            list_t _formats;
        };

        static inline adaptive_parser::mode_t operator|(adaptive_parser::mode_t lhs, adaptive_parser::mode_t rhs) {
            return static_cast<adaptive_parser::mode_t>(static_cast<int>(lhs) | static_cast<int>(rhs));
        }

    } }