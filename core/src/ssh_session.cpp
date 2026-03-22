#include "termcore/ssh_session.h"

#include "termcore/config.h"

namespace termcore {

// Currently SshSessionConfig is a plain struct with sensible defaults.
// When config loading is needed, the auto_deploy_integration field can be
// read from the user's configuration:
//
//   [ssh]
//   auto_deploy_integration = true
//
// This file serves as the integration point for future config-driven SSH
// session setup.

} // namespace termcore
