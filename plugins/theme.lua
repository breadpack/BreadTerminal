plugin = {
    name = "theme",
    version = "0.1.0",
    author = "BreadTerminal",
    description = "Default theme event logging plugin",
    capabilities = {"events"},
}

-- BreadTerminal default theme plugin
-- Loaded automatically at startup

terminal.on("title_change", function(title)
    terminal.log("Title changed: " .. title)
end)

terminal.on("notification", function(data)
    terminal.log("Notification: " .. data)
end)
