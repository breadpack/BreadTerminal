#ifndef TERMCORE_HISTORY_PROVIDER_H
#define TERMCORE_HISTORY_PROVIDER_H

#include <string>
#include <vector>

namespace termcore {

class HistoryProvider {
public:
    void addEntry(const std::string& command);
    std::string suggest(const std::string& prefix) const;
    size_t size() const { return history_.size(); }
    void clear() { history_.clear(); }

private:
    std::vector<std::string> history_;
    static constexpr size_t kMaxHistory = 1000;
};

} // namespace termcore

#endif // TERMCORE_HISTORY_PROVIDER_H
