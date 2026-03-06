#!/bin/bash

# Fan Service Wrapper Script
# Sets up environment and starts the fan service

# wrapper now relative to project structure

SCRIPT_DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )"
PROJECT_ROOT="$SCRIPT_DIR/.."

export LD_LIBRARY_PATH="$PROJECT_ROOT/libs/Doly/libs:$LD_LIBRARY_PATH"
export PATH="/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin:$PATH"

# Start the service from build directory
exec "$PROJECT_ROOT/build/fan_service"
