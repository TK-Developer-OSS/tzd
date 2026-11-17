#!/bin/bash
# phpMyAdmin 2.2.6 (April 2002; the 2.2 line supports PHP 3), as a PHP
# benchmark for the TzD mod_php3 (task #54).  Source: the RELEASE_2_2_6
# tag of github.com/phpmyadmin/phpmyadmin (TK asked for it 2026-11-14;
# the archive's RELEASE_2_2_7PL1 tag is a 2.3.0-dev trunk snapshot), in
# work/pma.
#   tests/pma/install.sh <htdocs> <mysql host:port> <url of the pma dir>
# installs it in <htdocs>/pma, logging in to MySQL as admin (config auth).
set -e
T=$(cd "$(dirname "$0")" && pwd); R=$T/../..
S=$R/work/pma/src/phpmyadmin-RELEASE_2_2_6
[ -d "$S" ] || { echo "no phpMyAdmin source in $S"; exit 1; }
H=$1; host=${2%:*}; port=${2#*:}; uri=$3
rm -rf "$H/pma"; cp -a "$S" "$H/pma"
sed -i "s|^\$cfgPmaAbsoluteUri = '';|\$cfgPmaAbsoluteUri = '$uri';|
        s|^\$cfgServers\[\$i\]\['host'\]          = 'localhost';|\$cfgServers[\$i]['host']          = '$host';|
        0,/^\$cfgServers\[\$i\]\['port'\]          = '';/s||\$cfgServers[\$i]['port']          = '$port';|
        0,/^\$cfgServers\[\$i\]\['user'\]          = 'root';/s||\$cfgServers[\$i]['user']          = 'admin';|
        0,/^\$cfgServers\[\$i\]\['password'\]      = '';/s||\$cfgServers[\$i]['password']      = 'adminpass';|" \
  "$H/pma/config.inc.php3"
grep -q "'adminpass'" "$H/pma/config.inc.php3"
