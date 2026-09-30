// legacy_format.hpp — number formatting the way std::ostream prints it (6 significant digits).
//
// Several macros wrote some columns with `csv << value` (ostream, precision 6) and others with
// fmt::format("{}") (shortest exact representation). Writers reproduce the ostream columns with
// stream_text so the bytes match. Trap: use it only where the macro used ostream; the column
// audit lists which roles are affected. Unifying to fmt formatting is a contract change.
#pragma once

#include <sstream>
#include <string>

inline std::string stream_text(double value) {
    std::ostringstream text;
    text << value;
    return text.str();
}

inline std::string stream_text(float value) {
    std::ostringstream text;
    text << value;
    return text.str();
}
