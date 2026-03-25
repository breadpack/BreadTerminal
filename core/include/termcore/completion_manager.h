#ifndef TERMCORE_COMPLETION_MANAGER_H
#define TERMCORE_COMPLETION_MANAGER_H

#include "termcore/history_provider.h"
#include <functional>
#include <string>
#include <vector>

namespace termcore {

class CompletionManager {
public:
    struct Provider {
        std::string name;
        int priority = 50;
        std::function<std::string(const std::string& input,
                                   const std::string& cwd)> getSuggestion;
    };

    CompletionManager();

    void registerProvider(Provider provider);
    void removeProvider(const std::string& name);

    void onInputChanged(const std::string& currentInput,
                        const std::string& cwd);

    void setSuggestion(const std::string& providerName,
                       const std::string& suggestion);

    const std::string& ghostText() const { return ghostText_; }
    bool hasGhostText() const { return !ghostText_.empty(); }

    std::string acceptFull();
    std::string acceptWord();

    void clear();

    void setEnabled(bool enabled) { enabled_ = enabled; }
    bool isEnabled() const { return enabled_; }

    HistoryProvider& historyProvider() { return historyProvider_; }

private:
    std::vector<Provider> providers_;
    HistoryProvider historyProvider_;
    std::string currentInput_;
    std::string ghostText_;
    std::string ghostProviderName_;
    bool enabled_ = true;
    bool hasSyncResult_ = false;
};

} // namespace termcore

#endif // TERMCORE_COMPLETION_MANAGER_H
