plugin = {
    name = "url-detect",
    version = "0.1.0",
    author = "BreadTerminal",
    description = "Default URL scheme detection and colors",
    capabilities = {"config"},
}

-- BreadTerminal default URL schemes
terminal.url.add_scheme("https", "http", "ftp", "file", "ssh", "git")
terminal.url.set_color(0x89b4fa)
