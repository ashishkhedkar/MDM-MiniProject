// Module 1a: RFC-4180 style CSV parser (quotes, CRLF, BOM, '#' comment lines).
#pragma once
#include <string>
#include <vector>

namespace csv {

using Row = std::vector<std::string>;

struct Parsed {
    std::vector<Row> rows;
    std::vector<size_t> lines;  // 1-based source line where each row starts
    std::string error;
};

inline Parsed parse(const std::string& in) {
    Parsed out;
    size_t i = 0;
    if (in.size() >= 3 && static_cast<unsigned char>(in[0]) == 0xEF &&
        static_cast<unsigned char>(in[1]) == 0xBB && static_cast<unsigned char>(in[2]) == 0xBF)
        i = 3;

    size_t line = 1, rowLine = 1;
    Row row;
    std::string field;
    bool inQuotes = false, rowStarted = false;

    auto finishRow = [&]() {
        row.push_back(field);
        field.clear();
        out.rows.push_back(row);
        out.lines.push_back(rowLine);
        row.clear();
        rowStarted = false;
    };

    for (; i < in.size(); ++i) {
        char c = in[i];
        if (inQuotes) {
            if (c == '"') {
                if (i + 1 < in.size() && in[i + 1] == '"') { field += '"'; ++i; }
                else inQuotes = false;
            } else {
                if (c == '\n') ++line;
                field += c;
            }
            continue;
        }
        if (!rowStarted && c == '#') {  // comment line
            while (i < in.size() && in[i] != '\n') ++i;
            ++line;
            continue;
        }
        if (!rowStarted && (c == '\r' || c == '\n')) {  // blank line
            if (c == '\n') ++line;
            continue;
        }
        if (!rowStarted) { rowStarted = true; rowLine = line; }
        if (c == '"' && field.empty()) inQuotes = true;
        else if (c == ',') { row.push_back(field); field.clear(); }
        else if (c == '\r') { /* ignore */ }
        else if (c == '\n') { finishRow(); ++line; }
        else field += c;
    }
    if (inQuotes) {
        out.error = "Malformed CSV: a quoted field starting near line " + std::to_string(rowLine) +
                    " is never closed.";
        return out;
    }
    if (rowStarted) finishRow();
    return out;
}

}  // namespace csv
