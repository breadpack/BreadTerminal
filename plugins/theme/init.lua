-- BreadTerminal default theme plugin
-- Loaded automatically at startup

terminal.on("title_change", function(title)
    terminal.log("Title changed: " .. title)
end)

terminal.on("notification", function(data)
    terminal.log("Notification: " .. data)
end)
