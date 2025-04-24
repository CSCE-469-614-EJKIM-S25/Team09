#!/bin/sh

# to run this script: bash wsl-deps.sh
# will prompt for password

sudo apt install zip
sudo apt install python3.10-venv
sudo apt install zlib1g-dev

chmod +x ./zsim/run-simulation
chmod +x ./zsim/run-all-xpolicy
chmod +x /$(pwd)/tools/pin-2.14-71313-gcc.4.4.7-linux/intel64/bin/pinbin

echo ""
echo "WSL configuration complete"