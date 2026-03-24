-- BreadTerminal default paste guard rules
-- Override with: terminal.paste.add_danger() / terminal.paste.whitelist()

terminal.paste.set_mode("multiline")

terminal.paste.add_danger("sudo ",             "Contains sudo command")
terminal.paste.add_danger("sudo su",           "Contains sudo su (root shell)")
terminal.paste.add_danger("sudo %-i",          "Contains sudo -i (root shell)")
terminal.paste.add_danger("rm %-rf",           "Contains rm -rf command")
terminal.paste.add_danger("rm %-r ",           "Contains recursive rm command")
terminal.paste.add_danger("rm %-R ",           "Contains recursive rm command")
terminal.paste.add_danger("chmod %-R 777",     "Dangerous permission change")
terminal.paste.add_danger("curl.*|.*sh",       "Curl piped to shell")
terminal.paste.add_danger("curl.*|.*bash",     "Curl piped to shell")
terminal.paste.add_danger("wget.*|.*sh",       "Wget piped to shell")
terminal.paste.add_danger("wget.*|.*bash",     "Wget piped to shell")
terminal.paste.add_danger("base64.*%-d.*|.*sh","Encoded payload piped to shell")
