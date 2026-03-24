-- BreadTerminal default tab title formatting
-- Override with: terminal.tab.on_title_format(function(info) ... end)

local shell_names = {
    ["cmd.exe"] = true, ["powershell.exe"] = true, ["pwsh.exe"] = true,
    ["powershell"] = true, ["pwsh"] = true,
    ["bash"] = true, ["bash.exe"] = true,
    ["zsh"] = true, ["fish"] = true, ["sh"] = true,
    ["wsl.exe"] = true, ["wsl"] = true,
    ["login"] = true, ["tmux"] = true, ["screen"] = true,
}

local function extract_last_component(path)
    return path:match("([^/\\]+)$") or path
end

local function extract_meaningful_title(title)
    -- Handle "PREFIX:path" patterns (e.g., "MINGW64:/c/Users/foo")
    local _, path = title:match("^(%u[%u%d_]+):(.+)$")
    if path then
        return extract_last_component(path)
    end
    -- Handle plain paths
    if title:find("[/\\]") then
        return extract_last_component(title)
    end
    return title
end

local function default_title_format(info)
    -- Custom title override (from terminal.tab.set_title)
    if info.custom_title and info.custom_title ~= "" then
        return info.custom_title
    end

    -- Meaningful screen title from non-shell process
    if info.title and info.title ~= "" then
        local is_shell = shell_names[info.process or ""]
        if not is_shell then
            return extract_meaningful_title(info.title)
        end
    end

    -- Working directory last component
    if info.cwd and info.cwd ~= "" then
        return extract_last_component(info.cwd)
    end

    -- Process name
    if info.process and info.process ~= "" then
        return info.process
    end

    -- Fallback
    return "Tab " .. (info.tab_index or 0)
end

terminal.tab.defaults = terminal.tab.defaults or {}
terminal.tab.defaults.title_format = default_title_format
terminal.tab.defaults.shell_names = shell_names
terminal.tab.on_title_format(default_title_format)
