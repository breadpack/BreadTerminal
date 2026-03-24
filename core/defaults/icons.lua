-- BreadTerminal default process icon mappings (Nerd Font)

local icons = {
    bash             = "F489",
    sh               = "F489",
    zsh              = "F489",
    fish             = "F489",
    ["cmd"]          = "E70F",
    ["cmd.exe"]      = "E70F",
    powershell       = "EBC7",
    pwsh             = "EBC7",
    ["powershell.exe"] = "EBC7",
    ["pwsh.exe"]     = "EBC7",
    python           = "E73C",
    python3          = "E73C",
    ["python.exe"]   = "E73C",
    ["python3.exe"]  = "E73C",
    node             = "E718",
    ["node.exe"]     = "E718",
    vim              = "E62B",
    nvim             = "E62B",
    git              = "E702",
    ["git.exe"]      = "E702",
    ssh              = "F489",
    ["ssh.exe"]      = "F489",
    docker           = "F308",
    ["docker.exe"]   = "F308",
    cargo            = "E7A8",
    rustc            = "E7A8",
    go               = "E626",
    ["go.exe"]       = "E626",
    ruby             = "E739",
    irb              = "E739",
    lua              = "E620",
    luajit           = "E620",
}

for process, icon in pairs(icons) do
    terminal.tab.set_process_icon(process, icon)
end
