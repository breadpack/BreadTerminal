-- BreadTerminal default paste guard rules
-- Override with: terminal.paste.add_danger() / terminal.paste.whitelist()
--
-- Patterns use simple substring matching (not regex).
-- The C++ engine uses std::string::find(pattern), so pass plain text.

terminal.paste.set_mode("multiline")

-- sudo commands
terminal.paste.add_danger("sudo ",             "Contains sudo command")
terminal.paste.add_danger("sudo su",           "Contains sudo su (root shell)")
terminal.paste.add_danger("sudo -i",           "Contains sudo -i (root shell)")

-- recursive rm commands
terminal.paste.add_danger("rm -rf",            "Contains rm -rf command")
terminal.paste.add_danger("rm -r ",            "Contains recursive rm command")
terminal.paste.add_danger("rm -R ",            "Contains recursive rm command")

-- home directory wipe (rm with ~ or $HOME)
-- Uses compound patterns: both substrings must co-occur.
terminal.paste.add_compound_danger("rm -rf", "~",     "Recursive delete targeting home directory")
terminal.paste.add_compound_danger("rm -r ",  "~",     "Recursive delete targeting home directory")
terminal.paste.add_compound_danger("rm -R ",  "~",     "Recursive delete targeting home directory")
terminal.paste.add_compound_danger("rm -rf", "$HOME",  "Recursive delete targeting $HOME")
terminal.paste.add_compound_danger("rm -r ",  "$HOME",  "Recursive delete targeting $HOME")
terminal.paste.add_compound_danger("rm -R ",  "$HOME",  "Recursive delete targeting $HOME")

-- dangerous permission change
terminal.paste.add_danger("chmod -R 777",      "Dangerous recursive permission change")

-- curl/wget piped to shell (checks sourceCmd before pipe, sh/bash/zsh after pipe)
terminal.paste.add_pipe_danger("curl",         "Curl piped to shell")
terminal.paste.add_pipe_danger("wget",         "Wget piped to shell")

-- base64 decode piped to shell
terminal.paste.add_pipe_danger("base64",       "Encoded payload piped to shell")
