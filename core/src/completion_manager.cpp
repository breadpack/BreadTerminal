#include "termcore/completion_manager.h"
#include <algorithm>

namespace termcore {

CompletionManager::CompletionManager() {
    Provider historyProv;
    historyProv.name = "history";
    historyProv.priority = 100;
    historyProv.getSuggestion = [this](const std::string& input,
                                       const std::string&) -> std::string {
        return historyProvider_.suggest(input);
    };
    registerProvider(std::move(historyProv));
}

void CompletionManager::registerProvider(Provider provider) {
    removeProvider(provider.name);
    auto it = std::find_if(providers_.begin(), providers_.end(),
        [&](const Provider& p) { return p.priority < provider.priority; });
    providers_.insert(it, std::move(provider));
}

void CompletionManager::removeProvider(const std::string& name) {
    providers_.erase(
        std::remove_if(providers_.begin(), providers_.end(),
            [&](const Provider& p) { return p.name == name; }),
        providers_.end());
}

void CompletionManager::onInputChanged(const std::string& currentInput,
                                        const std::string& cwd) {
    currentInput_ = currentInput;
    ghostText_.clear();
    ghostProviderName_.clear();
    hasSyncResult_ = false;

    if (!enabled_ || currentInput.empty()) return;

    for (const auto& provider : providers_) {
        if (!provider.getSuggestion) continue;
        std::string suggestion = provider.getSuggestion(currentInput, cwd);
        if (!suggestion.empty() && suggestion.size() > currentInput.size()) {
            ghostText_ = suggestion.substr(currentInput.size());
            ghostProviderName_ = provider.name;
            hasSyncResult_ = true;
            return;
        }
    }
}

void CompletionManager::setSuggestion(const std::string& providerName,
                                       const std::string& suggestion) {
    if (hasSyncResult_) return;
    if (!ghostText_.empty()) return;

    if (!suggestion.empty() && suggestion.size() > currentInput_.size() &&
        suggestion.compare(0, currentInput_.size(), currentInput_) == 0) {
        ghostText_ = suggestion.substr(currentInput_.size());
        ghostProviderName_ = providerName;
    }
}

std::string CompletionManager::acceptFull() {
    std::string result = ghostText_;
    ghostText_.clear();
    ghostProviderName_.clear();
    return result;
}

std::string CompletionManager::acceptWord() {
    if (ghostText_.empty()) return "";

    auto isWordBoundary = [](char c) {
        return c == '/' || c == '.' || c == '-' || c == '_' || c == ' ';
    };

    size_t pos = 0;
    while (pos < ghostText_.size() && isWordBoundary(ghostText_[pos])) {
        ++pos;
    }
    while (pos < ghostText_.size() && !isWordBoundary(ghostText_[pos])) {
        ++pos;
    }

    std::string word = ghostText_.substr(0, pos);
    ghostText_ = ghostText_.substr(pos);
    if (ghostText_.empty()) {
        ghostProviderName_.clear();
    }
    return word;
}

void CompletionManager::clear() {
    ghostText_.clear();
    ghostProviderName_.clear();
    currentInput_.clear();
    hasSyncResult_ = false;
}

} // namespace termcore
